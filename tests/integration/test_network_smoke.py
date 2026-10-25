"""Short two-game Linkup self-test through normal menus and keyboard driving.

Arguments: GAME DATA_ROOT. Each game owns a private Xvfb display and offline
muted audio. A private UDP proxy adds100ms RTT/2% loss below real ENet; the
fixture never changes OS traffic settings, game packets or physical car state.
"""

import json
from pathlib import Path
import sys
import time

from native_support import require, wait_for
from network_impairment import Impairment
from test_network_ai import actors
from test_network_race import RacePeer, connect, pause_checks, start
from test_network_recovery import end_race


def travelled(before, after):
    # Q16 physical positions; ignore vertical suspension movement.
    return sum((after[axis] - before[axis]) ** 2 for axis in (0, 2)) > 65536 ** 2


def main():
    require(len(sys.argv) == 3, "Usage: test_network_smoke.py GAME DATA_ROOT")
    peers = []
    proxy = None
    report = {"status": "failed", "input_transport": "ordinary-keyboard-wall-time",
              "driving_seconds": 3, "full_race_acceptance": False}
    started = time.monotonic()
    try:
        for name in ("host", "client"):
            peers.append(RacePeer(name, *sys.argv[1:3]))
        host, client = peers
        proxy = connect(host, client, Impairment(seed=31, delay_ms=50, loss=.02,
                                               jitter_ms=0, duplicate=0, reorder=0))
        require(host.environment["DISPLAY"] != client.environment["DISPLAY"], "Games must use independent private displays")
        host.one_lap_options()
        start(host, client)
        local = [int(peer.game()["network_actor"]) for peer in peers]
        require(len(set(local)) == 2, "Two players must own different physical cars")
        for peer in peers:
            require(peer.game()["network_actors"] == "2" and peer.game()["rival_count"] == "1", "Shared Start must create exactly two committed cars")
            require(int(peer.current()["load_service_calls"]) > 0, "Resource loading must service the connected transport")
            require(peer.game()["audio_active"] == "1" and "offline=1" in (peer.directory / "game.log").read_text(), "Smoke must use the real offline audio path")
        for peer in peers:
            wait_for(lambda: peer.game()["start_held"] == "0", "Original shared grid release", timeout=18)
            peer.wait(phase="racing")
        baseline = [actors(peer.game()) for peer in peers]
        counters = [peer.current() for peer in peers]
        remote_poses = [set(), set()]
        voices = [0, 0]
        volumes = [0, 0]
        for peer in peers:
            peer.keys({"Up"})
        drive_started = time.monotonic()
        while (elapsed := time.monotonic() - drive_started) < report["driving_seconds"]:
            # A brief ordinary lift/reapply exercises changed controls as well as
            # repeated state, without relying on one unreliable pedal datagram.
            for peer in peers:
                peer.keys(set() if 1.3 <= elapsed < 1.5 else {"Up"})
            for index, peer in enumerate(peers):
                row = peer.game()
                require(row["screen"] == "scene" and row["scene_active"] == "1", "Driving unexpectedly left the active race")
                fleet = actors(row)
                require(set(fleet) == set(local), "Physical fleet lost a shared human actor")
                remote_poses[index].add(fleet[local[1 - index]]["position"])
                voices[index] = max(voices[index], int(row["rival_voices"]))
                volumes[index] = max(volumes[index], int(row["rival_volume"]))
            proxy.check()
            time.sleep(.02)
        for peer in peers:
            peer.keys(set())
        snapshots = []
        for index, peer in enumerate(peers):
            row = peer.game()
            fleet = actors(row)
            network = peer.current()
            own, remote = local[index], local[1 - index]
            require(travelled(baseline[index][own]["position"], fleet[own]["position"]) and fleet[own]["speed"] > 655,
                    f"{peer.directory.name}: ordinary local keyboard driving did not move its actual body")
            require(travelled(baseline[index][remote]["position"], fleet[remote]["position"]) and fleet[remote]["speed"] > 655
                    and len(remote_poses[index]) >= 3, f"{peer.directory.name}: received remote human pose did not keep changing")
            require(int(network["state_consumed"]) > int(counters[index]["state_consumed"])
                    and int(network["controls_consumed"]) > int(counters[index]["controls_consumed"]),
                    f"{peer.directory.name}: remote state and controls did not reach the physical consumer")
            require(voices[index] > 0 and volumes[index] > 0, f"{peer.directory.name}: moving remote car did not produce spatial motor audio")
            require(row["motion_boundary"] == "0" and row["rival_boundaries"] == "0", "Short driving left supported physical track coverage")
            peer.capture("network-smoke-driving.png")
            snapshots.append({"local_actor": own, "initial": baseline[index], "final": fleet,
                              "remote_distinct_poses": len(remote_poses[index]), "rival_voices": voices[index],
                              "rival_volume": volumes[index], "network": network})
        report["driving"] = snapshots
        pause_checks(host, client)
        report["pause_resume"] = "passed"
        end_race(host, client)
        for peer in peers:
            require(peer.game()["rival_voices"] == "0" and peer.game()["music_track"] == "2", "End Race retained a remote motor or failed to restore frontend music")
            peer.capture("network-smoke-cleanup.png")
        impairment = proxy.statistics()
        require(impairment["dropped"] > 0 and impairment["overflow"] == 0 and impairment["oversized"] == 0,
                "Seeded100msRTT/2%loss proxy must drop real datagrams without overflowing")
        report.update(status="passed", impairment=impairment,
                      final=[{"game": peer.game(), "network": peer.current()} for peer in peers])
        print("Linkup smoke passed: two real cars move and receive each other under100msRTT/2%loss; spatial motors, pause/resume and End Race cleanup work.")
    except Exception as error:
        report["error"] = str(error)
        for peer in peers:
            if peer.process.poll() is None and hasattr(peer, "window"):
                try:
                    peer.capture("network-smoke-failure.png")
                except Exception:
                    pass
        raise
    finally:
        report["wall_seconds"] = round(time.monotonic() - started, 3)
        try:
            if proxy:
                try:
                    report["impairment"] = proxy.statistics()
                finally:
                    proxy.close()
        finally:
            try:
                for peer in reversed(peers):
                    peer.close()
            finally:
                Path("network-smoke-summary.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
