"""Real common-application connections, on independent private muted displays."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

from native_support import TraceReader, require, wait_for

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("isolated_network_runtime", ROOT / "tools/run-isolated.py")
isolated = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolated)


class Peer:
    def __init__(self, name, executable, data, *arguments):
        self.directory = Path(name).resolve()
        self.directory.mkdir()
        for child in ("home", "data", "config", "cache", "state", "runtime", "tmp", "work"):
            (self.directory / child).mkdir(mode=0o700)
        self.runtime = isolated.Runtime(self.directory)
        try:
            self.environment = isolated.private_environment(self.directory)
            isolated.start_display(self.runtime, self.environment, "800x600x24")
            self.trace = self.directory / "game.csv"
            self.reader = TraceReader(Path(str(self.trace) + ".network.csv"))
            self.game_reader = TraceReader(self.trace)
            self.log = (self.directory / "game.log").open("wb")
            self.process = self.runtime.launch(
                [executable, "--data-root", data, "--config", str(self.directory / "preferences.cfg"),
                 "--state-dir", str(self.directory / "saves"), "--resolution", "640x480", "--fullscreen", "off", "--vsync", "off",
                 "--fps-limit", "30", "--mute", "on", "--audio-offline", "--trace", str(self.trace), *arguments],
                self.environment, self.log)
        except Exception:
            self.runtime.cleanup()
            raise

    def current(self):
        require(self.process.poll() is None, f"Peer exited; see {self.directory / 'game.log'}")
        rows = self.reader.rows()
        return rows[-1] if rows else {}

    def wait(self, **values):
        return wait_for(lambda: (row := self.current()) and all(row.get(key) == str(value) for key, value in values.items()) and row,
                        f"{self.directory.name} network {values}", timeout=20)

    def close(self):
        self.runtime.cleanup()
        self.log.close()


def main():
    peers = []
    try:
        host = Peer("host", *sys.argv[1:3], "--network-host", "0", "--network-name", "HOST")
        peers.append(host)
        port = host.wait(phase="lobby", players=1)["port"]
        client = Peer("client", *sys.argv[1:3], "--network-join", f"127.0.0.1:{port}", "--network-name", "CLIENT")
        peers.append(client)
        client.wait(phase="lobby", players=2)
        host.wait(phase="lobby", players=2)
        require(host.current()["roster"] == client.current()["roster"], "Common applications received different rosters")
        require(host.current()["generation"] == client.current()["generation"], "Session generations differ")
        # With no window manager neither game has input focus: networking must
        # keep pumping through the ordinary inactive-frontend path.
        previous = int(client.current()["time_ms"])
        wait_for(lambda: int(client.current()["time_ms"]) > previous + 500, "Unfocused connection service")
        require(host.environment["DISPLAY"] != client.environment["DISPLAY"], "Peers must own separate Xvfb displays")
        for peer in peers:
            rows = peer.game_reader.rows()
            require(rows and rows[-1]["audio_active"] == "1" and "offline=1" in (peer.directory / "game.log").read_text(), "Network application must use real offline audio")
            require(int(peer.current()["peak_reliable"]) <= 65536 * 8, "Reliable queues exceeded peer bounds")
        # Abrupt process loss uses real monotonic timeout; no stepped simulation.
        host.process.kill()
        host.process.wait(timeout=3)
        row = client.wait(phase="failed", players=0)
        require(row["error"] in ("Connection timed out", "Host left the session"), "Host loss did not show a useful reason")
        Path("network-summary.json").write_text(json.dumps({"port": port, "host_loss": row,
            "displays": [peer.environment["DISPLAY"] for peer in peers]}, indent=2) + "\n")
        print("Common application host/join, matching roster, focus-independent service and hard host loss passed")
    finally:
        for peer in reversed(peers):
            peer.close()


if __name__ == "__main__":
    main()
