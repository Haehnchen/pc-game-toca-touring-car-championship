FROM ubuntu:26.04

RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        ca-certificates cmake g++ g++-mingw-w64-x86-64-posix git imagemagick \
        ninja-build pkg-config python3 unzip zstd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["python3", "tools/build-windows.py"]
