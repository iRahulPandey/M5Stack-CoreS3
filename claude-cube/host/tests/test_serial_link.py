from __future__ import annotations

import json
import os
import plistlib
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Optional, Tuple

import pytest

from claudecube import serial_link
from claudecube.serial_link import RECONNECT_S, PortFinder, SerialLink, espressif_ports, find_port

APPLE_HUB = 0x05AC
ARDUINO = 0x2341
ESPRESSIF = 0x303A


def usb_device(vendor: int, callout: Optional[str] = None, children: Optional[list] = None) -> dict:
    """An IOUSBHostDevice node as `ioreg -a -l` prints it, with the serial port two levels down."""
    node: dict = {"IOObjectClass": "IOUSBHostDevice", "idVendor": vendor, "IORegistryEntryChildren": list(children or [])}
    if callout:
        serial = {"IOObjectClass": "IOSerialBSDClient", "IOCalloutDevice": callout}
        node["IORegistryEntryChildren"].append({"IOObjectClass": "AppleUSBACMData", "IORegistryEntryChildren": [serial]})
    return node


@dataclass
class FakeRegistry:
    roots: list = field(default_factory=list)
    reads: int = 0

    def read(self) -> bytes:
        self.reads += 1
        return plistlib.dumps(self.roots)


@pytest.fixture
def registry(monkeypatch: pytest.MonkeyPatch) -> FakeRegistry:
    """Stand in for `ioreg`, with two USB modem devices plugged in."""
    fake = FakeRegistry()
    monkeypatch.setattr(serial_link, "_read_registry", fake.read)
    monkeypatch.setattr(serial_link, "_modem_devices", lambda: ["/dev/cu.usbmodem101", "/dev/cu.usbmodem2101"])
    monkeypatch.delenv(serial_link.PORT_ENV, raising=False)
    return fake


