#!/usr/bin/env python3
"""Run one native check with private storage and, when needed, an owned X server."""

import argparse
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


class Runtime:
    def __init__(self, directory):
        self.directory = directory
        self.groups = []

    def launch(self, command, env, output, **kwargs):
        process = subprocess.Popen(command, env=env, cwd=self.directory / "work",
                                   stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True, **kwargs)
        self.groups.append(process)
        return process

    def cleanup(self):
        # A reaped group leader can leave children behind; signal the whole owned group.
        for process in self.groups:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 2
        for process in self.groups:
            try:
                process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
        for process in self.groups:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 2
        for process in self.groups:
            try:
                process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass


def private_environment(directory, overrides=()):
    # Preserve toolchain and explicit Vulkan selection, never desktop/session/audio routing.
    permitted = {"PATH", "LANG", "LANGUAGE", "TZ", "LD_LIBRARY_PATH", "LD_PRELOAD",
                 "VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_LAYER_PATH", "LIBGL_DRIVERS_PATH",
                 "MESA_LOADER_DRIVER_OVERRIDE", "GALLIUM_DRIVER", "LLVM_PROFILE_FILE", "LP_NUM_THREADS"}
    environment = {key: value for key, value in os.environ.items()
                   if key in permitted or key.startswith("LC_")}
    for override in overrides:
        key, separator, value = override.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
            raise RuntimeError(f"Invalid environment entry: {override}")
        if key in {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "SESSION_MANAGER"}:
            raise RuntimeError(f"The isolated runtime owns {key}")
        environment[key] = value
    # CTest runs six cases concurrently. Mesa's per-process default otherwise
    # gives every software renderer all CPU cores, starving concurrent checks.
    environment.setdefault("LP_NUM_THREADS", "2")
    environment["HOME"] = str(directory / "home")
    for kind in ("DATA", "CONFIG", "CACHE", "STATE", "RUNTIME"):
        environment[f"XDG_{kind}_HOME" if kind != "RUNTIME" else "XDG_RUNTIME_DIR"] = str(directory / kind.lower())
    for key in ("TMPDIR", "TMP", "TEMP"):
        environment[key] = str(directory / "tmp")
    environment.update(SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy",
                       DBUS_SESSION_BUS_ADDRESS=f"unix:path={directory}/runtime/no-session-bus",
                       PULSE_SERVER=f"unix:{directory}/runtime/no-pulse-server",
                       PIPEWIRE_REMOTE=str(directory / "runtime/no-pipewire-server"),
                       ASAN_OPTIONS="detect_leaks=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    return environment


def start_display(runtime, environment, screen):
    for name in ("Xvfb", "xdpyinfo"):
        if shutil.which(name, path=environment.get("PATH")) is None:
            raise RuntimeError(f"{name} is required for display checks")
    read_fd, write_fd = os.pipe()
    deadline = time.monotonic() + 10
    try:
        with (runtime.directory / "xvfb.log").open("wb") as log:
            server = runtime.launch(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                                     screen, "-nolisten", "tcp"], environment, log,
                                    pass_fds=(write_fd,))
        os.close(write_fd)
        write_fd = None
        if not select.select([read_fd], [], [], max(0, deadline - time.monotonic()))[0]:
            raise RuntimeError("Xvfb display allocation exceeded its 10-second startup deadline")
        number = os.read(read_fd, 128).decode().strip()
        if server.poll() is not None or not number.isdecimal():
            raise RuntimeError("Xvfb failed to allocate a private display; see xvfb.log")
        environment["DISPLAY"] = f":{number}"
        # Readiness probes have their own groups and bounded lifetimes too.
        with (runtime.directory / "display-ready.log").open("wb") as log:
            while time.monotonic() < deadline:
                probe = runtime.launch(["xdpyinfo"], environment, log)
                try:
                    result = probe.wait(timeout=min(1, max(0, deadline - time.monotonic())))
                except subprocess.TimeoutExpired:
                    os.killpg(probe.pid, signal.SIGKILL)
                    probe.wait(timeout=1)
                    result = 1
                runtime.groups.remove(probe)
                if result == 0:
                    return
                if server.poll() is not None:
                    break
                time.sleep(0.05)
        raise RuntimeError("Xvfb did not become ready within its 10-second startup deadline")
    finally:
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)


def interrupted(signum, _frame):
    raise RuntimeError(f"Isolated check interrupted by signal {signum}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--display", action="store_true")
    parser.add_argument("--screen", default="1280x960x24")
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.timeout <= 0:
        parser.error("a command and a positive --timeout are required")
    if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*x24", args.screen):
        parser.error("--screen must be WIDTHxHEIGHTx24")
    if sys.platform != "linux":
        parser.error("the isolated runtime currently requires Linux")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^A-Za-z0-9_.-]", "_", args.name)
    directory = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=args.artifacts.resolve()))
    for child in ("home", "data", "config", "cache", "state", "runtime", "tmp", "work"):
        (directory / child).mkdir(mode=0o700)
    runtime = Runtime(directory)
    print(f"Artifacts: {directory}", flush=True)
    result = 1
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    try:
        environment = private_environment(directory, args.env)
        if args.display:
            start_display(runtime, environment, args.screen)
        (directory / "case.json").write_text(json.dumps({"command": command, "timeout": args.timeout,
                                                        "display": environment.get("DISPLAY")}, indent=2) + "\n")
        with (directory / "application.log").open("wb") as log:
            application = runtime.launch(command, environment, log)
        try:
            result = application.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f"Application exceeded its {args.timeout:g}-second deadline", file=sys.stderr)
            result = 124
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
    finally:
        # A second signal must not interrupt bounded cleanup.
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, signal.SIG_IGN)
        runtime.cleanup()
        log = directory / "application.log"
        if log.exists():
            sys.stdout.write(log.read_text(errors="replace"))
    return result if result >= 0 else 128 - result


if __name__ == "__main__":
    sys.exit(main())
