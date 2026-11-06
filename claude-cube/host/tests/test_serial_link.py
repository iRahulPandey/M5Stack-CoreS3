from __future__ import annotations

import plistlib
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import pytest

from claudecube import serial_link
from claudecube.serial_link import PortFinder, espressif_ports, find_port

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
