"""Two ordinary Linkup menus with independent displays, input and offline audio."""

from pathlib import Path
import socket
import subprocess
import sys

from native_support import require, wait_for
from test_network_connect import Peer


class LobbyPeer(Peer):
    def command(self, *arguments):
        return subprocess.run(list(map(str, arguments)), env=self.environment, check=True,
                              capture_output=True, text=True, timeout=3).stdout.strip()

    def game(self):
        require(self.process.poll() is None, "Game exited while using Linkup")
        rows = self.game_reader.rows()
        return rows[-1] if rows else {}

    def wait_game(self, **values):
        return wait_for(lambda: (row := self.game()) and all(row.get(key) == str(value) for key, value in values.items()) and row,
                        f"Linkup menu {values}", timeout=40 if values.get("screen") == "scene" else 10)

    def key(self, key, **values):
        cues = int(self.game()["frontend_cues"])
        self.command("xdotool", "key", key)
        return wait_for(lambda: (row := self.game()) and int(row["frontend_cues"]) > cues
                        and all(row.get(k) == str(v) for k, v in values.items()) and row,
                        f"Linkup key {key}: {values}", timeout=10)

    def text(self, text):
        cues = int(self.game()["frontend_cues"])
        self.command("xdotool", "type", "--delay", "20", text)
        frame = int(self.game()["frame"])
        wait_for(lambda: (row := self.game()) and int(row["frontend_cues"]) > cues and int(row["frame"]) > frame,
                 "Linkup complete text edit")

    def enter(self):
        with (self.directory / "openbox.log").open("wb") as log:
            self.runtime.launch(["openbox", "--sm-disable"], self.environment, log)
        wait_for(lambda: "_NET_SUPPORTING_WM_CHECK(WINDOW)" in self.command("xprop", "-root", "_NET_SUPPORTING_WM_CHECK"), "Private lobby window manager")
        self.wait_game(screen="title")
        self.window = self.command("xdotool", "search", "--onlyvisible", "--pid", self.process.pid,
                                   "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        self.command("xdotool", "windowactivate", "--sync", self.window)
        self.wait_game(focused=1)
        self.command("xdotool", "key", "Return")
        self.wait_game(screen="language")
        self.key("Return", screen="main_menu")
        self.key("Return", screen="race_menu")
        for selection in range(1, 4):
            self.key("Down", selection=selection)
        self.key("Return", screen="network_linkup", selection=0)

    def host_setup(self, name, port):
        self.key("Return", selection=0)
        self.text(str(port))
        self.key("Down", selection=1)
        self.key("Return", screen="name_entry")
        self.text(name)
        self.key("Return", screen="network_options", selection=4)
        self.wait(phase="lobby", players=1)

    def join_setup(self, name, port):
        self.key("Down", selection=1)
        self.key("Return", selection=0)
        self.text(f"127.0.0.1:{port}")
        self.key("Down", selection=1)
        self.key("Return", screen="name_entry")
        self.text(name)
        self.key("Return", screen="car_selection")
        self.wait(phase="lobby")

    def one_lap_options(self):
        self.wait_game(screen="network_options", selection=4)
        for selection in (3, 2, 1, 0):
            self.key("Up", selection=selection)
        while self.game()["laps"] != "1":
            self.key("Left")
        self.key("Down", selection=1)
        while self.game()["weather"] != "1":
            self.key("Right")
        self.key("Down", selection=2)
        if not getattr(self, "test_boost_disabled", False):
            self.key("Left")
            self.test_boost_disabled = True
        self.key("Down", selection=3)
        self.key("Down", selection=4)

    def waiting_room(self):
        if self.game()["screen"] == "network_options":
            self.key("Return", screen="car_selection")
        self.key("Return")
        if self.current()["hosting"] == "1":
            self.wait_game(screen="track_selection")
            self.key("Return", screen="network_linkup", selection=0)
        else:
            self.wait_game(screen="network_linkup", selection=0)
        self.wait_ready()

    def wait_ready(self):
        return wait_for(lambda: (row := self.current()) and member_ready(row, row["local_player"]),
                        "Entering the waiting room commits the local driver", timeout=10)

    def reopen(self):
        wait_for(lambda: self.game()["screen"] in ("main_menu", "network_linkup"),
                 "Session exit returns to a usable frontend", timeout=10)
        if self.game()["screen"] == "main_menu":
            self.key("Return", screen="race_menu", selection=3)
            self.key("Return", screen="network_linkup", selection=0)
        else:
            self.wait_game(screen="network_linkup")
            if self.current()["phase"] == "failed":
                self.key("Escape", screen="network_linkup", selection=0)

    def capture(self, name):
        self.command("import", "-silent", "-window", self.window, str(self.directory / name))


def member_ready(row, player):
    return any((fields := member.split(":"))[0] == str(player) and fields[-1] == "1"
               for member in row.get("roster", "").strip("/").split("/") if member)


def main():
    peers = []
    try:
        host = LobbyPeer("host", *sys.argv[1:3]); peers.append(host)
        client = LobbyPeer("client", *sys.argv[1:3]); peers.append(client)
        host.enter(); client.enter()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        host.host_setup("HOST", port)
        host.capture("linkup-options.png")
        host.one_lap_options()
        host.key("Return", screen="car_selection")
        host.capture("linkup-garage.png")
        host.key("Return", screen="track_selection")
        host.capture("linkup-track.png")
        host.key("Return", screen="network_linkup", selection=0)
        host.wait_ready()
        client.join_setup("CLIENT", port)
        host.wait(phase="lobby", players=2)
        require(member_ready(host.current(), host.current()["local_player"]), "Joining must preserve the host's waiting-room commitment")
        client.key("Right", screen="car_selection")
        client.key("Down", screen="car_selection")
        client.key("Return", screen="network_linkup", selection=0)
        client.wait_ready()
        wait_for(lambda: ":CLIENT:1:1:2:1/" in host.current().get("roster", ""), "Garage and automatic waiting-room commitment")
        client.capture("linkup-client-waiting.png")
        host.capture("linkup-host-waiting.png")
        client.key("Escape", screen="car_selection")
        wait_for(lambda: ":CLIENT:1:1:2:0/" in host.current().get("roster", ""), "Back withdraws only that driver's commitment")
        require(member_ready(host.current(), host.current()["local_player"]), "A client's Back must preserve the host's commitment")
        client.key("Return", screen="network_linkup")
        client.wait_ready()
        host.key("Escape", screen="track_selection")
        host.key("Right")
        host.key("Return", screen="network_linkup")
        host.wait_ready()
        client.wait(track=1)
        require(member_ready(client.current(), client.current()["local_player"]), "Host track selection must not strand a committed client")
        for peer in peers:
            require(peer.game()["music_track"] == "2" and peer.game()["music_playing"] == "1", "Original selection route must retain CD02")
            require(int(peer.game()["frontend_cues"]) >= 15, "Linkup must use ordinary menu sound cues")
        # Original Back chain reaches the session-name editor. A changed host
        # name closes/recreates its session, including waiting for port release.
        host.key("Escape", screen="track_selection")
        host.key("Escape", screen="car_selection")
        host.key("Escape", screen="network_options", selection=4)
        host.key("Escape", screen="name_entry")
        host.text("REHOST")
        host.key("Return", screen="network_options", selection=4)
        host.wait(phase="lobby", players=1)
        client.wait(phase="failed", players=0)
        client.reopen()
        client.join_setup("CLIENT", port)
        host.waiting_room()
        client.waiting_room()
        host.wait(players=2)
        # Starting excludes a driver still choosing a car, as in 46731c/466c33.
        client.key("Escape", screen="car_selection")
        wait_for(lambda: not member_ready(host.current(), client.current()["local_player"]), "Withdraw before source subset start")
        host.command("xdotool", "key", "Return")
        client.wait(phase="failed", players=0)
        require(client.current()["error"] == "Race started without you", "Excluded selector must receive the original start outcome")
        client.wait_game(screen="network_linkup")
        client.capture("linkup-started-without-you.png")
        host.wait_game(screen="scene")
        require(host.game()["network_actors"] == "1", "Only the committed human should enter this race")
        client.key("Escape", screen="network_linkup", selection=0)
        client.key("Escape", screen="race_menu", selection=3)
        client.key("Escape", screen="main_menu")
        print("Original Linkup options, name, garage/gearbox, track, waiting room, Back, commitment and subset Start passed")
    finally:
        for peer in reversed(peers): peer.close()


if __name__ == "__main__":
    main()
