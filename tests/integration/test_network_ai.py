"""Ordinary Linkup host with seven source AI cars and normal leave cleanup."""

import json
from pathlib import Path
import socket
import sys
import time

from native_support import require, wait_for
from test_network_race import RacePeer


def actors(row):
    return {int(fields[0]): {"active_human": fields[1] == "1", "finished": fields[2] == "1",
                            "position": tuple(map(int, fields[3:6])), "speed": int(fields[6])}
            for item in row["network_actor_states"].strip("/").split("/") if item
            for fields in (item.split(":"),)}


def main():
    peer = None
    report = {"status": "failed", "input_transport": "ordinary-keyboard-wall-time"}
    started = time.monotonic()
    try:
        peer = RacePeer("host-ai", *sys.argv[1:3])
        peer.enter()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        peer.host_setup("HOST AI", port)
        for selection in (3, 2, 1):
            peer.key("Up", selection=selection)
        peer.key("Right")
        peer.key("Down", selection=2)
        peer.key("Down", selection=3)
        peer.key("Right")
        peer.key("Down", selection=4)
        peer.waiting_room()
        peer.wait(weather=1, ai=1, car_boost=1)
        peer.command("xdotool", "key", "Return")
        wait_for(lambda: (row := peer.game()) and row["screen"] == "scene" and row["network_actors"] == "8" and row,
                 "Eight physical Linkup actors from the AI lobby option", timeout=25)
        initial = actors(peer.game())
        require(set(initial) == set(range(8)) and initial[0]["active_human"] and all(not initial[actor]["active_human"] for actor in range(1, 8)),
                "One human and seven AI slots must retain distinct shared actor identities")
        require(peer.game()["rival_count"] == "7" and peer.game()["game_mode"] == "3" and peer.game()["ai_controlled"] == "0",
                "Linkup AI must fill eight cars while preserving local human input ownership")
        wait_for(lambda: peer.game()["start_held"] == "0", "Source grid release with AI", timeout=15)
        peer.wait(phase="racing", actors=8, active_humans=1)
        peer.keys({"Up"})
        wait_for(lambda: int(peer.game()["rival_moving"]) == 7, "All seven actual AI bodies move", timeout=8)
        wait_for(lambda: int(peer.game()["rival_voices"]) > 0 and int(peer.game()["rival_volume"]) > 0, "Spatial AI motor audio", timeout=5)
        # Distant bodies can acquire speed before their next source position
        # update. Wait for one coherent snapshot proving both facts for all AI.
        moving = wait_for(lambda: (row := peer.game()) and (current := actors(row))
                          and all(current[actor]["position"] != initial[actor]["position"] and current[actor]["speed"] > 655 for actor in range(1, 8)) and row,
                          "Every AI physical XYZ pose and speed advance", timeout=8)
        require(moving["rival_boundaries"] == "0", "AI smoke must remain on supported track coverage")
        require(int(peer.current()["state_publications"]) >= 8, "Host must author state for its human and every AI actor")
        peer.capture("network-ai.png")
        peer.keys(set())
        peer.command("xdotool", "key", "Escape")
        peer.wait_game(pause_active=1, pause_selection=0)
        peer.command("xdotool", "key", "Up")
        peer.wait_game(pause_selection=2)
        peer.command("xdotool", "key", "Return")
        peer.wait(phase="idle", players=0)
        peer.wait_game(screen="main_menu", scene_active=0, rival_count=0)
        require(peer.game()["music_track"] == "2" and peer.game()["music_playing"] == "1", "End Race must clear the fleet and restore frontend music")
        report.update(status="passed", wall_seconds=time.monotonic() - started, moving=moving,
                      final=peer.game(), network=peer.current())
        print("Native Linkup optional AI: eight physical actors, seven moving AI cars, spatial audio and normal End Race cleanup passed")
    finally:
        Path("network-ai-summary.json").write_text(json.dumps(report, indent=2) + "\n")
        if peer:
            peer.close()


if __name__ == "__main__":
    main()
