"""Find, read and print the smoke test's report line ("REPORT {json}") from the cube's serial output."""
from __future__ import annotations

import json
from typing import Mapping, Optional

REPORT_PREFIX = b"REPORT "
REPORT_VERSION = 1
NAME_WIDTH = 18


def find_report(buffer: bytes) -> Optional[dict]:
    """The first complete report line in `buffer`, or None if it has not fully arrived yet."""
    start = buffer.find(REPORT_PREFIX)
    if start < 0:
        return None
    end = buffer.find(b"\n", start)
    if end < 0:
        return None
    report = json.loads(buffer[start + len(REPORT_PREFIX):end])
    if report.get("v") != REPORT_VERSION:
        raise ValueError(f"report version {report.get('v')} is not supported (expected {REPORT_VERSION})")
    return report


def exit_code(report: Mapping) -> int:
    return 1 if any(c.get("status") == "fail" for c in report.get("checks", [])) else 0


def format_report(report: Mapping) -> str:
    return "\n".join([*_summary(report), "", *_checks(report), "", *_i2c(report), "", *_bench(report)]) + "\n"


def _summary(report: Mapping) -> list:
    counts = {s: sum(1 for c in report.get("checks", []) if c.get("status") == s) for s in ("pass", "warn", "fail")}
    seconds = report.get("ms", 0) / 1000
    return [f"CoreS3 smoke test: {counts['pass']} pass, {counts['warn']} warn, {counts['fail']} fail ({seconds:.1f} s)"]


def _checks(report: Mapping) -> list:
    return [f"{c.get('status', '?').upper():<4}  {c.get('name', ''):<{NAME_WIDTH}} {c.get('detail', '')}"
            for c in report.get("checks", [])]


def _i2c(report: Mapping) -> list:
    addresses = " ".join(f"0x{a:02X}" for a in sorted(report.get("i2c", [])))
    return [f"I2C addresses: {addresses or 'none'}"]


def _bench(report: Mapping) -> list:
    b, e = report.get("bench", {}), report.get("e01", {})
    return [
        "Memory (MB/s, 1 MB = 10^6 bytes)",
        f"  {'SRAM read':<{NAME_WIDTH}}{b.get('sram_read', 0):.1f}",
        f"  {'PSRAM read':<{NAME_WIDTH}}{b.get('psram_read', 0):.1f} (1 core), {b.get('psram_read_2core', 0):.1f} (2 cores)",
        f"  {'PSRAM write':<{NAME_WIDTH}}{b.get('psram_write', 0):.1f}",
        f"  {'Flash read':<{NAME_WIDTH}}{b.get('flash_read', 0):.1f}",
        "",
        f"E01 decode ceiling at {e.get('bandwidth', 0):.1f} MB/s (every token reads every weight once)",
        f"  {'3.3M INT8 model':<{NAME_WIDTH}}{e.get('int8_3m3_tok_s', 0):.1f} tok/s",
        f"  {'260K FP32 model':<{NAME_WIDTH}}{e.get('fp32_260k_tok_s', 0):.1f} tok/s",
    ]
