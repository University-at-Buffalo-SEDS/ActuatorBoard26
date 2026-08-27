#!/usr/bin/env python3
"""Upload a packaged LaunchCore full image over the board's UART4 recovery link."""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
from pathlib import Path

MAGIC = 0x5052434C
VERSION = 1
RESPONSE = 0x80
OP_INFO, OP_BEGIN, OP_DATA, OP_FINISH, OP_ABORT, OP_RESET = range(1, 7)
HEADER = struct.Struct("<IBBHII")
RESPONSE_PAYLOAD = struct.Struct("<II")
MAX_CHUNK = 256


def encode_frame(opcode: int, offset: int = 0, payload: bytes = b"") -> bytes:
    prefix = struct.pack("<IBBHI", MAGIC, VERSION, opcode, len(payload), offset)
    checksum = binascii.crc32(prefix)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return prefix + struct.pack("<I", checksum) + payload


def read_exact(port, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = port.read(length - len(data))
        if not chunk:
            raise TimeoutError("timed out waiting for LaunchCore recovery response")
        data.extend(chunk)
    return bytes(data)


def transact(port, opcode: int, offset: int = 0, payload: bytes = b"") -> tuple[int, int, int]:
    port.write(encode_frame(opcode, offset, payload))
    raw_header = read_exact(port, HEADER.size)
    magic, version, response_opcode, length, status, checksum = HEADER.unpack(raw_header)
    body = read_exact(port, length)
    calculated = binascii.crc32(raw_header[:12])
    calculated = binascii.crc32(body, calculated) & 0xFFFFFFFF
    if magic != MAGIC or version != VERSION or response_opcode != opcode | RESPONSE:
        raise RuntimeError("invalid LaunchCore recovery response header")
    if checksum != calculated or length != RESPONSE_PAYLOAD.size:
        raise RuntimeError("invalid LaunchCore recovery response CRC or length")
    expected_offset, slot_size = RESPONSE_PAYLOAD.unpack(body)
    return status, expected_offset, slot_size


def require_ok(result: tuple[int, int, int], operation: str) -> tuple[int, int]:
    status, expected_offset, slot_size = result
    if status != 0:
        raise RuntimeError(f"{operation} failed with status {status} at offset {expected_offset}")
    return expected_offset, slot_size


def upload(port, image: bytes) -> None:
    _, slot_size = require_ok(transact(port, OP_INFO), "info")
    if len(image) > slot_size:
        raise ValueError(f"image is {len(image)} bytes; recovery slot holds {slot_size}")
    require_ok(transact(port, OP_BEGIN, len(image)), "begin")
    for offset in range(0, len(image), MAX_CHUNK):
        chunk = image[offset : offset + MAX_CHUNK]
        expected, _ = require_ok(transact(port, OP_DATA, offset, chunk), "data")
        if expected != offset + len(chunk):
            raise RuntimeError(f"board acknowledged unexpected offset {expected}")
        print(f"\r{expected}/{len(image)} bytes", end="", flush=True)
    print()
    require_ok(transact(port, OP_FINISH), "finish")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("device", help="UART device connected to PC10/PC11")
    parser.add_argument("image", type=Path, help="packaged *.launchcore.bin image")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
        return 2
    image = args.image.read_bytes()
    try:
        with serial.Serial(args.device, args.baud, timeout=3, write_timeout=3) as port:
            upload(port, image)
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"recovery upload failed: {error}", file=sys.stderr)
        return 1
    print("image validated; board is rebooting")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
