from __future__ import annotations

import json

import pytest

from smokereport import exit_code, find_report, format_report

REPORT = {
    "v": 1,
    "ms": 14210,
    "checks": [
        {"name": "Chip", "status": "pass", "detail": "ESP32-S3 r2, 2 cores, 240 MHz, 16 MB flash"},
        {"name": "Camera GC0308", "status": "warn", "detail": "no ACK at 0x21"},
        {"name": "IMU BMI270", "status": "pass", "detail": "0x69, id 0x24 (expect 0x24)"},
        {"name": "Other I2C", "status": "info", "detail": "none"},
    ],
    "i2c": [35, 52, 54, 56, 64, 81, 88, 105],
    "bench": {"sram_read": 540.2, "psram_read": 28.4, "psram_read_2core": 31.0, "psram_write": 40.1, "flash_read": 26.9},
    "e01": {"bandwidth": 31.0, "int8_3m3_tok_s": 9.4, "fp32_260k_tok_s": 29.8},
}


def report_line(report: dict) -> bytes:
    return b"REPORT " + json.dumps(report).encode("ascii") + b"\n"


class TestFindReport:
    def test_should_find_the_report_among_other_serial_output(self) -> None:
        noise = b"ESP-ROM:esp32s3-20210327\r\nSNAP garbage\x00\xff\n"
        assert find_report(noise + report_line(REPORT) + b"trailing") == REPORT

    def test_should_wait_for_the_end_of_the_line(self) -> None:
        line = report_line(REPORT)
        assert find_report(line[:-20]) is None

    def test_should_return_none_without_a_report(self) -> None:
        assert find_report(b"booting...\n") is None

    def test_should_reject_an_unknown_report_version(self) -> None:
        with pytest.raises(ValueError, match="report version 2"):
            find_report(report_line({**REPORT, "v": 2}))


class TestFormatReport:
    def test_should_list_every_check_with_its_status(self) -> None:
        text = format_report(REPORT)
        assert "PASS  Chip" in text
        assert "WARN  Camera GC0308" in text
        assert "no ACK at 0x21" in text

    def test_should_show_bandwidth_and_the_e01_ceiling(self) -> None:
        text = format_report(REPORT)
        assert "PSRAM read" in text and "28.4" in text and "31.0" in text
        assert "9.4 tok/s" in text

    def test_should_list_i2c_addresses_in_hex(self) -> None:
        assert "0x23 0x34 0x36 0x38 0x40 0x51 0x58 0x69" in format_report(REPORT)

    def test_should_summarize_the_counts(self) -> None:
        assert "2 pass, 1 warn, 0 fail" in format_report(REPORT)


class TestExitCode:
    def test_should_succeed_when_nothing_failed(self) -> None:
        assert exit_code(REPORT) == 0

    def test_should_fail_when_any_check_failed(self) -> None:
        failing = {**REPORT, "checks": REPORT["checks"] + [{"name": "PSRAM", "status": "fail", "detail": "0 KB"}]}
        assert exit_code(failing) == 1
