"""Read-only verification of the Repentance+ J510 core loop port anchors."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


J510_SHA256 = "114673659401da17e55bd44b2c9f1cb7d40ce5fd31e15690588661a8a7c60ff6"
J510_TIMESTAMP = 0x6AFE6324
J510_IMAGE_SIZE = 0x953000


class Image:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("missing PE signature")
        machine, count, stamp, _, _, optional_size = struct.unpack_from("<HHIIIH", data, pe + 4)
        optional = pe + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        if machine != 0x14C or magic != 0x10B:
            raise ValueError("expected Intel 32-bit PE")
        self.base = struct.unpack_from("<I", data, optional + 0x1C)[0]
        self.size = struct.unpack_from("<I", data, optional + 0x38)[0]
        self.timestamp = stamp
        self.sections = []
        for index in range(count):
            start = optional + optional_size + 40 * index
            name, virtual_size, rva, file_size, file_offset = struct.unpack_from("<8sIIII", data, start)
            self.sections.append((name.rstrip(b"\0"), rva, min(virtual_size, file_size), file_offset))

    def text(self, address, size):
        rva = address - self.base
        for name, start, length, offset in self.sections:
            if name == b".text" and start <= rva <= start + length - size:
                begin = offset + rva - start
                if begin + size <= len(self.data):
                    return self.data[begin:begin + size]
        raise ValueError(f"{address:#x} is not backed by .text")

    def expect(self, address, expected):
        if self.text(address, len(expected)) != expected:
            raise ValueError(f"unexpected bytes at {address:#x}")

    def call(self, address):
        opcode, displacement = struct.unpack("<Bi", self.text(address, 5))
        if opcode != 0xE8:
            raise ValueError(f"missing rel32 call at {address:#x}")
        target = address + 5 + displacement
        self.text(target, 1)
        return target


def value(instructions, offset):
    return struct.unpack_from("<I", instructions, offset)[0]


def inspect_j510(image):
    update_call, render_call, gc_guard, gc_call = 0x9465FC, 0x946601, 0x946606, 0x94660F
    wrapper = image.call(update_call)
    render = image.call(render_call)
    garbage_collect = image.call(gc_call)
    if (wrapper, render, garbage_collect) != (0x9C2950, 0x96AF30, 0x86F2D0):
        raise ValueError("main-loop call destinations differ from the audited build")
    guard = image.text(gc_guard, 9)
    if guard[:2] != b"\x83\x3d" or guard[6:] != b"\0\x74\x05":
        raise ValueError("GC guard no longer compares the game pointer and skips one call")
    game = value(guard, 2)
    image.expect(wrapper, bytes.fromhex(
        "68 34 40 c8 00 ff 15 98 aa b3 00 83 c4 04 83 38 00 74 06 ff 15 80 aa b3 00 32 c9"
    ))
    image.expect(wrapper + 0x20, bytes.fromhex("6a 01"))
    image.expect(wrapper + 0x27, b"\xc3")
    runtime_input = image.call(wrapper + 0x1B)
    manager_update = image.call(wrapper + 0x22)
    if (runtime_input, manager_update) != (0xA91A50, 0x96A650):
        raise ValueError("update wrapper input/update chain changed")

    lookup = image.text(manager_update + 0x1C, 16)
    if lookup[:2] != bytes.fromhex("8b 3d") or lookup[6:12] != bytes.fromhex("89 7c 24 14 8b 8f"):
        raise ValueError("update no longer loads the expected manager/counter layout")
    manager, counter = value(lookup, 2), value(lookup, 12)
    image.expect(manager_update + 0x2C, bytes.fromhex("8b c1 25 01 00 00 80"))
    image.expect(0x96A6B1, bytes.fromhex("89 87") + struct.pack("<I", counter))
    image.expect(0x96AF00, bytes.fromhex("ff 87") + struct.pack("<I", counter))
    image.expect(0x96AF19, bytes.fromhex("c2 04 00"))

    rollback = 0x96AB4F
    image.expect(rollback, bytes.fromhex("f6 87") + struct.pack("<I", counter) + bytes.fromhex("01 75 60 a1")
                 + struct.pack("<I", game) + bytes.fromhex("33 f6 8b 88 00 83 01 00 39 b1 64 12 00 00"))
    image.expect(0x96AB70, bytes.fromhex(
        "8b 81 5c 12 00 00 8b 14 b0 80 ba 75 01 00 00 00 74 1f "
        "8b 82 44 03 00 00 89 82 3c 03 00 00"
    ))
    image.expect(render + 0x41, bytes.fromhex("8b 3d") + struct.pack("<I", manager))

    limiter_test, limiter_branch = 0x94665D, 0x94666B
    image.expect(limiter_test, bytes.fromhex("84 db 75 10"))
    image.expect(limiter_branch - 10, bytes.fromhex("f7 05 94 e8 c9 00 00 02 00 00"))
    opcode, displacement = struct.unpack("<Hi", image.text(limiter_branch, 6))
    loop_head = limiter_branch + 6 + displacement
    if opcode != 0x850F or loop_head != 0x946541:
        raise ValueError("limiter no longer conditionally returns to the audited loop head")
    image.text(loop_head, 1)

    return {
        "main_calls": {
            "update": [update_call, wrapper],
            "render": [render_call, render],
            "lua_gc": [gc_call, garbage_collect],
            "lua_gc_game_pointer_guard": gc_guard,
        },
        "update_wrapper": {
            "input": runtime_input, "manager_update": manager_update,
            "pre_input_import_calls": [0xB3AA98, 0xB3AA80],
        },
        "layout": {
            "manager_pointer": manager, "game_pointer": game, "manager_counter_offset": counter,
            "game_room_offset": 0x18300, "room_entity_count_offset": 0x1264,
            "room_entity_array_offset": 0x125C, "entity_preview_flag_offset": 0x175,
            "entity_position_x_offset": 0x33C, "entity_backup_x_offset": 0x344,
        },
        "limiter": {
            "force_flag_test": limiter_test, "vsync_branch": limiter_branch, "loop_head": loop_head,
        },
    }


def audit(path):
    data = path.read_bytes()
    image = Image(data)
    sha256 = hashlib.sha256(data).hexdigest()
    if (image.timestamp, image.size, image.base, sha256) != (
        J510_TIMESTAMP, J510_IMAGE_SIZE, 0x400000, J510_SHA256
    ):
        raise ValueError("executable does not match the exact audited J510 Steam image")
    verified = inspect_j510(image)
    return {
        "version": "1.9.7.19.J510", "steam_build": 26774712, "sha256": sha256,
        "scope": "read-only executable preflight; address evidence, not a runnable port",
        "verified": verified,
        "not_verified": ["lifecycle and menu hooks", "graphics and Lua draw replay ABI",
                         "projectile budgets and optional native adapters", "in-game validation"],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    arguments = parser.parse_args()
    try:
        print(json.dumps(audit(arguments.executable), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{error}\n")
