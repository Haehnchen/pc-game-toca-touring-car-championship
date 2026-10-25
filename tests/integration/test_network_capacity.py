"""Focused eight-peer lifecycle acceptance, with an optional ten-minute soak.

Normal menus and held keyboard input are the only application controls. The
proxy neither knows ENet nor edits a game packet. The ordinary case checks
concrete lifecycle outcomes; --soak adds repeated full races and sustained load.
"""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import socket
import time

from native_support import TraceReader, require, wait_for
from network_impairment import Impairment, UdpProxy
from test_full_lap import route_source, route_targets
from test_network_connect import Peer
from test_network_race import RacePeer


CAPACITY_SECONDS = 15
SOAK_SECONDS = 600
BASE_SEED = 1414480800


class RecentTraceReader(TraceReader):
    """Keep parser offsets but only the latest diagnostic rows in memory."""

    def rows(self):
        rows = super().rows()
        if len(rows) > 256:
            self._rows = rows[-256:]
        return self._rows.copy()


def race_peer(name, executable, data):
    peer = RacePeer(name, executable, data)
    # The short two-peer test deliberately retains history for its assertions;
    # this ten-minute test uses current rows and explicit five-second samples.
    peer.reader = RecentTraceReader(Path(str(peer.trace) + ".network.csv"))
    peer.game_reader = RecentTraceReader(peer.trace)
    peer.ticks = RecentTraceReader(Path(str(peer.trace) + ".simulation.csv"))
    return peer


def unused_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def host_menu(peer, port):
    peer.enter()
    peer.host_setup("HOST", port)


def join_menu(peer, name, port):
    peer.enter()
    peer.join_setup(name, port)


def create_group(prefix, executable, data, peers, proxies):
    group = []
    port = unused_port()
    host = race_peer(f"{prefix}-host", executable, data)
    peers.append(host)
    group.append(host)
    host.test_port = port
    host_menu(host, port)
    def join(index):
        proxy = UdpProxy(("127.0.0.1", port), Impairment(BASE_SEED + index))
        proxies.append(proxy)
        client = race_peer(f"{prefix}-client-{index}", executable, data)
        peers.append(client)
        client.test_port = proxy.address[1]
        client.test_name = f"P{index}"
        join_menu(client, client.test_name, client.test_port)
        client.wait(phase="lobby")
        client.wait_game(selection=0)
        return client
    # Each worker owns its process, display, input and trace readers. Wait for
    # every worker before teardown, including when one launch fails.
    with ThreadPoolExecutor(max_workers=7) as workers:
        group.extend(workers.map(join, range(1, 8)))
    for peer in group:
        peer.wait(phase="lobby", players=8)
    wait_for(lambda: len({peer.current()["roster"] for peer in group}) == 1,
             "Eight identical committed rosters", timeout=20)
    require(len({peer.environment["DISPLAY"] for peer in group}) == 8,
            "Capacity peers must own eight independent private displays")
    return group, port


def configure_one_lap(host):
    host.one_lap_options()


def lobby_converged(group):
    def shared_snapshot():
        rows = [peer.current() for peer in group]
        if not all(row.get("phase") == "lobby" and row.get("players") == "8" for row in rows):
            return False
        fields = ("generation", "roster_revision", "settings_revision", "roster", "track", "weather", "laps", "ai")
        return rows if len({tuple(row[field] for field in fields) for row in rows}) == 1 else False
    return wait_for(shared_snapshot, "Eight peers share current lobby roster/settings revisions", timeout=20)


def member_ready(row, player):
    return any((fields := member.split(":"))[0] == player and fields[-1] == "1"
               for member in row["roster"].strip("/").split("/"))


