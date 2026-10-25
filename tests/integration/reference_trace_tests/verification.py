"""Source trace checks for verification."""

import unittest
import tempfile
import io
import json
import signal
import subprocess
import time
import zlib
from pathlib import Path
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE


class RetainedPrefixVerifierTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.source = self.directory / "trace.jsonl"
        self.raw = b'{"type":"header"}\n{"type":"update","id":0}\n{"type":"update","id":1}\n'
        self.source.write_bytes(self.raw)
        self.checksum = zlib.crc32(self.raw)
        self.quality = {"passed": False, "coverage_sufficient": False}
        self.verifier = TRACE.RetainedPrefixVerifier(self.directory, Path("/synthetic/replay"))
        patcher = patch.object(TRACE.os, "killpg")
        self.kill = patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.verifier.cancel)

    def start(self):
        process = Mock(pid=1234567890)
        process.poll.return_value = None
        process.wait.return_value = 0
        with patch.object(TRACE.subprocess, "Popen", return_value=process) as launch:
            self.assertTrue(self.verifier.start(self.source, 2, len(self.raw), self.checksum, self.quality))
            self.assertFalse(self.verifier.start(self.source, 2, len(self.raw), self.checksum, self.quality))
        launch.assert_called_once()
        self.assertIn("--compare-startup-world-reset", launch.call_args.args[0])
        return process

    def report(self, **changes):
        report = {"status": "compared", "mode": "startup-world-reset", "source_updates": 2,
                  "complete_source_interval": True, "numerical_parity": True, "exact_prefix_updates": 2,
                  "trace_crc32": self.verifier.receipt["trace_crc32"], "parity": False, "coverage_sufficient": False}
        report.update(changes)
        (self.directory / "verify/fleet-comparison-startup-world-reset.json").write_text(json.dumps(report))

    def test_background_copy_is_complete_immutable_and_never_waits_for_a_running_job(self):
        process = self.start()
        snapshot = self.directory / "verify-prefix.jsonl"
        copied = snapshot.read_bytes()
        self.assertTrue(copied.startswith(self.raw))
        footer = json.loads(copied.splitlines()[-1])
        self.assertEqual((footer["updates"], footer["prefix_crc32"], footer["coverage"]), (2, f"{self.checksum:08x}", self.quality))
        self.assertEqual(f"{zlib.crc32(copied):08x}", self.verifier.receipt["trace_crc32"])
        with self.source.open("ab") as source: source.write(b'{"type":"update","id":2}\n')
        self.assertEqual(snapshot.read_bytes(), copied)
        self.assertIsNone(self.verifier.poll())
        process.wait.assert_not_called()

    def test_exact_receipt_survives_a_following_job_overwriting_the_report(self):
        process = self.start()
        log = self.verifier.log
        self.report()
        process.poll.return_value = 1 #Numerically exact, while full lifecycle coverage is pending.
        with patch("sys.stdout", new=io.StringIO()): receipt = self.verifier.poll()
        self.assertEqual(receipt["status"], "exact")
        self.assertTrue(log.closed)
        saved = (self.directory / "verify-last-exact.json").read_bytes()
        self.assertEqual(json.loads(saved)["trace_crc32"], receipt["trace_crc32"])
        self.start()
        self.assertFalse((self.directory / "verify/fleet-comparison-startup-world-reset.json").exists())
        self.assertEqual((self.directory / "verify-last-exact.json").read_bytes(), saved)
        self.assertEqual(self.verifier.verified_updates, 2)

    def test_result_is_bound_to_the_immutable_snapshot_crc(self):
        process = self.start()
        self.report(trace_crc32="00000000")
        process.poll.return_value = 0
        with patch("sys.stdout", new=io.StringIO()): receipt = self.verifier.poll()
        self.assertEqual(receipt["status"], "different")
        self.assertEqual(self.verifier.verified_updates, 0)
        self.assertTrue((self.directory / "verify-prefix.jsonl").is_file())

    def test_cancel_reaps_the_leader_and_kills_remaining_native_children(self):
        process = self.start()
        log = self.verifier.log
        self.verifier.cancel()
        self.assertEqual(self.kill.call_args_list, [call(process.pid, signal.SIGTERM), call(process.pid, signal.SIGKILL)])
        self.assertEqual(process.wait.call_count, 2)
        self.assertTrue(log.closed)
        self.assertIsNone(self.verifier.process)
        self.assertEqual(self.verifier.receipt["status"], "cancelled")

    def test_timeout_and_vanished_group_both_close_and_reap_the_owned_checker(self):
        process = self.start()
        process.wait.side_effect = [subprocess.TimeoutExpired("replay", 5), 0]
        self.verifier.started = time.monotonic() - 601
        with patch("sys.stdout", new=io.StringIO()): receipt = self.verifier.poll()
        self.assertEqual(receipt["status"], "error")
        self.assertIsNone(self.verifier.process)
        process = self.start()
        self.kill.side_effect = ProcessLookupError
        self.verifier.cancel()
        self.assertEqual(process.wait.call_count, 2)

    def test_aborted_footer_preserves_only_finished_records_and_never_claims_lifecycle_completion(self):
        closed_raw = b"".join(self.raw.splitlines(keepends=True)[:2])
        closed = {"updates": 1, "source_bytes": len(closed_raw), "prefix_crc32": f"{zlib.crc32(closed_raw):08x}"}
        TRACE.finalize_aborted_prefix(self.source, 2, len(self.raw), self.checksum, {"passed": True}, "source #DE", closed)
        data = self.source.read_bytes()
        self.assertEqual(data[:len(self.raw)], self.raw)
        footer = json.loads(data[len(self.raw):])
        self.assertEqual((footer["updates"], footer["prefix_crc32"], footer["capture_status"]), (2, f"{self.checksum:08x}", "aborted"))
        self.assertFalse(footer["coverage"]["passed"])
        self.assertEqual(footer["last_closed_clock_prefix"], closed)
        partial = b'{"type":"header"}\n{"type":"update"'
        self.source.write_bytes(partial)
        with self.assertRaisesRegex(RuntimeError, "incomplete JSON"):
            TRACE.finalize_aborted_prefix(self.source, 0, len(partial) - 4, 0, self.quality, "source #DE", closed)
        with self.assertRaisesRegex(RuntimeError, "incomplete JSON"):
            TRACE.finalize_aborted_prefix(self.source, 1, len(partial), 0, self.quality, "source #DE", closed)
        self.assertEqual(self.source.read_bytes(), partial)

    def test_header_without_any_completed_update_keeps_the_original_exception_honest(self):
        header = b'{"type":"header"}\n'
        self.source.write_bytes(header)
        TRACE.finalize_aborted_prefix(self.source, 0, len(header), zlib.crc32(header), self.quality, "source #DE", {"updates": 0})
        self.assertEqual(self.source.read_bytes(), header)

    def test_final_timeout_leaves_a_complete_crc_footer_for_the_error_receipt(self):
        with patch.object(TRACE, "verify_retained_prefix", side_effect=subprocess.TimeoutExpired("replay", 600)):
            exact, error = TRACE.verify_final_capture(self.source, self.directory, Path("/synthetic/replay"), 2, self.checksum, self.quality)
        self.assertFalse(exact)
        self.assertIn("600", error)
        footer = json.loads(self.source.read_bytes().splitlines()[-1])
        self.assertEqual((footer["updates"], footer["prefix_crc32"]), (2, f"{self.checksum:08x}"))

    def test_final_comparison_keeps_real_footer_even_when_numeric_or_lifecycle_parity_is_missing(self):
        for result in (True, False):
            self.source.write_bytes(self.raw)
            with patch.object(TRACE, "verify_retained_prefix", return_value=result):
                exact, error = TRACE.verify_final_capture(self.source, self.directory, Path("/synthetic/replay"), 2, self.checksum, self.quality)
            self.assertEqual((exact, error), (result, None))
            data = self.source.read_bytes()
            self.assertEqual(data[:len(self.raw)], self.raw)
            footer = json.loads(data[len(self.raw):])
            self.assertEqual((footer["updates"], footer["prefix_crc32"], footer["coverage"]), (2, f"{self.checksum:08x}", self.quality))
