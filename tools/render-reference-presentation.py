"""Observe the completed reference frame without releasing its game thread."""

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import zlib


def current_source_thread(listing, expected_pid):
    process = None
    selected = []
    for line in listing.splitlines():
        if line and not line[0].isspace():
            match = re.match(r"([0-9a-fA-F]{4,8})\s+(.*)$", line)
            if match:
                description = match[2]
                process = (int(match[1], 16), "(D)" in description and bool(re.search(r"(?:^|[\\/\s])tourcars\.exe$", description, re.I)))
        if "<==" in line:
            match = re.match(r"\s+([0-9a-fA-F]{4,8})\s+", line)
            if not match or process != (expected_pid, True):
                raise ValueError("Selected thread does not belong to the owned original process")
            selected.append(int(match[1], 16))
    if len(selected) != 1:
        raise ValueError("Expected exactly one selected original game thread")
    return selected[0]


def build_thread_helper(directory, runner, supplied=None):
    if supplied is not None:
        supplied = supplied.resolve()
        if not supplied.is_file():
            raise RuntimeError("The supplied reference thread helper does not exist")
        return supplied
    compiler, linker = shutil.which("clang"), shutil.which("ld")
    if not compiler or not linker:
        raise RuntimeError("Original capture needs Clang and GNU ld with i386pe, or --thread-helper")
    source = Path(__file__).with_name("reference-thread-control.c")
    object_file, executable = directory / "thread-control.o", directory / "thread-control.exe"
    commands = ([compiler, "--target=i686-w64-windows-gnu", "-ffreestanding", "-fno-stack-protector", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-c", str(source), "-o", str(object_file)],
                [linker, "-mi386pe", "--entry", "_entry", "--subsystem", "console", "--enable-stdcall-fixup", str(object_file),
                 str(runner / "lib/wine/i386-windows/kernel32.dll"), "-o", str(executable)])
    with (directory / "thread-helper-build.log").open("wb") as log:
        for command in commands:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    return executable


def capture_completed_window(debugger, environment, runner, helper, windows_pid, unix_pid, state, directory, capture_window):
    """Drain graphics workers while one explicitly suspended game thread stays fixed."""
    listing = debugger.command("info thread")
    thread = current_source_thread(listing, windows_pid)
    # The generic debugger conservatively invalidates unknown commands. This
    # query only observes its stopped thread list; it does not execute the game.
    debugger.confirm_pause()
    debugger.enable_process_memory(unix_pid)
    renderer = debugger.word(0x4b9b94)
    guards = ((0x4cd760 + state["actor"] * 0x53c, 0x53c), (renderer + 0x4300, 64),
              (renderer + 0x4240, 64), (0x4d383c, 4))
    before = [debugger.memory(address, size) for address, size in guards]
    helper_windows = "Z:" + str(helper).replace("/", "\\")

    def control(mode):
        result = subprocess.run([str(runner / "bin/wine"), helper_windows, mode, f"{windows_pid:x}", f"{thread:x}"],
                                env=environment, capture_output=True, timeout=15)
        if not 16 <= result.returncode <= 143:
            raise RuntimeError(f"Owned game thread {mode} failed: {result.returncode}; {result.stderr.decode(errors='replace')}")
        return result.returncode - 16

    suspended = False
    suspend_count_before = None
    try:
        previous_count = control("s")
        suspended = True
        suspend_count_before = previous_count
        if previous_count != 0:
            raise RuntimeError("Owned game thread already has an independent suspend owner")
        # A debugger stop freezes DXVK's command/submission workers too. Only
        # the explicit game-thread suspension survives detach; workers can now
        # finish the already submitted frame without another game update.
        debugger.command("detach")
        started = time.monotonic()
        deadline = min(debugger.deadline, started + 5)
        last, stable = None, 0
        output = directory / "original.png"
        while time.monotonic() < deadline:
            time.sleep(.1)
            capture_window(environment, output)
            checksum = zlib.crc32(output.read_bytes())
            stable = stable + 1 if checksum == last else 1
            last = checksum
            if stable >= 3 and time.monotonic() >= started + .5:
                break
        else:
            raise TimeoutError("Original window did not settle while its game thread was frozen")
        after = [os.pread(debugger.memory_fd, size, address) for address, size in guards]
        if before != after:
            raise RuntimeError("Original body/camera/projection/update counter changed during presentation")
        state["presentation_barrier"] = {"main_thread_frozen": True, "workers_released": True,
                                         "state_unchanged": True, "stable_window": True,
                                         "process_id": windows_pid, "thread_id": thread,
                                         "previous_suspend_count": previous_count,
                                         "guard_crc32": f"{zlib.crc32(b''.join(before)):08x}"}
    finally:
        try:
            if suspended:
                previous_count = control("r")
                if previous_count != suspend_count_before + 1:
                    raise RuntimeError("Original game-thread suspend ownership changed before cleanup")
        finally:
            debugger.close_memory()
