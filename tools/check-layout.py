#!/usr/bin/env python3
"""Reject ROM images that violate the PK-88 memory map."""

from pathlib import Path
import re
import sys


def load_ihx(path: Path) -> dict[int, int]:
    image: dict[int, int] = {}
    upper = 0
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw.startswith(":"):
            raise ValueError(f"{path}:{number}: invalid Intel HEX record")
        record = bytes.fromhex(raw[1:])
        if sum(record) & 0xFF:
            raise ValueError(f"{path}:{number}: checksum mismatch")
        count = record[0]
        address = (record[1] << 8) | record[2]
        kind = record[3]
        data = record[4 : 4 + count]
        if kind == 0:
            for offset, byte in enumerate(data):
                image[upper + address + offset] = byte
        elif kind == 4:
            upper = int.from_bytes(data, "big") << 16
        elif kind == 1:
            break
    return image


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"layout check failed: {message}")


def load_version(path: Path) -> bytes:
    match = re.search(r'(?m)^#define WFDIGI_VERSION "(.*)"\s*$', path.read_text())
    require(match is not None, "WFDIGI_VERSION define missing")
    value = match.group(1).encode("ascii").decode("unicode_escape").encode("ascii")
    require(value, "WFDIGI_VERSION is empty")
    return value


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: check-layout.py firmware.ihx firmware.map version.h")
    version = load_version(Path(sys.argv[3]))
    image = load_ihx(Path(sys.argv[1]))
    require(image, "ROM image is empty")
    require(max(image) < 0x8000, "ROM overlaps RAM at 0x8000")
    require([image.get(i) for i in range(4)] == [0xF3, 0x31, 0x00, 0x00],
            "reset must begin DI; LD SP,0")

    vectors = []
    for address in range(0x100, 0x110, 2):
        target = image.get(address, 0xFF) | (image.get(address + 1, 0xFF) << 8)
        vectors.append(target)
    require(all(0x120 <= target < 0x8000 for target in vectors),
            "all eight IM2 vectors must address ROM stubs")
    require(len(set(vectors)) == 8, "IM2 vectors must be distinct")

    pclk = 4_915_200
    terminal_tc = pclk // (2 * 9600 * 16) - 2
    radio_tc = pclk // (2 * 1200 * 32) - 2
    require(terminal_tc == 14, "9600 8N1 time constant changed")
    require(radio_tc == 62, "1200 baud DPLL time constant changed")

    rom = bytes(image.get(i, 0xFF) for i in range(max(image) + 1))
    map_text = Path(sys.argv[2]).read_text()
    require(b"\xED\x5E" in rom, "IM 2 instruction not found")
    require(b"\xED\x4D" in rom, "RETI instruction not found")
    require(b"\xDB\xF8" in rom, "watchdog pet (IN A,0xF8) not found")
    require(b"Whiskey Fox Digi - version " in rom, "boot banner missing from ROM")
    require(version + b"\x00" in rom, "WFDIGI_VERSION string missing from ROM")
    version_sym = re.search(r"(?m)^\s*([0-9A-Fa-f]{8})\s+_wfdigi_version\b", map_text)
    require(version_sym is not None, "wfdigi_version is not linked")
    require(int(version_sym.group(1), 16) < 0x8000, "wfdigi_version is not in ROM")
    require(b"Copyright 2026 - Kenneth Finnegan\r\n" in rom, "copyright line missing from ROM")
    require(b"Cold boot...\r\n" in rom, "cold boot line missing from ROM")
    require(b"N0CALL" in rom, "default MYCALL missing from ROM")
    require(re.search(r"00008000.*_g_config|_g_config.*00008000", map_text, re.IGNORECASE),
            "g_config is not fixed at 0x8000")
    # ld l,#tc ; ld a,#12  — the WR12 writes emitted for the two generators.
    require(bytes((0x2E, terminal_tc, 0x3E, 0x0C)) in rom,
            "terminal WR12 is not programmed for 9600 baud")
    require(bytes((0x2E, radio_tc, 0x3E, 0x0C)) in rom,
            "radio WR12 is not programmed for the 1200 baud DPLL clock")

    print(f"ROM layout: PASS ({max(image) + 1} bytes address span)")


if __name__ == "__main__":
    main()
