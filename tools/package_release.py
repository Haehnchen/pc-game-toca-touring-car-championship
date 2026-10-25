#!/usr/bin/env python3
"""Build a stripped Linux or Windows ZIP from a TOCA distribution build."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
GARAGE_ASSETS = frozenset(f"ui/{profile}/garage/gar{variant}-car{car}.webp"
                         for profile in ("original", "mega")
                         for variant in range(1, 5) for car in (range(10) if variant == 1 else range(7, 10)))
STARTUP_ASSETS = frozenset({"ui/original/title.webp", "ui/original/cpyright.webp", "ui/mega/title-wide.webp", "ui/mega/cpyright.webp"})
HOST_GLIBC = re.compile(
    r"^(?:ld-linux[^/]*|ld64\.so\.\d+|"
    r"lib(?:c|m|mvec|pthread|dl|rt|resolv|util|anl|BrokenLocale|"
    r"thread_db|nss_[^/]+)\.so(?:\.\d+)*)$"
)
HOST_GRAPHICS = re.compile(r"^lib(?:vulkan|GL|EGL|GLX|GLdispatch|drm|gbm|nvidia|cuda)[^/]*\.so(?:\.\d+)*$")
WINDOWS_SYSTEM_DLLS = {
    "advapi32", "avrt", "bcrypt", "cfgmgr32", "comctl32", "combase", "comdlg32",
    "crypt32", "d3d9", "d3d11", "d3d12", "d3dcompiler_47", "dbghelp", "dinput8",
    "dnsapi", "dwmapi", "dxgi", "gdi32", "hid", "imm32", "iphlpapi", "kernel32",
    "ksuser", "mf", "mfplat", "mfreadwrite", "mfuuid", "mmdevapi", "msacm32", "msimg32",
    "msvcrt", "msvcp_win", "netapi32", "normaliz", "ntdll", "ole32", "oleacc", "oleaut32",
    "powrprof", "profapi", "propsys", "psapi", "rpcrt4", "secur32", "setupapi", "shcore", "shell32",
    "shlwapi", "ucrtbase", "urlmon", "user32", "userenv", "usp10", "uxtheme", "version", "winhttp",
    "wininet", "winmm", "winmmbase", "winnsi", "winscard", "winspool", "wintrust", "wlanapi", "ws2_32",
    "wsock32", "wtsapi32", "xinput1_4", "xinput9_1_0",
}


class PackagingError(RuntimeError):
    """A distribution input cannot produce a verified portable package."""


def require(condition: object, message: str) -> None:
    if not condition:
        raise PackagingError(message)


def run(command: list[str], library_directories: list[str] | None = None) -> str:
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("LD_", "DYLD_"))}
    if library_directories:
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(library_directories)
    result = subprocess.run(command, capture_output=True, text=True,
                            env={**environment, "LC_ALL": "C"}, check=False)
    require(result.returncode == 0,
            f"{command[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def cache_values(build: Path) -> dict[str, str]:
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if match := re.fullmatch(r"([^/#][^:]*):[^=]+=(.*)", line):
            values[match[1]] = match[2]
    return values


def regular_file(path: Path) -> Path:
    require(path.is_file() and not path.is_symlink(), f"missing regular input file: {path}")
    return path


def tree_files(root: Path) -> list[Path]:
    require(root.is_dir() and not root.is_symlink(), f"missing input directory: {root}")
    files = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        require(not path.is_symlink(), f"symlinks are not runtime assets: {path}")
        require(not any(char in relative for char in "\n\r\\"), f"unsafe filename: {path}")
        if path.is_dir():
            continue
        files.append(regular_file(path))
    return files


def parse_ldd(output: str) -> dict[str, Path]:
    dependencies: dict[str, Path] = {}
    address = r"\(0x[0-9a-fA-F]+\)"
    for raw in output.splitlines():
        line = raw.strip()
        if not line or re.fullmatch(rf"(?:linux-vdso|linux-gate)[^\s/]*\s+{address}", line):
            continue
        require("not found" not in line, f"unresolved runtime dependency: {line}")
        if match := re.fullmatch(rf"([^\s/]+)\s+=>\s+(/.+?)\s+{address}", line):
            name, source = match[1], Path(match[2])
        elif match := re.fullmatch(rf"(/.+?)\s+{address}", line):
            source = Path(match[1])
            name = source.name
        else:
            raise PackagingError(f"unrecognized ldd output: {raw!r}")
        require(dependencies.get(name, source).resolve() == source.resolve(),
                f"conflicting runtime dependency: {name}")
        dependencies[name] = source
    require(dependencies, "ldd found no dynamic runtime dependencies")
    return dependencies


def runtime_assets(root: Path) -> list[tuple[str, Path]]:
    """Select built runtime formats only, excluding source art and provenance."""
    selected = []
    for path in tree_files(root):
        relative = path.relative_to(root).as_posix()
        parts = PurePosixPath(relative).parts
        if (re.fullmatch(r"music/track0[2-6]\.ogg", relative)
                or relative in STARTUP_ASSETS
                or (parts[:2] == ("ui", "mega") and (len(parts) == 3 or (len(parts) == 4 and parts[2] in {"hud", "menu-icons", "loading"}))
                    and path.suffix == ".webp")
                or relative in GARAGE_ASSETS
                or (relative.startswith("models/mega/") and path.suffix in {".glb", ".webp"})):
            selected.append((relative, path))
    return selected


def bundled_inputs(directory: Path) -> dict[str, Path]:
    """Collect the unpacked BFP files."""
    return {path.relative_to(directory).as_posix(): path for path in tree_files(directory)
            if path.suffix == ".bfp"}


def copy_inputs(files: list[tuple[str, Path]], destination: Path) -> None:
    for name, source in files:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o644)


def converted_audio_inputs(directory: Path) -> list[tuple[str, Path]]:
    """Collect unpacked Vorbis files and sound banks."""
    return [(path.relative_to(directory).as_posix(), path) for path in tree_files(directory)
            if re.fullmatch(r"(?:data/[^/]+\.bfp|fe/sounds/[^/]+\.ogg|speech/(?:[^/]+/)*[^/]+\.ogg)",
                            path.relative_to(directory).as_posix())]


def release_inputs(build: Path) -> tuple[dict, dict[str, str]]:
    values = cache_values(build)
    metadata = json.loads((build / "toca-package.json").read_text(encoding="utf-8"))
    host, target = platform.system(), metadata.get("system")
    require((target in {"Linux", "Windows"} and target == host) or (target == "Windows" and host == "Linux"),
            "release ZIP packaging requires a native Linux or Windows build or a Windows cross-build on Linux")
    regular_file(Path(metadata["executable"]))
    return metadata, values


def linux_runtime(executable: Path, stage: Path, strip: str,
                  library_directories: list[str] | None = None) -> None:
    dependencies = parse_ldd(run(["ldd", str(executable)], library_directories))
    libraries = {name: source for name, source in dependencies.items() if not HOST_GLIBC.fullmatch(name)}
    for name in libraries:
        require(not HOST_GRAPHICS.fullmatch(name),
                f"host graphics runtime must not be bundled: {name}; build SDL with dynamic driver loading")
    target = stage / "toca"
    shutil.copyfile(executable, target)
    target.chmod(0o755)
    for name, source in sorted(libraries.items()):
        destination = stage / "lib" / name
        destination.parent.mkdir(exist_ok=True)
        shutil.copyfile(source.resolve(strict=True), destination)
        destination.chmod(0o755)
    for path in [target, *(stage / "lib" / name for name in sorted(libraries))]:
        run([strip, "--strip-unneeded", str(path)])
    # Inspect the complete application's closure without discovery paths. Libraries
    # loaded through dlopen (the host's display/audio/graphics drivers) stay external.
    for name, resolved in parse_ldd(run(["ldd", str(target)])).items():
        if HOST_GLIBC.fullmatch(name):
            continue
        require(name in libraries and resolved.resolve() == (stage / "lib" / name).resolve(),
                f"runtime dependency escapes the package: {name} -> {resolved}")


def parse_imports(output: str) -> list[str]:
    names = re.findall(r"^[ \t]*DLL Name:[ \t]*(.*?)[ \t]*$", output, re.MULTILINE)
    require(all(name and not any(char in name for char in "/\\:\0") and name.lower().endswith(".dll")
                for name in names), "objdump reported an invalid DLL import name")
    return names


def find_dll(name: str, directories: list[Path]) -> Path | None:
    for directory in directories:
        if directory.is_dir():
            for candidate in directory.iterdir():
                if candidate.name.lower() == name.lower() and candidate.is_file():
                    return candidate
    return None


def windows_system_dll(name: str) -> bool:
    lower = name.lower()
    if lower.startswith(("api-ms-win-", "ext-ms-win-")) or lower.removesuffix(".dll") in WINDOWS_SYSTEM_DLLS:
        return True
    windows = os.environ.get("SystemRoot") or os.environ.get("WINDIR")
    return bool(windows and find_dll(name, [Path(windows) / "System32"]))


def windows_runtime(executable: Path, stage: Path, strip: str, objdump: str,
                    library_directories: list[str] | None = None) -> None:
    """Bundle the recursive PE import closure beside the executable."""
    target = stage / "toca.exe"
    shutil.copyfile(executable, target)
    target.chmod(0o755)
    search = [executable.parent, *(Path(path) for path in library_directories or []),
              *(Path(path) for path in os.environ.get("PATH", "").split(os.pathsep) if path)]
    libraries: dict[str, Path] = {}
    staged = [target]
    pending = [executable]
    while pending:
        source = pending.pop()
        for name in parse_imports(run([objdump, "-p", str(source)])):
            if windows_system_dll(name):
                continue
            dependency = find_dll(name, [source.parent, *search])
            require(dependency is not None, f"missing DLL {name!r} imported by {source}")
            dependency = regular_file(dependency).resolve(strict=True)
            key = name.lower()
            if key in libraries:
                require(libraries[key] == dependency, f"conflicting paths for DLL {name!r}")
                continue
            libraries[key] = dependency
            destination = stage / name
            shutil.copyfile(dependency, destination)
            destination.chmod(0o755)
            staged.append(destination)
            pending.append(dependency)
    for path in staged:
        run([strip, "--strip-unneeded", str(path)])


def windows_launchers(stage: Path) -> None:
    for driver in ("vulkan", "direct3d12"):
        script = ("@echo off\r\n"
                  "setlocal\r\n"
                  f'"%~dp0toca.exe" --gpu-driver {driver} %*\r\n'
                  "exit /b %errorlevel%\r\n")
        (stage / f"toca-{driver}.bat").write_bytes(script.encode("ascii"))
    with (stage / "README.md").open("ab") as readme:
        readme.write(b"\n\n## Windows graphics launchers\n\n"
                     b"Run `toca.exe` to use Vulkan by default on Windows.\n"
                     b"Select a backend with `toca.exe --gpu-driver direct3d12` or\n"
                     b"`toca.exe --gpu-driver vulkan`. The command-line option overrides\n"
                     b"the `SDL_GPU_DRIVER` environment variable, which overrides the default.\n"
                     b"Double-click `toca-vulkan.bat` to use Vulkan, or\n"
                     b"`toca-direct3d12.bat` to use Direct3D 12. If graphics appear\n"
                     b"incorrect, try the other launcher. Both accept the game's\n"
                     b"usual command-line arguments.\n")


def zip_tree(stage: Path, output: Path) -> None:
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9,
                         allowZip64=True, strict_timestamps=False) as archive:
        for path in tree_files(stage):
            archive.write(path, path.relative_to(stage).as_posix())


def package(build_dir: Path, output_dir: Path | None = None) -> Path:
    build = build_dir.resolve()
    metadata, values = release_inputs(build)
    output_dir = (output_dir or ROOT / "dist").resolve()
    require(not (output_dir == build or output_dir.is_relative_to(build) or build.is_relative_to(output_dir)),
            "build and output directories must be separate")
    require(not output_dir.is_relative_to(ROOT / "references")
            and not output_dir.is_relative_to(ROOT / "assets"),
            "output directory must be outside read-only source and reference assets")
    original_data_directory = Path(metadata["original_data_directory"]).resolve()
    originals = sorted(bundled_inputs(original_data_directory).items())
    audio_directory = Path(metadata.get("audio_directory", build / "generated/audio")).resolve()
    audio = converted_audio_inputs(audio_directory)
    assets = runtime_assets(Path(metadata["asset_directory"]))
    shader_root = Path(metadata["shader_directory"]).resolve()
    shaders = []
    for filename in metadata["shader_files"]:
        shader = regular_file(Path(filename))
        require(shader.resolve().is_relative_to(shader_root), f"shader lies outside the build shader directory: {shader}")
        shaders.append((shader.relative_to(shader_root).as_posix(), shader))
    architecture = {"amd64": "x86_64", "aarch64": "arm64"}.get(metadata["processor"].lower(), metadata["processor"].lower())
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"toca-{metadata['system']}-{architecture}.zip"
    with tempfile.TemporaryDirectory(prefix=".toca-package-", dir=output_dir) as temporary:
        workspace = Path(temporary)
        stage = workspace / "payload"
        stage.mkdir()
        strip = metadata.get("strip") or values["CMAKE_STRIP"]
        if metadata["system"] == "Windows":
            windows_runtime(Path(metadata["executable"]), stage, strip,
                            metadata.get("objdump") or values["CMAKE_OBJDUMP"],
                            metadata.get("library_directories", []))
        else:
            linux_runtime(Path(metadata["executable"]), stage, strip,
                          metadata.get("library_directories", []))
        copy_inputs(originals, stage / "game-data")
        copy_inputs(audio, stage / "game-data")
        copy_inputs(assets, stage / "assets")
        copy_inputs(shaders, stage / "shaders")
        shutil.copyfile(regular_file(ROOT / "README.md"), stage / "README.md")
        if metadata["system"] == "Windows":
            windows_launchers(stage)
        archive = workspace / output.name
        zip_tree(stage, archive)
        archive.replace(output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/dist")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    try:
        print(package(args.build_dir, args.output_dir))
    except (PackagingError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
