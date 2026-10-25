"""Source trace checks for memory."""

import tempfile
import struct
import os
from pathlib import Path
from unittest.mock import patch

from .support import TRACE, TraceTestCase


class MemoryTests(TraceTestCase):
    def test_scalar_register_parser_accepts_actual_decimal_zero_without_weakening_pointers(self):
        self.assertEqual(TRACE.register_value("0"), 0)
        self.assertEqual(TRACE.register_value("25"), 25)
        self.assertEqual(TRACE.register_value("-1"), 0xffffffff)
        self.assertEqual(TRACE.register_value("0x19"), 25)
        with self.assertRaisesRegex(RuntimeError, "register address"):
            TRACE.register_address("0")

    def test_words_preserve_source_width_and_little_endian_bytes(self):
        text = "0x004ad080: 0x00010000 0xffff8000\n0x004ad088: 0x12345678\n"
        self.assertEqual(TRACE.parse_memory(text, 0x4ad080, 12), bytes.fromhex("000001000080ffff78563412"))

    def test_actual_winedbg_unhandled_exception_is_logged_and_never_resumed(self):
        debugger = self.debugger()
        cache = debugger.paused_memory()
        raw = (b"Unhandled exception: divide by zero in 32-bit code (0x0042eea3).\n"
               b"Register dump:\n EIP:0042eea3 EAX:e0460000 ECX:00000499 EDX:00007cb2\nWine-dbg>")
        read_fd, write_fd = os.pipe()
        reader = os.fdopen(read_fd, "rb", buffering=0)
        self.addCleanup(reader.close)
        os.write(write_fd, raw)
        os.close(write_fd)
        debugger.process.stdout = reader
        with self.assertRaisesRegex(TRACE.SourceException, "Unhandled exception: divide by zero"):
            debugger.read_prompt()
        self.assertEqual(debugger.log.getvalue(), raw)
        self.assertFalse(debugger.confirmed_pause)
        self.assertEqual(debugger.commands.getvalue(), "")
        with self.assertRaisesRegex(RuntimeError, "crossed"):
            cache.memory(0x1000, 4)

    def test_batched_reads_preserve_multiple_prompts_in_one_real_pipe_chunk(self):
        debugger = self.debugger()
        read_fd, write_fd = os.pipe()
        reader = os.fdopen(read_fd, "rb", buffering=0)
        self.addCleanup(reader.close)
        os.write(write_fd, b"00000011\nWine-dbg>00000022\nWine-dbg>")
        os.close(write_fd)
        debugger.process.stdout = reader
        result = debugger.memory_many([(0x1000, 4), (0x2000, 4)])
        self.assertEqual(result, [struct.pack("<I", 17), struct.pack("<I", 34)])
        self.assertEqual(debugger.process.stdin.getvalue(), b"x /1x 0x00001000\nx /1x 0x00002000\n")
        self.assertEqual(debugger.pending, b"")
        self.assertTrue(debugger.confirmed_pause)

    def test_process_memory_checks_debugger_identity_and_reads_only_while_stopped(self):
        debugger = self.debugger(b"00000011\nWine-dbg>00000022\nWine-dbg>")
        words = [struct.pack("<I", value) * 16 for value in (17, 34)]
        # A real read-only descriptor exercises pread without touching a game.
        with tempfile.TemporaryFile() as memory:
            for address, raw in zip((0x400000, TRACE.LAYOUT["body_base"]), words):
                memory.seek(address); memory.write(raw)
            memory.flush()
            descriptor = os.dup(memory.fileno())
            with patch.object(TRACE.os, "open", return_value=descriptor) as opening, \
                    patch.object(debugger, "memory", side_effect=words):
                debugger.enable_process_memory(1234)
            opening.assert_called_once_with("/proc/1234/mem", os.O_RDONLY | os.O_CLOEXEC)
            self.addCleanup(debugger.close_memory)
            self.assertEqual(debugger.memory(0x400000, 64), words[0])
            self.assertEqual(debugger.memory_many([(TRACE.LAYOUT["body_base"], 64)]), [words[1]])
            debugger.send("cont")
            with self.assertRaisesRegex(RuntimeError, "confirmed"):
                debugger.memory(0x400000, 4)
            debugger.confirm_pause()
            with self.assertRaisesRegex(RuntimeError, "Short"):
                debugger.memory_many([(0x10000000, 4)])
            self.assertFalse(debugger.confirmed_pause)

    def test_process_memory_rejects_wrong_address_space_and_closes_descriptor(self):
        debugger = self.debugger()
        with patch.object(TRACE.os, "open", return_value=77), \
                patch.object(TRACE.os, "pread", return_value=b"wrong"), \
                patch.object(TRACE.os, "close") as close, \
                patch.object(debugger, "memory", return_value=bytes(64)):
            with self.assertRaisesRegex(RuntimeError, "differs"):
                debugger.enable_process_memory(1234)
            close.assert_called_once_with(77)
        self.assertIsNone(getattr(debugger, "memory_fd", None))

    def test_batch_failure_drains_responses_and_invalidates_snapshot_cache(self):
        debugger = self.debugger()
        cache = debugger.paused_memory()
        with patch.object(debugger, "read_prompt", side_effect=["Cannot read memory", "00000022"]) as prompt:
            with self.assertRaisesRegex(RuntimeError, "rejected"):
                debugger.memory_many([(0x1000, 4), (0x2000, 4)])
        self.assertEqual(prompt.call_count, 2)
        with self.assertRaisesRegex(RuntimeError, "crossed"):
            cache.memory(0x1000, 4)
        debugger.confirm_pause()
        with patch.object(debugger, "read_prompt", side_effect=["00000011", TimeoutError("interrupted batch")]):
            with self.assertRaises(TimeoutError):
                debugger.memory_many([(0x1000, 4), (0x2000, 4)])
        self.assertFalse(debugger.confirmed_pause)

    def test_snapshot_cache_merges_overlaps_but_never_crosses_progress_or_new_stop(self):
        debugger = self.debugger()
        cache = debugger.paused_memory()
        with patch.object(debugger, "memory_many", return_value=[bytes(range(12))]) as read:
            cache.preload([(0x1000, 8), (0x1004, 8)])
            self.assertEqual(cache.memory(0x1004, 4), bytes(range(4, 8)))
            self.assertEqual(cache.memory(0x1000, 0), b"")
            read.assert_called_once_with([(0x1000, 12)])
            debugger.send("print $esp")
            self.assertEqual(cache.word(0x1000), 0x03020100)
            debugger.send("cont")
            with self.assertRaisesRegex(RuntimeError, "crossed"):
                cache.word(0x1000)
        debugger.confirm_pause()
        cache = debugger.paused_memory()
        debugger.confirm_pause()
        with self.assertRaisesRegex(RuntimeError, "crossed"):
            cache.word(0x1000)
        debugger.invalidate_pause()
        with self.assertRaisesRegex(RuntimeError, "confirmed"):
            debugger.paused_memory()

    def test_no_values_or_truncated_and_noncontiguous_reads_fail(self):
        for response in ("No symbols found for x\nUndefined symbol\n", "004ad080: 00010000\n", "004ad084: 00010000 00010000\n"):
            with self.subTest(response=response), self.assertRaises(RuntimeError):
                TRACE.parse_memory(response, 0x4ad080, 8)

    def test_real_wine_bare_word_rows_and_single_word(self):
        self.assertEqual(TRACE.parse_memory(" 0000000e\n", 0x4d37a0, 4), bytes.fromhex("0e000000"))
        self.assertEqual(TRACE.parse_memory(" 00010000 ffff8000\n 12345678\n", 0x4ad080, 12), bytes.fromhex("000001000080ffff78563412"))

    def test_real_wine_symbol_label_does_not_replace_memory_address(self):
        self.assertEqual(TRACE.parse_memory("0x004d2538 tourcars+0xd2538: ff74046c ff8b2541\n", 0x4d2538, 8), bytes.fromhex("6c0474ff41258bff"))
        with self.assertRaises(RuntimeError):
            TRACE.parse_memory("0x004d253c tourcars+0xd253c: ff74046c ff8b2541\n", 0x4d2538, 8)

    def test_signed_clock_values_retain_raw_bits(self):
        self.assertEqual(TRACE.signed(0xffffffff), -1)
        self.assertEqual(TRACE.signed(0x80000000), -2147483648)
        self.assertEqual(TRACE.signed(0x10000), 65536)

    def test_actual_short_hex_stack_register_and_unambiguous_address(self):
        self.assertEqual(TRACE.register_address("0x83fa6c\n"), 0x83fa6c)
        self.assertEqual(TRACE.register_address("0083fa6c\n"), 0x83fa6c)
        with self.assertRaises(RuntimeError):
            TRACE.register_address("Undefined symbol")

    def test_attachment_requires_this_invocations_display_runtime_and_prefix(self):
        environment = {"DISPLAY": ":77", "XDG_RUNTIME_DIR": "/private/case/runtime", "WINEPREFIX": "/serialized/wine"}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for pid, display, runtime, prefix in ((12, ":77", "/private/case/runtime", "/serialized/wine"),
                                                  (13, ":0", "/private/case/runtime", "/serialized/wine"),
                                                  (14, ":77", "/other/runtime", "/serialized/wine"),
                                                  (15, ":77", "/private/case/runtime", "/other/wine")):
                process = root / str(pid)
                process.mkdir()
                (process / "comm").write_text("tourcars.exe\n")
                (process / "environ").write_bytes(f"DISPLAY={display}\0XDG_RUNTIME_DIR={runtime}\0WINEPREFIX={prefix}\0".encode())
            self.assertEqual(TRACE.owned_game_processes(environment, root), [12])
