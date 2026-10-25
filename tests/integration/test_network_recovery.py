"""Short real loading failures/cancel and simultaneous pause ownership checks."""

import json
import os
from pathlib import Path
import shutil
import socket
import sys
import time

from native_support import require, wait_for
from test_network_lobby import member_ready
from test_network_race import RacePeer, connect, start


def clean_menu(peer, error):
    peer.wait(phase="failed", players=0, error=error)
    peer.wait_game(screen="network_linkup", scene_active=0, rival_count=0, loading_step=0,
                   scene_vertices=0, music_track=2, music_playing=1)


def end_race(host, client=None):
    host.command("xdotool", "key", "Escape")
    host.wait_game(pause_active=1, pause_selection=0)
    host.command("xdotool", "key", "Up")
    host.wait_game(pause_selection=2)
    host.command("xdotool", "key", "Return")
    host.wait(phase="idle", players=0)
    host.wait_game(screen="main_menu", scene_active=0, rival_count=0, music_track=2, music_playing=1)
    if client:
        clean_menu(client, "Host left the session")


def reconnect(host, client):
    for peer in (host, client):
        peer.reopen()
    host.host_setup("HOST", host.test_port)
    client.join_setup("CLIENT", host.test_port)
    for peer in (host, client):
        peer.wait(phase="lobby", players=2)
    host.one_lap_options()


def private_runtime(name, executable, data):
    runtime = Path(name).resolve()
    assets = runtime / "assets"
    assets.mkdir(parents=True)
    root = assets / "original-data"
    # Only private pathnames are removed/restored below; shared bytes are never
    # edited in place. Both the explicit data root and the executable's bundled
    # overlay must see the injected absence instead of healing each other.
    shutil.copytree(data, root, copy_function=os.link)
    target = runtime / executable.name
    os.link(executable, target)
    for source in (executable.parent / "assets").iterdir():
        if source.name != "original-data":
            (assets / source.name).symlink_to(source, target_is_directory=source.is_dir())
    for name in ("shaders", "lib", "_deps"):
        source = executable.parent / name
        if source.exists():
            (runtime / name).symlink_to(source, target_is_directory=source.is_dir())
    return target, root


def main():
    peers = []
    report = {"status": "failed"}
    started = time.monotonic()
    executable = Path(sys.argv[1]).resolve()
    reference = Path(sys.argv[2]).resolve()
    runtimes = [private_runtime(name, executable, reference) for name in ("host-assets", "client-assets")]
    roots = [root for _, root in runtimes]
    try:
        host = RacePeer("host", str(runtimes[0][0]), str(roots[0])); peers.append(host)
        client = RacePeer("client", str(runtimes[1][0]), str(roots[1])); peers.append(client)
        connect(host, client)
        host.one_lap_options()

        # Failure after scene construction, while creating real race audio.
        missing = roots[1] / "data/sounddry.bfp"
        missing.unlink()
        for peer in peers:
            peer.waiting_room()
        host.command("xdotool", "key", "Return")
        clean_menu(client, "Unable to load the race")
        host.wait_game(screen="scene")
        host.wait(active_humans=1)
        require("Linkup loading:" in (client.directory / "game.log").read_text(), "Resource failure did not use the guarded loading path")
        report["client_failure"] = client.game()
        client.capture("loading-resource-error.png")
        os.link(reference / "data/sounddry.bfp", missing)
        end_race(host)

        # Host fails during the first stage: all peers receive the typed reason.
        reconnect(host, client)
        for peer in peers:
            peer.waiting_room()
        missing = roots[0] / "data/donlkeep.bfp"
        missing.unlink()
        host.command("xdotool", "key", "Return")
        for peer in peers:
            clean_menu(peer, "Unable to load the race")
        report["host_failure"] = [peer.current() for peer in peers]
        os.link(reference / "data/donlkeep.bfp", missing)

        reconnect(host, client)
        start(host, client)
        host_id, client_id = (peer.current()["local_player"] for peer in peers)
        host.command("xdotool", "key", "Escape")
        for peer in peers:
            peer.wait(paused=1, pause_players=f"{host_id}/")
            peer.wait_game(scene_active=0)
        client.wait_game(pause_active=0)
        client.capture("remote-pause.png")
        frozen = [peer.game() for peer in peers]
        time.sleep(.2)
        require(all(all(peer.game()[field] == before[field] for field in ("simulation_steps", "body_x", "body_z", "lap_time_ms"))
                    for peer, before in zip(peers, frozen)), "A remote pause advanced simulation")
        client.command("xdotool", "key", "Escape")
        for peer in peers:
            peer.wait(paused=1, pause_players=f"{host_id}/{client_id}/")
        host.command("xdotool", "key", "Escape")
        for peer in peers:
            peer.wait(paused=1, pause_players=f"{client_id}/")
        host.wait_game(pause_active=0, scene_active=0)
        host.capture("remaining-pause-owner.png")
        client.command("xdotool", "key", "Escape")
        for peer in peers:
            peer.wait(paused=0, pause_players="")
            peer.wait_game(scene_active=1)
        client.command("xdotool", "windowminimize", client.window)
        for peer in peers:
            peer.wait(paused=1, pause_players=f"{client_id}/")
        client.command("xdotool", "windowmap", client.window)
        client.command("xdotool", "windowactivate", "--sync", client.window)
        for peer in peers:
            peer.wait(paused=0, pause_players="")
        report["pause_recovered"] = [peer.current() for peer in peers]
        end_race(host, client)

        # A real responsive ENet peer intentionally never acknowledges loading.
        # Session unit tests inject the short deadline; this native case checks
        # waiting names, keyboard cancellation and complete scene/audio cleanup.
        host.reopen()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        host.host_setup("HOST", port)
        fixture_log = host.directory / "waiting-peer.log"
        with fixture_log.open("wb") as log:
            fixture = host.runtime.launch([sys.argv[3], str(reference), f"127.0.0.1:{port}", "WAITING"], host.environment, log)
            host.wait(phase="lobby", players=2)
            wait_for(lambda: member_ready(host.current(), 2), "Responsive peer commits to the waiting room")
            host.waiting_room()
            host.command("xdotool", "key", "Return")
            host.wait_game(screen="scene_loading", loading_step=2)
            host.wait(phase="loading", loaded_players=f"{host.current()['local_player']}/")
            host.capture("waiting-for-player.png")
            host.command("xdotool", "key", "Escape")
            clean_menu(host, "Race loading cancelled")
            wait_for(lambda: "Race loading cancelled" in fixture_log.read_text(), "Reliable cancellation reaches the waiting peer")
            fixture.wait(timeout=3)
        report.update(status="passed", wall_seconds=time.monotonic() - started, cancellation=host.current())
        print("Native loading failures, waiting/cancellation, pause owners/focus and scene/audio cleanup passed")
    finally:
        Path("network-recovery-summary.json").write_text(json.dumps(report, indent=2) + "\n")
        for peer in reversed(peers):
            peer.close()


if __name__ == "__main__":
    main()
