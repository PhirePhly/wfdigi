#!/usr/bin/env python3
"""Reject ROM images that violate a supported WFDIGI platform layout."""

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


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, matching crc16_feed() in src/util.c."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def store_rom_checksum(bin_path: Path, image: dict[int, int]) -> int:
    blob = bytearray(bin_path.read_bytes())
    require(len(blob) == 0x8000, "ROM image is not 32768 bytes")
    require(max(image) < 0x7FFE, "firmware occupies the ROM checksum")
    for addr, value in image.items():
        require(blob[addr] == value, f"bin byte at 0x{addr:04X} does not match the HEX file")
    for addr in range(0x7FFE):
        if addr not in image:
            require(blob[addr] == 0xFF, f"gap at 0x{addr:04X} is not 0xFF")
    # "123456789" is the CRC-16/CCITT-FALSE check value.
    require(crc16(b"123456789") == 0x29B1, "CRC-16 test vector failed")
    crc = crc16(bytes(blob[:0x7FFE]))
    blob[0x7FFE] = crc & 0xFF
    blob[0x7FFF] = (crc >> 8) & 0xFF
    bin_path.write_bytes(blob)
    require(crc16(bytes(bin_path.read_bytes()[:0x7FFE])) == crc, "stored ROM checksum does not match")
    return crc


def main() -> None:
    if len(sys.argv) != 7 or sys.argv[1] != "--platform":
        raise SystemExit(
            "usage: check-layout.py --platform pk88|tnc2 "
            "firmware.ihx firmware.map version.h firmware.bin"
        )
    platform = sys.argv[2]
    require(platform in ("pk88", "tnc2"), f"unknown platform {platform}")
    version = load_version(Path(sys.argv[5]))
    print(f"Whiskey Fox Digi {platform} - version {version.decode('ascii')}")
    image = load_ihx(Path(sys.argv[3]))
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

    rom = bytes(image.get(i, 0xFF) for i in range(max(image) + 1))
    map_text = Path(sys.argv[4]).read_text()
    require(b"\xED\x5E" in rom, "IM 2 instruction not found")
    require(b"\xED\x4D" in rom, "RETI instruction not found")
    require(b"Whiskey Fox Digi - version " in rom, "boot banner missing from ROM")
    require(version + b"\x00" in rom, "WFDIGI_VERSION string missing from ROM")
    version_sym = re.search(r"(?m)^\s*([0-9A-Fa-f]{8})\s+_wfdigi_version\b", map_text)
    require(version_sym is not None, "wfdigi_version is not linked")
    require(int(version_sym.group(1), 16) < 0x8000, "wfdigi_version is not in ROM")
    require(b"Copyright 2026 - Kenneth Finnegan\r\n" in rom, "copyright line missing from ROM")
    require(b"ROM checksum: " in rom, "ROM checksum line missing from ROM")
    require(b"PASS\r\n" in rom and b"FAIL\r\n" in rom, "ROM checksum result missing from ROM")
    require(b"Cold boot...\r\n" in rom, "cold boot line missing from ROM")
    require(re.search(r"00008000.*_g_config|_g_config.*00008000", map_text, re.IGNORECASE),
            "g_config is not fixed at 0x8000")
    if platform == "pk88":
        pclk = 4_915_200
        terminal_tc = pclk // (2 * 9600 * 16) - 2
        radio_tc = pclk // (2 * 1200 * 32) - 2
        require(terminal_tc == 14, "9600 8N1 time constant changed")
        require(radio_tc == 62, "1200 baud DPLL time constant changed")
        require(b"\xDB\xF8" in rom, "watchdog pet (IN A,0xF8) not found")
        require(bytes((0x0C, terminal_tc)) in rom,
                "terminal WR12 is not programmed for 9600 baud")
        require(bytes((0x0C, radio_tc)) in rom,
                "radio WR12 is not programmed for the 1200 baud DPLL clock")
        require(b"APWFDP" in rom, "PK-88 TNCID APWFDP missing")
    else:
        require(b"\xDB\xF8" not in rom, "TNC-2 image contains the PK-88 watchdog strobe")
        for port in (0xDC, 0xDD, 0xDE, 0xDF):
            port_read = bytes((0xDB, port))
            port_write = bytes((0xD3, port))
            require(port_read in rom or port_write in rom,
                    f"TNC-2 SIO port 0x{port:02X} is not referenced")
        require(bytes((4, 0x20, 1, 0x00, 7, 0x7E)) in rom,
                "TNC-2 radio SIO setup missing")
        require(bytes((4, 0x44, 2, 0x00)) in rom,
                "TNC-2 terminal SIO setup missing")
        require(bytes((5, 0xEA, 1, 0x04)) in rom,
                "TNC-2 status-modified interrupt vector setup missing")
        require(b"APWFD2" in rom, "TNC-2 TNCID APWFD2 missing")

    crc = store_rom_checksum(Path(sys.argv[6]), image)
    print(f"ROM layout: PASS ({max(image) + 1} bytes address span, checksum {crc:04X})")


if __name__ == "__main__":
    main()
