"""Seeded, bounded UDP impairment below ENet, local to one test process.

Each proxy is a single client/server link. No machine-wide network settings are
changed. The seed determines decisions in datagram arrival order; scheduling of
the native processes is deliberately real wall time, not a replay clock.
"""

from dataclasses import asdict, dataclass
import heapq
import random
import select
import socket
import threading
import time


@dataclass(frozen=True)
class Impairment:
    seed: int
    delay_ms: float = 50
    loss: float = .02
    jitter_ms: float = 20
    duplicate: float = .01
    reorder: float = .01

    def __post_init__(self):
        if self.delay_ms < 0 or self.jitter_ms < 0:
            raise ValueError("Negative UDP proxy delay")
        if any(not 0 <= value <= 1 for value in (self.loss, self.duplicate, self.reorder)):
            raise ValueError("UDP proxy probability outside 0..1")


class UdpProxy:
    max_pending = 2048
    max_datagram = 4096

    def __init__(self, server, impairment):
        self.server = (socket.gethostbyname(server[0]), int(server[1]))
        self.impairment = impairment
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.setblocking(False)
        self.address = self.socket.getsockname()
        self.client = None
        self.random = random.Random(impairment.seed)
        self.pending = []
        self.serial = 0
        self.stopped = threading.Event()
        self.lock = threading.Lock()
        self.error = None
        self.blocked = False
        self.counts = dict(received=0, forwarded=0, dropped=0, duplicated=0,
                           reordered=0, overflow=0, oversized=0, foreign=0,
                           peak_pending=0, bytes_forwarded=0)
        self.thread = threading.Thread(target=self._run, name=f"udp-proxy-{self.address[1]}", daemon=True)
        self.thread.start()

    def _queue(self, data, destination, now, delay):
        if len(self.pending) >= self.max_pending:
            self.counts["overflow"] += 1
            return
        self.serial += 1
        heapq.heappush(self.pending, (now + delay, self.serial, data, destination))
        self.counts["peak_pending"] = max(self.counts["peak_pending"], len(self.pending))

    def _receive(self, data, sender, now):
        self.counts["received"] += 1
        if len(data) > self.max_datagram:
            self.counts["oversized"] += 1
            return
        if sender == self.server:
            destination = self.client
        elif self.client is None or sender == self.client:
            self.client = sender
            destination = self.server
        else:
            self.counts["foreign"] += 1
            return
        if destination is None:
            self.counts["foreign"] += 1
            return
        profile = self.impairment
        if self.blocked or self.random.random() < profile.loss:
            self.counts["dropped"] += 1
            return
        delay = max(0, profile.delay_ms + self.random.uniform(-profile.jitter_ms, profile.jitter_ms)) / 1000
        if self.random.random() < profile.reorder:
            delay += .04
            self.counts["reordered"] += 1
        self._queue(data, destination, now, delay)
        if self.random.random() < profile.duplicate:
            self.counts["duplicated"] += 1
            self._queue(data, destination, now, delay + .001)

    def _run(self):
        try:
            while not self.stopped.is_set():
                readable, _, _ = select.select([self.socket], [], [], .005)
                with self.lock:
                    if readable:
                        # Bound work per turn independently of offered traffic.
                        for _ in range(256):
                            try:
                                data, sender = self.socket.recvfrom(65536)
                            except BlockingIOError:
                                break
                            self._receive(data, sender, time.monotonic())
                    now = time.monotonic()
                    while self.pending and self.pending[0][0] <= now:
                        _, _, data, destination = heapq.heappop(self.pending)
                        if self.blocked:
                            self.counts["dropped"] += 1
                            continue
                        try:
                            self.socket.sendto(data, destination)
                        except BlockingIOError:
                            self.counts["dropped"] += 1
                        else:
                            self.counts["forwarded"] += 1
                            self.counts["bytes_forwarded"] += len(data)
        except Exception as error:
            self.error = error

    def check(self):
        if self.error is not None:
            raise RuntimeError("UDP impairment worker failed") from self.error
        if not self.thread.is_alive() and not self.stopped.is_set():
            raise RuntimeError("UDP impairment worker stopped unexpectedly")

    def statistics(self):
        self.check()
        with self.lock:
            return dict(profile=asdict(self.impairment), **self.counts, pending=len(self.pending))

    def blackhole(self):
        with self.lock:
            self.blocked = True

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=1)
        if self.thread.is_alive():
            raise RuntimeError("UDP impairment worker did not stop")
        self.socket.close()
        self.check()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
