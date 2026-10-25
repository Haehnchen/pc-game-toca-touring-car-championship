"""Two real wall-clock Linkup players; only normal menus and held keyboard input.

The test driver follows canonical POINTS targets through diagnostic poses. It
never supplies packets, positions, route gates, lap counts or finish times.
"""

import json
import math
from pathlib import Path
import socket
import sys
import time

from native_support import TraceReader, require, wait_for
from network_impairment import Impairment, UdpProxy
from test_full_lap import route_source, route_targets, target_ahead
from test_network_lobby import LobbyPeer


class RacePeer(LobbyPeer):
    def __init__(self, *arguments):
        super().__init__(*arguments)
        self.ticks = TraceReader(Path(str(self.trace) + ".simulation.csv"))
        self.held = set()
        self.previous_step = None
        self.previous_forward = None

    def keys(self, wanted):
        arguments = []
        for key in sorted(self.held - wanted):
            arguments.extend(("keyup", key))
        for key in sorted(wanted - self.held):
            arguments.extend(("keydown", key))
        if arguments:
            self.command("xdotool", *arguments)
        self.held = wanted

    def drive(self, points):
        rows = self.ticks.rows()
        if not rows:
            return
        row = rows[-1]
        step = int(row["step"])
        if step == self.previous_step:
            return
        if row["completed_laps"] == "1" or row["ai_controlled"] == "1":
            self.keys(set())
            return
        require(row["motion_boundary"] == "0", f"Driver left physical coverage: {self.directory.name}, point{row['next_point']}")
        position = tuple(int(row[name]) / 65536 for name in ("body_x", "body_z"))
        forward = tuple(int(row[name]) / 65536 for name in ("body_forward_x", "body_forward_z"))
        target = target_ahead(points, int(row["next_point"]), position, 12)
        dx, dz = target[0] - position[0], target[1] - position[1]
        error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
        yaw = 0 if self.previous_forward is None else math.atan2(
            self.previous_forward[0] * forward[1] - self.previous_forward[1] * forward[0],
            self.previous_forward[0] * forward[0] + self.previous_forward[1] * forward[1]) / (step - self.previous_step)
        steering = error - 8 * yaw
        desired_speed = max(.42, .8 - abs(error) * .8)
        speed = int(row["speed"]) / 65536
        wanted = {"Up"} if speed < desired_speed else {"Down"} if speed > desired_speed + .1 else set()
        if steering > .025:
            wanted.add("Left")
        elif steering < -.025:
            wanted.add("Right")
        self.keys(wanted)
        self.previous_step, self.previous_forward = step, forward


def connect(host, client, impairment=None):
    host.enter()
    client.enter()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    host.test_port = port
    host.host_setup("HOST", port)
    proxy = UdpProxy(("127.0.0.1", port), impairment) if impairment else None
    client.join_setup("CLIENT", proxy.address[1] if proxy else port)
    for peer in (host, client):
        peer.wait(phase="lobby", players=2)
    return proxy


def start(host, client):
    for peer in (host, client):
        peer.waiting_room()
    wait_for(lambda: all(all(player.endswith(":1") for player in peer.current()["roster"].strip("/").split("/")) for peer in (host, client)), "Both drivers committed in the waiting room")
    host.command("xdotool", "key", "Return")
    for peer in (host, client):
        wait_for(lambda: (row := peer.game()) and row["screen"] == "scene" and row["game_mode"] == "3" and row, "Real Linkup scene", timeout=30)


def pause_checks(host, client):
    host.command("xdotool", "key", "Escape")
    for peer in (host, client):
        peer.wait(paused=1)
        peer.wait_game(scene_active=0)
    frozen = [peer.game() for peer in (host, client)]
    network_times = [int(peer.current()["time_ms"]) for peer in (host, client)]
    time.sleep(.3)
    for peer, before, network_time in zip((host, client), frozen, network_times):
        after = peer.game()
        require(all(after[field] == before[field] for field in ("simulation_steps", "body_x", "body_z", "lap_time_ms")), "Global pause advanced a car or race clock")
        require(int(peer.current()["time_ms"]) > network_time, "Pause stopped network service")
    host.command("xdotool", "key", "Escape")
    for peer in (host, client):
        peer.wait(paused=0)
        peer.wait_game(scene_active=1)
    client.command("xdotool", "windowminimize", client.window)
    for peer in (host, client):
        peer.wait(paused=1)
    client.command("xdotool", "windowmap", client.window)
    client.command("xdotool", "windowactivate", "--sync", client.window)
    for peer in (host, client):
        peer.wait(paused=0)


