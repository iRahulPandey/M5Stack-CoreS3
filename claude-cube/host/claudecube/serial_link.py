"""Write-only link to the cube's USB serial port, using only the standard library."""
from __future__ import annotations

import errno
import glob
import logging
import os
import termios
import tty
from typing import Optional

log = logging.getLogger(__name__)

PORT_GLOB = "/dev/cu.usbmodem*"
PORT_ENV = "CLAUDE_CUBE_PORT"


def find_port() -> Optional[str]:
    explicit = os.environ.get(PORT_ENV)
    if explicit:
        return explicit if os.path.exists(explicit) else None
    ports = sorted(glob.glob(PORT_GLOB))
    return ports[0] if ports else None


class SerialLink:
    """Opens the port lazily, survives unplugging and reflashing, and never blocks the caller."""

    def __init__(self) -> None:
        self._fd: Optional[int] = None
        self._port: Optional[str] = None

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
        port = find_port()
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
