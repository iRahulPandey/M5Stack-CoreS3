"""Write-only link to the cube's USB serial port, using only the standard library."""
from __future__ import annotations

import errno
import glob
import logging
import os
import plistlib
import subprocess
import sys
import termios
import tty
from typing import Iterator, List, Optional, Tuple
from xml.parsers.expat import ExpatError

log = logging.getLogger(__name__)

PORT_GLOB = "/dev/cu.usbmodem*"
PORT_ENV = "CLAUDE_CUBE_PORT"
ESPRESSIF_VID = 0x303A  # the ESP32-S3's own USB port, which is how the CoreS3 connects
IOREG_COMMAND = ["ioreg", "-a", "-r", "-c", "IOUSBHostDevice", "-l"]
IOREG_TIMEOUT_S = 5.0


def find_port() -> Optional[str]:
    """The cube's serial port: CLAUDE_CUBE_PORT if set, else the first port of an Espressif USB device."""
    return PortFinder().find()


class PortFinder:
    """find_port, asking the I/O Registry again only when the set of USB modem devices changes."""

    def __init__(self) -> None:
        self._devices: Optional[Tuple[str, ...]] = None
        self._port: Optional[str] = None

    def find(self) -> Optional[str]:
        explicit = os.environ.get(PORT_ENV)
        if explicit:
            return explicit if os.path.exists(explicit) else None
        devices = tuple(_modem_devices())
        if devices != self._devices:
            ports = espressif_ports() if devices else []  # nothing plugged in: no need to run ioreg
            self._devices, self._port = devices, (ports[0] if ports else None)
        return self._port


def espressif_ports() -> List[str]:
    """Serial ports that belong to USB devices with Espressif's vendor id, so other boards are left alone."""
    try:
        roots = plistlib.loads(_read_registry())
    except (OSError, subprocess.SubprocessError, plistlib.InvalidFileException, ExpatError, ValueError) as exc:
        log.warning("Cannot list USB devices with ioreg: %s", exc)
        return []
    nodes = roots if isinstance(roots, list) else [roots]
    return sorted({port for node in nodes for port in _callouts(node, None)})


def _callouts(node: object, vendor: object) -> Iterator[str]:
    """Walk one registry subtree, yielding serial ports whose nearest USB device is Espressif's."""
    if not isinstance(node, dict):
        return
    vendor = node.get("idVendor", vendor)
    port = node.get("IOCalloutDevice")
    if vendor == ESPRESSIF_VID and isinstance(port, str):
        yield port
    children = node.get("IORegistryEntryChildren")
    for child in children if isinstance(children, list) else []:
        yield from _callouts(child, vendor)


def _modem_devices() -> List[str]:
    return sorted(glob.glob(PORT_GLOB))


def _read_registry() -> bytes:
    return subprocess.run(IOREG_COMMAND, capture_output=True, check=True, timeout=IOREG_TIMEOUT_S).stdout


class SerialLink:
    """Opens the port lazily, survives unplugging and reflashing, and never blocks the caller."""

    def __init__(self) -> None:
        self._fd: Optional[int] = None
        self._port: Optional[str] = None
        self._finder = PortFinder()

    @property
    def connected(self) -> bool:
        return self._fd is not None

    def write(self, data: bytes) -> bool:
        if self._fd is None and not self._open():
            return False
        assert self._fd is not None
        try:
            self._drain()
            os.write(self._fd, data)
            return True
        except BlockingIOError:
            return False  # the cube is not draining its buffer; drop this frame
        except OSError as exc:
            log.warning("Serial write failed, closing port", extra={"port": self._port, "errno": exc.errno})
            self.close()
            return False

    def close(self) -> None:
        if self._fd is not None:
            try:
                os.close(self._fd)
            except OSError:
                pass
            log.info("Closed serial port %s", self._port)
        self._fd, self._port = None, None

    def _drain(self) -> None:
        """Discard anything the cube printed, so its log lines never pile up in the tty buffer."""
        assert self._fd is not None
        try:
            while os.read(self._fd, 4096):
                pass
        except BlockingIOError:
            pass

    def _open(self) -> bool:
        port = self._finder.find()
        if port is None:
            return False
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            if exc.errno not in (errno.EBUSY, errno.ENOENT, errno.ENXIO):
                log.warning("Cannot open %s: %s", port, exc)
            return False
        try:
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            attrs[2] = (attrs[2] | termios.CLOCAL) & ~termios.HUPCL  # keep DTR up on close: no reset
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        except termios.error as exc:
            log.warning("Cannot configure %s: %s", port, exc)
            os.close(fd)
            return False
        self._fd, self._port = fd, port
        log.info("Opened serial port %s", port)
        return True


if __name__ == "__main__":
    found = find_port()
    if found:
        print(found)
    sys.exit(0 if found else 1)