def start_group(group):
    before = lobby_converged(group)[0]
    def ready(peer):
        peer.wait(phase="lobby", players=8)
        player = peer.current()["local_player"]
        require(not member_ready(before, player), "New race inherited an already-ready human")
        peer.waiting_room()
    # Exercise concurrent confirmations of the same choices; Ready-only roster
    # broadcasts must not invalidate another human's confirmation.
    with ThreadPoolExecutor(max_workers=8) as workers:
        list(workers.map(ready, group))
    wait_for(lambda: all(all(member.endswith(":1") for member in
                             peer.current()["roster"].strip("/").split("/")) for peer in group),
             "Eight members ready against current revisions", timeout=20)
    host = group[0]
    host.command("xdotool", "key", "Return")
    for peer in group:
        wait_for(lambda: (row := peer.game()) and row["screen"] == "scene" and row["game_mode"] == "3",
                 "Eight-car native Linkup scene", timeout=60)
        peer.previous_step = None
        peer.previous_forward = None
    require(len({peer.game()["network_actor"] for peer in group}) == 8,
            "Each human must have a distinct physical input owner")
    require(all(peer.game()["network_actors"] == "8" and peer.game()["rival_count"] == "7" for peer in group),
            "Committed native fleet must contain exactly eight cars")
    require(all(int(peer.current()["load_service_calls"]) > 0 for peer in group),
            "All eight scene constructors must service their live endpoint")


def return_group(group):
    require(len({peer.current()["finish_order"] for peer in group}) == 1,
            "Eight peers disagree on owner-authored finish order/time")
    for peer in group:
        peer.keys(set())
        peer.wait_game(session_state=13, race_classified=1, race_result_count=8)
        require(int(peer.game()["race_result_ms"]) > 0 and
                peer.game()["race_result_ms"] == peer.game()["race_total_ms"],
                "Classification changed an owner's local finish time")
    require(len({peer.game()["network_finish_order"] for peer in group}) == 1,
            "Rendered eight-car results disagree")
    # Original results close the session. Rejoin through the real name/options/
    # garage route before the next race; old actor generations cannot survive.
    group[0].command("xdotool", "key", "Return")
    group[0].wait(phase="idle", players=0)
    for peer in group[1:]:
        peer.wait(phase="failed", players=0)
    for peer in group:
        peer.reopen()
    group[0].host_setup("HOST", group[0].test_port)
    configure_one_lap(group[0])
    with ThreadPoolExecutor(max_workers=7) as workers:
        list(workers.map(lambda peer: peer.join_setup(peer.test_name, peer.test_port), group[1:]))
    lobby_converged(group)


def launch_contact(group, proxies):
    """Brake the front row while the following rows launch, using real input."""
    for peer in group:
        wait_for(lambda: peer.game()["start_held"] == "0", "Eight-owner shared grid release", timeout=25)
    rows = [(peer, peer.ticks.rows()[-1]) for peer in group]
    direction = tuple(int(rows[0][1][name]) for name in ("body_forward_x", "body_forward_z"))
    front = {peer for peer, _ in sorted(rows, key=lambda item:
             int(item[1]["body_x"]) * direction[0] + int(item[1]["body_z"]) * direction[1], reverse=True)[:2]}
    for peer in group:
        peer.keys({"Down"} if peer in front else {"Up"})
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        for peer in group:
            require(peer.current()["phase"] == "racing", "Grid launch lost an admitted peer")
        for proxy in proxies:
            proxy.check()
        time.sleep(.02)
    for peer in group:
        peer.previous_step = None
        peer.previous_forward = None


