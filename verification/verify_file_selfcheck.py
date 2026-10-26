"""Emulate the EXE's actual checksum loop, init gate and dispatch arithmetic.

The separate raw-file buffer represents the bytes returned by its file reader.
This is a focused regression check, not a Windows startup or audio test.
It never writes executable files or changes the expected checksum.
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP

EXE_SHA = "6a651e8164b8444a5f0639cb6a2fef2aa43c5500bd9c5288627c2030b047f876"
FILE_CALL = 0x9954
CALL_RVA = 0xA554
FILE_BUFFER = 0x10000000
STACK = 0x20000000
FRAME = STACK + 0xF000
APP = 0x30000000


def u32(machine, address):
    return struct.unpack("<I", machine.mem_read(address, 4))[0]


def put(machine, address, value):
    machine.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))


def emulate(raw_file, memory_image):
    pe = pefile.PE(data=memory_image)
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    base = pe.OPTIONAL_HEADER.ImageBase
    machine.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
    machine.mem_write(base, pe.get_memory_mapped_image())
    machine.mem_map(FILE_BUFFER, (len(raw_file) + 0xFFF) & ~0xFFF)
    machine.mem_write(FILE_BUFFER, raw_file)
    machine.mem_map(STACK, 0x10000)
    machine.mem_map(APP, 0x1000)
    machine.reg_write(UC_X86_REG_EBP, FRAME)
    machine.reg_write(UC_X86_REG_ESP, FRAME - 0x400)
    put(machine, FRAME - 0x330, FILE_BUFFER)
    put(machine, FRAME - 0x334, len(raw_file))

    # Execute the real scan and sum, starting after the file read/allocation.
    machine.emu_start(0x40F860, 0x40F9C7, count=100_000_000)
    assert machine.reg_read(UC_X86_REG_EIP) == 0x40F9C7
    marker = u32(machine, 0x4DA5F8)
    assert marker == raw_file.find(bytes.fromhex("b1 1b 0b a9")) == 0xCC958
    computed = u32(machine, FRAME - 0x358)
    assert computed == sum(b * b for b in raw_file[:marker]) & 0xFFFFFFFF
    stored = struct.unpack(">I", raw_file[marker + 4:marker + 8])[0]
    put(machine, 0x4DA5F0, computed)
    put(machine, 0x4DA5F4, stored)
    put(machine, FRAME - 0x194, APP)
    audio_init = []

    def audio_stub(uc, address, size, data):
        # Only record whether the actual gate reaches the audio helper.
        if address == 0x40CFDE:
            audio_init.append(True)
            sp = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EIP, u32(uc, sp))
            uc.reg_write(UC_X86_REG_ESP, sp + 4)

    handle = machine.hook_add(UC_HOOK_CODE, audio_stub)
    machine.emu_start(0x40F4D1, 0x40F4EC, count=100)
    machine.hook_del(handle)
    assert machine.reg_read(UC_X86_REG_EIP) == 0x40F4EC
    commands = {}
    for command in (0x66, 0x143, 0x193):
        put(machine, FRAME + 8, command)
        machine.emu_start(0x42C233, 0x42C246, count=20)
        commands[hex(command)] = hex(u32(machine, FRAME + 8))
    return {
        "file_sha256": hashlib.sha256(raw_file).hexdigest(),
        "memory_help_call": bytes(machine.mem_read(base + CALL_RVA, 5)).hex(" "),
        "computed_checksum": hex(computed), "stored_checksum": hex(stored),
        "audio_init_gate_reached": bool(audio_init), "dispatch_commands": commands,
    }


def main():
    original_path = Path(sys.argv[1])
    original = original_path.read_bytes()
    assert len(original) == 1594880
    assert hashlib.sha256(original).hexdigest() == EXE_SHA
    assert original[FILE_CALL:FILE_CALL + 5] == bytes.fromhex("e8 01 78 07 00")
    changed = bytearray(original)
    changed[FILE_CALL:FILE_CALL + 5] = b"\x90" * 5
    cases = {
        "original": emulate(original, original),
        "retired_disk_patch": emulate(bytes(changed), bytes(changed)),
        "runtime_only_candidate": emulate(original, bytes(changed)),
    }
    for name in ("original", "runtime_only_candidate"):
        case = cases[name]
        assert case["computed_checksum"] == case["stored_checksum"] == "0x123a2e7b"
        assert case["audio_init_gate_reached"]
        assert all(k == v for k, v in case["dispatch_commands"].items())
    bad = cases["retired_disk_patch"]
    assert bad["computed_checksum"] == "0x123ab8c9"
    assert not bad["audio_init_gate_reached"]
    assert all(k != v for k, v in bad["dispatch_commands"].items())
    assert hashlib.sha256(original_path.read_bytes()).hexdigest() == EXE_SHA
    print(json.dumps({
        "checks": "passed", "cases": cases,
        "scope": "Actual x86 file checksum loop, audio init gate and command arithmetic; file IO and audio helper not executed; Windows startup untested",
    }, indent=2))


if __name__ == "__main__":
    main()
