"""Turn the cube's raw RGB565 screen buffer into a PNG, with only the standard library."""
from __future__ import annotations

import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
RGB_COLOR_TYPE = 2
BIT_DEPTH = 8
FILTER_NONE = b"\x00"


def rgb565_to_png(data: bytes, width: int, height: int) -> bytes:
    """`data` holds big-endian RGB565 pixels, row by row, as the M5GFX sprite buffer stores them."""
    expected = width * height * 2
    if len(data) != expected:
        raise ValueError(f"expected {expected} bytes for {width}x{height}, got {len(data)}")
    rows = bytearray()
    for y in range(height):
        rows += FILTER_NONE
        row = data[y * width * 2:(y + 1) * width * 2]
        for i in range(0, len(row), 2):
            rows += _expand((row[i] << 8) | row[i + 1])
    header = struct.pack(">IIBBBBB", width, height, BIT_DEPTH, RGB_COLOR_TYPE, 0, 0, 0)
    return PNG_SIGNATURE + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + _chunk(b"IEND", b"")


def _expand(value: int) -> bytes:
    """5/6/5 bits to 8 bits per channel, repeating the high bits so white stays 255."""
    r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
    return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))


def _chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