def main():
    peers = []
    proxy = None
    report = {"status": "failed", "input_transport": "ordinary-keyboard-wall-time", "source_full_parity": False}
    started = time.monotonic()
    try:
        host = RacePeer("host", *sys.argv[1:3]); peers.append(host)
        client = RacePeer("client", *sys.argv[1:3]); peers.append(client)
        proxy = connect(host, client, Impairment(seed=1414480707, jitter_ms=0, duplicate=0, reorder=0))
        host.one_lap_options()
        start(host, client)
        generation = host.current()["race_generation"]
        require(host.game()["network_actor"] != client.game()["network_actor"], "Shared actor IDs must distinguish local input owners")
        require(all(peer.game()["network_actors"] == "2" and peer.game()["rival_count"] == "1" for peer in peers), "Linkup must instantiate exactly the committed two-car fleet")
        require(all(int(peer.current()["load_service_calls"]) > 0 for peer in peers), "Scene loading must pump the real transport")
        pause_checks(host, client)
        for peer in peers:
            wait_for(lambda: peer.game()["start_held"] == "0", "Shared grid release", timeout=18)
            peer.wait(phase="racing")
        client.keys({"Up"})
        wait_for(lambda: client.ticks.rows() and int(client.ticks.rows()[-1]["throttle"]) > 0, "Client-owned pedal")
        require(host.ticks.rows()[-1]["throttle"] == "0", "Remote pedal changed host-owned controls")
        wait_for(lambda: int(host.current()["controls_consumed"]) > 0 and int(host.current()["state_consumed"]) > 0, "Remote controls and state reach the physical fleet")
        points = route_targets(sys.argv[2], 0)
        _, source = route_source(sys.argv[2], 0)
        deadline = time.monotonic() + 330
        next_report = time.monotonic()
        while time.monotonic() < deadline:
            for peer in peers:
                peer.drive(points)
            if all(peer.current()["phase"] == "results" for peer in peers):
                break
            if time.monotonic() >= next_report:
                print("Linkup driving: " + ", ".join(f"{peer.directory.name} point{peer.game()['next_point']}/{len(points)} lap{peer.game()['completed_laps']}" for peer in peers), flush=True)
                next_report = time.monotonic() + 15
            time.sleep(.005)
        else:
            raise AssertionError("Normal wall-time drivers did not finish within330 seconds")
        for peer in peers:
            peer.keys(set())
            peer.wait_game(session_state=13, race_classified=1, race_result_count=2)
            require(int(peer.game()["race_result_ms"]) > 0 and peer.game()["race_result_ms"] == peer.game()["race_total_ms"], "Results must retain the local owner-authored time")
            require(peer.game()["rival_voices"] == "0", "Results must stop spatial remote motors")
            history = peer.game_reader.rows()
            require(any(int(row["rival_voices"]) > 0 for row in history), "Physical remote cars must use the real spatial engine audio path")
            require(int(peer.current()["state_link_sent"]) > 0 and int(peer.current()["state_consumed"]) > 0, "Both owners must publish and consume actual state")
            peer.capture("network-results.png")
        require(host.current()["finish_order"] == client.current()["finish_order"], "Peers disagree on host-sequenced owner-authored finish order/time")
        require(host.game()["network_finish_order"] == client.game()["network_finish_order"], "Rendered result consumers disagree on reliable finish records")
        impairment = proxy.statistics()
        require(impairment["dropped"] > 0 and impairment["overflow"] == 0 and impairment["oversized"] == 0, "The bounded100msRTT/2%loss fixture must actually drop traffic without overflowing")
        report.update(source=source, finish_order=host.current()["finish_order"], wall_seconds=time.monotonic() - started,
                      states=[peer.current() for peer in peers], impairment=impairment)
        host.command("xdotool", "key", "Return")
        host.wait(phase="idle", players=0)
        client.wait(phase="failed", players=0)
        for peer in peers:
            peer.reopen()
        host.host_setup("HOST", host.test_port)
        client.join_setup("CLIENT", proxy.address[1])
        host.one_lap_options()
        start(host, client)
        require(host.current()["race_generation"] != generation and all(peer.game()["completed_laps"] == "0" for peer in peers), "New session must recreate an empty race with a new generation")
        host.process.kill()
        host.process.wait(timeout=3)
        client.wait(phase="failed", players=0)
        client.wait_game(screen="network_linkup", scene_active=0, rival_count=0)
        require(client.game()["music_track"] == "2", "Host loss must clear cars and restore frontend music")
        report["status"] = "passed"
        print("Native Linkup load/grid, owner controls/state, wall-time two-human finish, pause/focus, results/session close, fresh Start and host-loss cleanup passed")
    finally:
        Path("network-race-summary.json").write_text(json.dumps(report, indent=2) + "\n")
        for peer in reversed(peers):
            peer.close()
        if proxy:
            proxy.close()


if __name__ == "__main__":
    main()