class TestEspressifPorts:
    def test_should_pick_only_ports_of_espressif_devices(self, registry: FakeRegistry) -> None:
        registry.roots.append(usb_device(APPLE_HUB, children=[
            usb_device(ARDUINO, "/dev/cu.usbmodem2101"),
            usb_device(ESPRESSIF, "/dev/cu.usbmodem101"),
        ]))
        assert espressif_ports() == ["/dev/cu.usbmodem101"]

    def test_should_list_a_port_once_when_ioreg_repeats_a_nested_device(self, registry: FakeRegistry) -> None:
        cube = usb_device(ESPRESSIF, "/dev/cu.usbmodem101")
        registry.roots.extend([usb_device(APPLE_HUB, children=[cube]), cube])
        assert espressif_ports() == ["/dev/cu.usbmodem101"]

    def test_should_find_nothing_when_ioreg_fails(self, monkeypatch: pytest.MonkeyPatch) -> None:
        def fail() -> bytes:
            raise subprocess.CalledProcessError(1, ["ioreg"])

        monkeypatch.setattr(serial_link, "_read_registry", fail)
        assert espressif_ports() == []

    def test_should_find_nothing_in_unreadable_ioreg_output(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(serial_link, "_read_registry", lambda: b"not a plist")
        assert espressif_ports() == []


class TestFindPort:
    def test_should_ignore_a_usb_modem_from_another_vendor(self, registry: FakeRegistry) -> None:
        registry.roots.append(usb_device(ARDUINO, "/dev/cu.usbmodem101"))
        assert find_port() is None

    def test_should_use_the_override_when_it_exists(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        port = tmp_path / "tty.cube"
        port.touch()
        monkeypatch.setenv(serial_link.PORT_ENV, str(port))
        assert find_port() == str(port)

    def test_should_find_nothing_when_the_override_is_missing(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv(serial_link.PORT_ENV, str(tmp_path / "gone"))
        assert find_port() is None

    def test_should_skip_ioreg_when_no_usb_modem_is_plugged_in(self, registry: FakeRegistry, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(serial_link, "_modem_devices", lambda: [])
        assert find_port() is None
        assert registry.reads == 0


class TestPortFinder:
    def test_should_ask_ioreg_again_only_when_the_usb_modems_change(self, registry: FakeRegistry, monkeypatch: pytest.MonkeyPatch) -> None:
        registry.roots.append(usb_device(ESPRESSIF, "/dev/cu.usbmodem101"))
        finder = PortFinder()
        assert [finder.find(), finder.find()] == ["/dev/cu.usbmodem101"] * 2
        assert registry.reads == 1
        monkeypatch.setattr(serial_link, "_modem_devices", lambda: ["/dev/cu.usbmodem101"])
        finder.find()
        assert registry.reads == 2


class TestCommandLine:
    def test_should_print_the_port_for_flash_sh(self, tmp_path: Path) -> None:
        port = tmp_path / "tty.cube"
        port.touch()
        host = Path(__file__).resolve().parents[1]
        result = subprocess.run(
            ["/usr/bin/python3", "-m", "claudecube.serial_link"],
            cwd=host, env={serial_link.PORT_ENV: str(port)}, capture_output=True, text=True, check=False,
        )
        assert (result.returncode, result.stdout) == (0, f"{port}\n")


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now


@dataclass
class FixedFinder:
    port: Optional[str]
    finds: int = 0

    def find(self) -> Optional[str]:
        self.finds += 1
        return self.port


@pytest.fixture
def cube(monkeypatch: pytest.MonkeyPatch) -> Iterator[Tuple[int, int]]:
    """A pseudo-terminal stands in for the cube: the link writes to its slave side, the test reads the master."""
    master, slave = os.openpty()
    os.set_blocking(master, False)
    monkeypatch.setenv(serial_link.PORT_ENV, os.ttyname(slave))
    yield master, slave
    for fd in (master, slave):
        try:
            os.close(fd)
        except OSError:
            pass


def read_all(master: int) -> bytes:
    out = b""
    while True:
        try:
            chunk = os.read(master, 65536)
        except BlockingIOError:
            return out
        if not chunk:
            return out
        out += chunk


def frame(seq: int) -> bytes:
    return (json.dumps({"seq": seq, "pad": "x" * 280}) + "\n").encode("ascii")


class TestSerialLink:
    def test_should_write_a_frame_to_the_port(self, cube: Tuple[int, int]) -> None:
        master, _ = cube
        link = SerialLink()
        assert link.write(frame(1))
        assert link.connected
        assert read_all(master) == frame(1)
        link.close()
        assert not link.connected

    def test_should_discard_what_the_cube_prints(self, cube: Tuple[int, int]) -> None:
        master, slave = cube
        link = SerialLink()
        link.write(frame(1))
        os.write(master, b"I (1234) cube: boot log\n")
        link.write(frame(2))
        os.set_blocking(slave, False)
        with pytest.raises(BlockingIOError):
            os.read(slave, 100)
        link.close()

    def test_should_finish_a_cut_off_frame_before_sending_the_next(self, cube: Tuple[int, int]) -> None:
        master, _ = cube
        link = SerialLink()
        accepted = [link.write(frame(seq)) for seq in range(8)]
        assert False in accepted  # the pty's 1 KB buffer filled up part way through a frame
        out = read_all(master)
        assert link.write(frame(99))
        out += read_all(master)
        seqs = [json.loads(line)["seq"] for line in out.splitlines()]
        assert seqs == sorted(seqs)
        assert seqs[-1] == 99
        link.close()

    def test_should_close_the_port_when_the_cube_is_unplugged(self, cube: Tuple[int, int]) -> None:
        master, _ = cube
        link = SerialLink()
        assert link.write(frame(1))
        os.close(master)
        assert not link.write(frame(2))
        assert not link.connected

    def test_should_not_look_for_the_port_again_before_the_reconnect_delay(self) -> None:
        finder, clock = FixedFinder(None), Clock()
        link = SerialLink(finder=finder, clock=clock)
        assert [link.write(frame(seq)) for seq in range(3)] == [False] * 3
        assert finder.finds == 1
        clock.now = RECONNECT_S
        link.write(frame(4))
        assert finder.finds == 2

    @pytest.mark.parametrize("kind", ["missing", "directory", "regular file"])
    def test_should_drop_frames_while_the_port_cannot_be_used(self, tmp_path: Path, kind: str) -> None:
        port = tmp_path / "port"
        if kind == "directory":
            port.mkdir()
        elif kind == "regular file":
            port.touch()
        link = SerialLink(finder=FixedFinder(str(port)))
        assert not link.write(frame(1))
        assert not link.connected
