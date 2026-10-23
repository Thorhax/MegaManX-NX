#!/usr/bin/env python3
"""Replay the original Magic Carpet inputs in a headless Snes9x libretro core.

No audio device is opened. Only stage/equipment setup writes WRAM; all 1800
demonstration frames use controller input and original ROM behavior.
"""
from __future__ import annotations

import argparse
import csv
import ctypes as ct
import hashlib
import json
from pathlib import Path
import struct

ROM_SHA256 = "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"
LOADOUT = bytes.fromhex("0000dc000000000000000000dc00dc00dc08100400000000")


class GameInfo(ct.Structure):
    _fields_ = [("path", ct.c_char_p), ("data", ct.c_void_p),
                ("size", ct.c_size_t), ("meta", ct.c_char_p)]


class Variable(ct.Structure):
    _fields_ = [("key", ct.c_char_p), ("value", ct.c_char_p)]


class SystemInfo(ct.Structure):
    _fields_ = [("library_name", ct.c_char_p), ("library_version", ct.c_char_p),
                ("valid_extensions", ct.c_char_p), ("need_fullpath", ct.c_bool),
                ("block_extract", ct.c_bool)]


class Oracle:
    def __init__(self, core: Path, rom: Path, out: Path):
        self.lib = ct.CDLL(str(core.resolve()))
        info = SystemInfo()
        self.lib.retro_get_system_info.argtypes = [ct.POINTER(SystemInfo)]
        self.lib.retro_get_system_info(ct.byref(info))
        self.core_name = info.library_name.decode()
        self.core_version = info.library_version.decode()
        self.pad = 0
        self.pixel_format = 0
        self.pixels = None
        self.directory = str(out.resolve()).encode()
        self.options: dict[bytes, bytes] = {}
        self.callbacks = []
        for name, signature, callback in (
            ("environment", ct.CFUNCTYPE(ct.c_bool, ct.c_uint, ct.c_void_p), self.environment),
            ("video_refresh", ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint, ct.c_uint, ct.c_size_t), self.video),
            ("audio_sample", ct.CFUNCTYPE(None, ct.c_int16, ct.c_int16), lambda l, r: None),
            ("audio_sample_batch", ct.CFUNCTYPE(ct.c_size_t, ct.c_void_p, ct.c_size_t), lambda p, n: n),
            ("input_poll", ct.CFUNCTYPE(None), lambda: None),
            ("input_state", ct.CFUNCTYPE(ct.c_int16, ct.c_uint, ct.c_uint, ct.c_uint, ct.c_uint),
             lambda port, device, index, button: int(port == 0 and bool(self.pad & (1 << button)))),
        ):
            fn = signature(callback)
            self.callbacks.append(fn)
            getattr(self.lib, "retro_set_" + name)(fn)
        self.lib.retro_init()
        data = rom.read_bytes()
        if len(data) % 0x8000 == 512:
            data = data[512:]
        if hashlib.sha256(data).hexdigest() != ROM_SHA256:
            raise ValueError("this input fixture requires the USA Rev 1 ROM")
        self.rom = ct.create_string_buffer(data)
        self.lib.retro_load_game.argtypes = [ct.POINTER(GameInfo)]
        self.lib.retro_load_game.restype = ct.c_bool
        info = GameInfo(str(rom.resolve()).encode(), ct.cast(self.rom, ct.c_void_p), len(data), None)
        if not self.lib.retro_load_game(ct.byref(info)):
            raise RuntimeError("libretro core rejected ROM")
        self.lib.retro_get_memory_data.restype = ct.c_void_p
        self.lib.retro_get_memory_size.restype = ct.c_size_t
        if self.lib.retro_get_memory_size(2) != 0x20000:
            raise RuntimeError("expected 128 KiB SNES system WRAM")
        self.ram = (ct.c_ubyte * 0x20000).from_address(self.lib.retro_get_memory_data(2))
        self.lib.retro_serialize_size.restype = ct.c_size_t
        self.lib.retro_serialize.argtypes = [ct.c_void_p, ct.c_size_t]
        self.lib.retro_serialize.restype = ct.c_bool

    def environment(self, command, data):
        if command == 10:  # SET_PIXEL_FORMAT
            self.pixel_format = ct.cast(data, ct.POINTER(ct.c_int))[0]
            return True
        if command == 15:  # GET_VARIABLE
            var = ct.cast(data, ct.POINTER(Variable))[0]
            var.value = self.options.get(var.key)
            return bool(var.value)
        if command == 16:  # SET_VARIABLES: retain declared defaults
            variables = ct.cast(data, ct.POINTER(Variable))
            i = 0
            while variables[i].key:
                self.options[variables[i].key] = variables[i].value.split(b"; ")[-1].split(b"|")[0]
                i += 1
            return True
        if command == 17:  # GET_VARIABLE_UPDATE
            ct.cast(data, ct.POINTER(ct.c_bool))[0] = False
            return True
        if command in (9, 30, 31):  # system, core assets, save directories
            ct.cast(data, ct.POINTER(ct.c_char_p))[0] = self.directory
            return True
        if command == 18:  # SET_SUPPORT_NO_GAME
            return True
        return False

    def video(self, data, width, height, pitch):
        if data and data != ct.c_void_p(-1).value:
            self.pixels = ct.string_at(data, pitch * height), width, height, pitch

    def run(self, frames=1, pad=0):
        self.pad = pad
        for _ in range(frames):
            self.lib.retro_run()

    def word(self, address):
        return self.ram[address] | self.ram[address + 1] << 8

    def save(self, path):
        data = ct.create_string_buffer(self.lib.retro_serialize_size())
        if not self.lib.retro_serialize(data, len(data)):
            raise RuntimeError("oracle snapshot failed")
        path.write_bytes(data.raw)

    def screenshot(self, path):
        if self.pixels is None or self.pixel_format not in (0, 1, 2):
            raise RuntimeError("unsupported core video format")
        pixels, width, height, pitch = self.pixels
        if self.pixel_format != 1:
            converted = bytearray()
            green_bits = 6 if self.pixel_format == 2 else 5
            for y in range(height):
                for x in range(width):
                    v = struct.unpack_from("<H", pixels, y * pitch + x * 2)[0]
                    blue = (v & 31) * 255 // 31
                    green = ((v >> 5) & ((1 << green_bits) - 1)) * 255 // ((1 << green_bits) - 1)
                    red = ((v >> (5 + green_bits)) & 31) * 255 // 31
                    converted.extend((blue, green, red, 0))
            pixels, pitch = converted, width * 4
        header = b"BM" + struct.pack("<IHHI", 54 + width * height * 4, 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        path.write_bytes(header + b"".join(pixels[y * pitch:y * pitch + width * 4] for y in range(height)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--inputs", type=Path, default=Path(__file__).resolve().parents[1] / "tests/data/magic_carpet_inputs.csv")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    c = Oracle(args.core, args.rom, args.output)
    try:
        for _ in range(20):
            c.run(1, 8)
            c.run(119)
            if c.ram[0xd1] == 2 and c.ram[0xd2] == 4:
                break
        else:
            raise RuntimeError("did not reach gameplay from title")
        c.ram[0xd1:0xd5] = bytes([2, 2, 0, 0])
        c.ram[0x1f7a] = 5
        c.ram[0x1f81:0x1f83] = bytes(2)
        # Native stage entry creates the boots actor. Granting ownership
        # afterwards permits a dash but leaves its lower-body sprites absent.
        c.ram[0x1f99] = 8
        c.ram[0x1f94] = 0xc0
        for _ in range(30):
            c.run(60)
            if c.ram[0xd3] == 4 and c.ram[0xba9] == 2:
                break
        else:
            raise RuntimeError("did not enter Storm Eagle")
        c.run(60)
        c.ram[0x1f88:0x1fa0] = LOADOUT
        boots_ready = bool(c.ram[0xc78] and c.ram[0xc8e] == 0x5f)
        if not boots_ready:
            raise RuntimeError("stage entry did not initialize the acquired dash boots")
        tick, heart_frames, carpet_frames = 0, 0, 0
        first_frame, first_y, min_y = None, 0, 65535
        heart_reference, same_reference = None, True
        with args.inputs.open() as inputs, (args.output / "magic-carpet.csv").open("w", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["frame", "pad", "x", "y", "camera_x", "camera_y", "shot", "item", "class", "intro_clear", "counter0", "counter1", "max_health"])
            for line in inputs:
                if line.startswith("#"):
                    continue
                count, bits = line.strip().split(",")
                count, bits = int(count), int(bits, 16)
                if not 0 < count <= 1800 or not 0 <= bits < 4096:
                    raise ValueError("invalid input range")
                pad = sum(1 << j for j in range(12) if bits & (1 << (11 - j)))
                for _ in range(count):
                    c.run(1, pad)
                    shot, item, kind = 0, 0, 0
                    for d in range(0x1228, 0x1428, 64):
                        held = c.word(d + 12)
                        if (not c.ram[d] or c.ram[d + 10] != 13 or
                                not 0x1628 <= held < 0x1928 or (held - 0x1628) % 48 or not c.ram[held]):
                            continue
                        shot, item, kind = d, held, c.ram[held + 10]
                        heart_frames += kind == 11
                        if kind == 11:
                            heart_reference = (shot, item)
                        if kind == 14:
                            same_reference &= heart_reference == (shot, item)
                            if first_frame is None:
                                first_frame, first_y = tick, c.word(0xbb0)
                            carpet_frames += 1
                            min_y = min(min_y, c.word(0xbb0))
                    writer.writerow([tick, f"{pad:03x}", c.word(0xbad), c.word(0xbb0), c.word(0x1e4d), c.word(0x1e50), f"{shot:04x}", f"{item:04x}", kind, f"{c.ram[0x1f9b]:02x}", c.ram[0xb9b], c.ram[0xb9c], c.ram[0x1f9a]])
                    if tick == 60 and not (c.ram[0xbaa] == 0x14 and c.ram[0xc78] and c.ram[0xc8f] == 0x38):
                        raise RuntimeError("dash fixture did not submit the native boots pose")
                    if tick in (60, 900, 1500):
                        c.screenshot(args.output / f"carpet-{tick}.bmp")
                    if tick in (760, 1500):
                        c.save(args.output / ("carpet-setup.state" if tick == 760 else "carpet-active.state"))
                    tick += 1
        summary = dict(frames=tick, heart_frames=heart_frames, carpet_frames=carpet_frames,
                       first_carpet_frame=first_frame, first_y=first_y, min_y=min_y,
                       same_reference=same_reference, max_health=c.ram[0x1f9a],
                       dash_boots_initialized=boots_ready,
                       core_name=c.core_name, core_version=c.core_version,
                       core_sha256=hashlib.sha256(args.core.read_bytes()).hexdigest(), rom_sha256=ROM_SHA256)
        (args.output / "result.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(json.dumps(summary, indent=2))
        if (tick != 1800 or not heart_frames or carpet_frames < 120 or
                not same_reference or c.ram[0x1f9a] != 18 or first_y <= min_y + 200):
            raise RuntimeError("Magic Carpet did not reproduce; inspect trace and screenshots")
    finally:
        c.lib.retro_unload_game()
        c.lib.retro_deinit()


if __name__ == "__main__":
    main()
