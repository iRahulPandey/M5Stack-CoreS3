from __future__ import annotations

import os
import socket
import struct
import threading
import zlib
from pathlib import Path
from typing import Iterator, Tuple

import pytest

import snap

WIDTH, HEIGHT = 2, 1
PIXELS = bytes([0xF8, 0x00, 0x07, 0xE0])  # one red and one green RGB565 pixel


class FakeCube(threading.Thread):
    """Answers each "snap" command like the firmware: a SNAP header line, then the raw pixels."""

    def __init__(self, sock: socket.socket) -> None:
        super().__init__(daemon=True)
        self._sock = sock
        self.page = 0
        self.commands = b""

    def run(self) -> None:
        while True:
            data = self._sock.recv(4096)
            if not data:
                return
            self.commands += data
            for line in data.split(b"\n"):
                if line == b"next":
                    self.page = (self.page + 1) % len(snap.PAGE_NAMES)
                elif line == b"snap":
                    self._sock.sendall(b"log line before the header\nSNAP %d %d %d\n" % (WIDTH, HEIGHT, self.page) + PIXELS)


@pytest.fixture
def cube() -> Iterator[Tuple[int, FakeCube]]:
    host_end, cube_end = socket.socketpair()
    fake = FakeCube(cube_end)
    fake.start()
    yield host_end.fileno(), fake
    host_end.close()  # the fake cube's recv() then returns b"" and its thread ends
    fake.join(timeout=5)
    cube_end.close()


def png_pixels(png: bytes) -> bytes:
    """The raw RGB rows of a PNG written by rgb565_to_png (one IDAT chunk, filter byte per row)."""
    length = struct.unpack(">I", png[33:37])[0]
    return zlib.decompress(png[41:41 + length])


class TestCapture:
    def test_should_return_the_page_and_a_png_of_the_screen(self, cube: Tuple[int, FakeCube]) -> None:
        fd, fake = cube
        fake.page = 3
        page, png = snap.capture(fd, b"\nsnap\n")
        assert page == 3
        assert png_pixels(png) == b"\x00" + bytes([255, 0, 0, 0, 255, 0])

    def test_should_flip_through_every_page(self, cube: Tuple[int, FakeCube], tmp_path: Path) -> None:
        fd, fake = cube
        fake.page = 2
        saved = snap.capture_all(fd, tmp_path)
        assert sorted(p.name for p in saved) == sorted(f"snap-{name}.png" for name in snap.PAGE_NAMES)
        assert fake.commands.count(b"next") == len(snap.PAGE_NAMES) - 1

    def test_should_time_out_when_the_cube_does_not_answer(self, monkeypatch: pytest.MonkeyPatch) -> None:
        host_end, cube_end = socket.socketpair()
        with host_end, cube_end:
            monkeypatch.setattr(snap, "READ_TIMEOUT_S", 0.05)
            with pytest.raises(TimeoutError):
                snap.capture(host_end.fileno(), b"\nsnap\n")


class TestOpenRaw:
    def test_should_open_a_tty_in_raw_mode(self) -> None:
        master, slave = os.openpty()
        try:
            fd = snap.open_raw(os.ttyname(slave))
            os.close(fd)
        finally:
            os.close(master)
            os.close(slave)


class TestMain:
    @pytest.fixture
    def state(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
        monkeypatch.setattr(snap, "STATE_DIR", tmp_path / "state")
        monkeypatch.setattr(snap, "PAUSE_PATH", tmp_path / "state" / "paused")
        monkeypatch.setattr(snap, "DEFAULT_OUT", tmp_path / "state" / "snap.png")
        monkeypatch.setattr(snap, "BRIDGE_RELEASE_S", 0.0)
        return tmp_path / "state"

    def test_should_save_the_current_page_and_lift_the_pause(
        self, state: Path, cube: Tuple[int, FakeCube], monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture
    ) -> None:
        fd, _ = cube
        monkeypatch.setattr(snap, "find_port", lambda: "/dev/cu.usbmodem101")
        monkeypatch.setattr(snap, "open_raw", lambda port: os.dup(fd))
        assert snap.main([]) == 0
        assert (state / "snap.png").read_bytes().startswith(snap.rgb565_to_png(PIXELS, WIDTH, HEIGHT)[:8])
        assert not (state / "paused").exists()
        assert capsys.readouterr().out.strip() == str(state / "snap.png")

    def test_should_save_every_page_into_a_folder(self, state: Path, cube: Tuple[int, FakeCube], tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        fd, _ = cube
        monkeypatch.setattr(snap, "find_port", lambda: "/dev/cu.usbmodem101")
        monkeypatch.setattr(snap, "open_raw", lambda port: os.dup(fd))
        assert snap.main(["--all", str(tmp_path / "docs")]) == 0
        assert len(list((tmp_path / "docs").glob("snap-*.png"))) == len(snap.PAGE_NAMES)

    def test_should_fail_without_a_cube(self, state: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture) -> None:
        monkeypatch.setattr(snap, "find_port", lambda: None)
        assert snap.main([]) == 1
        assert "No cube found" in capsys.readouterr().err
