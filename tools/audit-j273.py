#!/usr/bin/env python3
"""Verify the executable produced by REPENTOGON Launcher for J273."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
from struct import unpack_from

SHA256 = "ceb598b4e5e03dabbd2da6ea322a9aa6ffc66fcc377aeea4391f008403b7faa4"
HEADER = Path(__file__).resolve().parents[1] / "src" / "j273_build.h"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    digest = sha256(data).hexdigest()
    if digest != SHA256:
        parser.exit(1, f"Unsupported executable SHA-256 {digest}\n")

    native = HEADER.read_text()

    def constant(name):
        return int(re.search(fr"\b{name} = (0x[0-9a-f]+);", native)[1], 16)

    def signature(name):
        match = re.search(fr"\b{name}\[\] = \{{([^}}]+)\}}", native)
        return bytes(int(byte, 16) for byte in re.findall(r"0x[0-9a-f]{2}", match[1]))

    pe, = unpack_from("<I", data, 0x3C)
    machine, count, timestamp = unpack_from("<HHI", data, pe + 4)
    optional_size, = unpack_from("<H", data, pe + 20)
    base, = unpack_from("<I", data, pe + 24 + 28)
    image_size, = unpack_from("<I", data, pe + 24 + 56)
    if (machine, timestamp, image_size) != (constant("kMachine"), constant("kTimestamp"),
                                            constant("kImageSize")):
        parser.exit(1, "PE identity differs from the runtime guard\n")
    sections = [
        unpack_from("<IIII", data, pe + 24 + optional_size + index * 40 + 8)
        for index in range(count)
    ]
    for address, expected in (("kMainCalls", "kMainCallBytes"), ("kSleepClamp", "kSleepBytes"),
                              ("kSpinBranch", "kSpinBytes")):
        va = constant(address)
        rva = va - base
        pattern = signature(expected)
        for _, section_rva, size, offset in sections:
            if section_rva <= rva and rva + len(pattern) <= section_rva + size:
                start = offset + rva - section_rva
                if data[start:start + len(pattern)] != pattern:
                    parser.exit(1, f"{address} bytes differ at {va:#010x}\n")
                print(f"{address} {va:#010x} verified")
                break
        else:
            parser.exit(1, f"{address} is outside the executable file\n")
    print(f"Repentance+ 1.9.7.12.J273 verified ({digest})")


if __name__ == "__main__":
    main()
