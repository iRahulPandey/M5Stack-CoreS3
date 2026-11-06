from __future__ import annotations

import struct
import zlib

import pytest

from claudecube.snapshot import rgb565_to_png

RED_565 = b"\xf8\x00"   # big-endian, as the M5GFX sprite buffer stores it
BLUE_565 = b"\x00\x1f"
WHITE_565 = b"\xff\xff"


def png_rows(png: bytes) -> tuple[int, int, bytes]:
    assert png.startswith(b"\x89PNG\r\n\x1a\n")
    width, height = struct.unpack(">II", png[16:24])
    idat_len = struct.unpack(">I", png[33:37])[0]
    assert png[37:41] == b"IDAT"
    return width, height, zlib.decompress(png[41:41 + idat_len])


class TestRgb565ToPng:
    def test_should_expand_each_pixel_to_full_range_rgb(self) -> None:
        width, height, raw = png_rows(rgb565_to_png(RED_565 + BLUE_565 + WHITE_565, 3, 1))
        assert (width, height) == (3, 1)
        assert raw == b"\x00" + bytes([255, 0, 0, 0, 0, 255, 255, 255, 255])

    def test_should_start_every_row_with_a_filter_byte(self) -> None:
        _, height, raw = png_rows(rgb565_to_png(RED_565 * 4, 2, 2))
        assert height == 2
        assert raw[0] == 0 and raw[7] == 0
        assert len(raw) == 2 * (1 + 2 * 3)

    def test_should_reject_a_short_buffer(self) -> None:
        with pytest.raises(ValueError, match="expected 8 bytes"):
            rgb565_to_png(RED_565, 2, 2)