def rss_kib(peer):
    for line in Path(f"/proc/{peer.process.pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    raise AssertionError("Native process has no resident-memory metric")


def sample(group, proxies, elapsed):
    rows = []
    for peer in group:
        row = dict(peer.current())
        require(row["phase"] in ("loading", "grid", "racing", "results", "lobby"),
                f"Unexpected capacity endpoint loss: {peer.directory.name}: {row}")
        require(row["players"] == "8", "An admitted peer vanished during the eight-peer interval")
        require(int(row["peak_reliable"]) <= 8 * 65536, "Reliable transport storage exceeded its peer bounds")
        links = 7 if row["hosting"] == "1" else 1
        require(int(row["latest_pending_peak"]) <= 8 * links, "Latest-state storage exceeded eight slots per link")
        row.update(peer=peer.directory.name, rss_kib=rss_kib(peer),
                   car_contacts=int(peer.game()["car_contacts"]),
                   rival_voices=int(peer.game()["rival_voices"]))
        rows.append(row)
    proxy_rows = [proxy.statistics() for proxy in proxies]
    require(all(row["peak_pending"] <= UdpProxy.max_pending and row["overflow"] == 0 and
                row["oversized"] == 0 and row["foreign"] == 0 for row in proxy_rows),
            "Private proxy exceeded its bounded UDP queue or saw an unexpected sender")
    return dict(elapsed_seconds=elapsed, peers=rows, proxies=proxy_rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("data")
    parser.add_argument("--soak", action="store_true", help="also require ten minutes and complete eight-player races")
    args = parser.parse_args()
    executable, data = args.executable, args.data
    duration = SOAK_SECONDS if args.soak else CAPACITY_SECONDS
    peers, proxies = [], []
    report = dict(status="failed", duration_required_seconds=duration, soak=args.soak,
                  input_transport="ordinary-keyboard-wall-time", source_full_parity=False,
                  profile="100ms-RTT/2%-loss/20ms-one-way-jitter/1%-duplicate/1%-reorder",
                  seeds=list(range(BASE_SEED + 1, BASE_SEED + 8)), samples=[], completed_races=[])
    started = time.monotonic()
    try:
        group, port = create_group("capacity", executable, data, peers, proxies)
        # A ninth real native application receives a reliable explicit reason,
        # while the eight admitted players remain usable.
        ninth = Peer("capacity-ninth", executable, data, "--network-join", f"127.0.0.1:{port}",
                     "--network-name", "NINTH")
        peers.append(ninth)
        rejected = ninth.wait(phase="failed", players=0)
        require("full" in rejected["error"].lower(), "Ninth human did not receive the explicit capacity rejection")
        report["ninth_rejection"] = rejected
        ninth.close()
        peers.remove(ninth)
        configure_one_lap(group[0])
        points = route_targets(data, 0)
        _, report["source"] = route_source(data, 0)
        start_group(group)
        interval_started = time.monotonic()
        launch_contact(group, proxies)
        next_sample = interval_started
        phases = Counter()
        collision_seen = audio_seen = False
        while time.monotonic() - interval_started < duration:
            now = time.monotonic()
            for peer in group:
                peer.drive(points)
            if now >= next_sample:
                value = sample(group, proxies, now - interval_started)
                report["samples"].append(value)
                collision_seen |= any(row["car_contacts"] > 0 for row in value["peers"])
                audio_seen |= all(row["rival_voices"] > 0 for row in value["peers"])
                phases.update(row["phase"] for row in value["peers"])
                print(f"Eight native peers: {now - interval_started:.1f}s, " + ", ".join(
                    f"{peer.directory.name}:point{peer.game()['next_point']}" for peer in group), flush=True)
                next_sample = now + 5
            if all(peer.current()["phase"] == "results" for peer in group):
                report["completed_races"].append(dict(generation=group[0].current()["race_generation"],
                    finish_order=group[0].current()["finish_order"], elapsed_seconds=now - interval_started))
                return_group(group)
                start_group(group)
            time.sleep(.005)
        report["eight_peer_seconds"] = time.monotonic() - interval_started
        report["samples"].append(sample(group, proxies, report["eight_peer_seconds"]))
        report["phase_samples"] = dict(phases)
        report["rss_bounds"] = {}
        for peer in group:
            measurements = [row["rss_kib"] for sample in report["samples"] for row in sample["peers"]
                            if row["peer"] == peer.directory.name]
            low, high = min(measurements), max(measurements)
            report["rss_bounds"][peer.directory.name] = dict(min_kib=low, max_kib=high, growth_kib=high - low)
            require(high - low <= 256 * 1024, "Native resident memory grew by more than256 MiB during the bounded run")
        require(all(proxy.statistics()["dropped"] > 0 for proxy in proxies), "An impaired link never exercised seeded loss")
        if args.soak:
            require(all(proxy.statistics()["duplicated"] > 0 and proxy.statistics()["reordered"] > 0 for proxy in proxies),
                    "A soak link never exercised all seeded duplication/reordering decisions")
        require(all(any(int(row["state_consumed"]) > 0 and int(row["controls_consumed"]) > 0
                        for value in report["samples"] for row in value["peers"]
                        if row["peer"] == peer.directory.name) for peer in group),
                "A native peer never consumed actual remote state and changed controls")
        if args.soak:
            require(report["completed_races"], "Eight physical wall-time drivers never completed a full race")
        require(collision_seen, "Eight-peer run did not exercise actual physical car contact")
        require(audio_seen, "Eight-peer run never used all eight real spatial remote-motor consumers")
        report["collision_seen"], report["remote_audio_seen"] = collision_seen, audio_seen
        if all(peer.current()["phase"] == "results" for peer in group):
            return_group(group)
            start_group(group)
        for peer in group:
            peer.keys(set())
            wait_for(lambda: peer.current()["phase"] in ("grid", "racing"), "Active race before peer loss", timeout=30)
        victim = next((peer for peer in reversed(group[1:]) if peer.game()["completed_laps"] == "0"), group[-1])
        lost_actor = victim.game()["network_actor"]
        victim.process.kill()
        victim.process.wait(timeout=3)
        survivors = [peer for peer in group if peer is not victim]
        for peer in survivors:
            peer.wait(active_humans=7)
            require(peer.game()["network_actors"] == "8", "Client loss deleted the retained physical human actor")
        wait_for(lambda: any(entry.split(":")[:2] == [lost_actor, "0"] for entry in
                             group[0].game()["network_actor_states"].strip("/").split("/")),
                 "Client loss installs inactive-human host takeover", timeout=5)
        report["client_loss"] = dict(actor=lost_actor, host=group[0].current(),
                                    actor_states=group[0].game()["network_actor_states"])
        survivors[0].process.kill()
        survivors[0].process.wait(timeout=3)
        for peer in survivors[1:]:
            peer.wait(phase="failed", players=0)
            peer.wait_game(screen="network_linkup", scene_active=0, rival_count=0)
            require(peer.game()["music_track"] == "2", "Host loss failed to restore frontend audio")
        # Close every old endpoint/display before creating a genuinely fresh
        # lobby. The new eight peers must not inherit old actor ownership.
        report["final_sample"] = [dict(peer=peer.directory.name, **peer.reader.rows()[-1]) for peer in group]
        for peer in reversed(group):
            peer.close()
            peers.remove(peer)
        report["proxy_final"] = [proxy.statistics() for proxy in proxies]
        for proxy in reversed(proxies):
            proxy.close()
        proxies.clear()
        fresh, _ = create_group("fresh", executable, data, peers, proxies)
        start_group(fresh)
        report["fresh_lobby"] = [peer.current() for peer in fresh]
        require(all(peer.game()["completed_laps"] == "0" for peer in fresh), "Fresh lobby inherited old race progress")
        report.update(status="passed", wall_seconds=time.monotonic() - started)
        print("Eight native peers passed impaired driving/contact/audio, concurrent Ready, ninth rejection, client/host loss and a fresh lobby"
              + ("; ten-minute soak and full results/restart also passed" if args.soak else ""), flush=True)
    finally:
        report["wall_seconds"] = time.monotonic() - started
        Path("network-capacity-summary.json").write_text(json.dumps(report, indent=2) + "\n")
        for peer in reversed(peers):
            peer.close()
        for proxy in reversed(proxies):
            proxy.close()


if __name__ == "__main__":
    main()
