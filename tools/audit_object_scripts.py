#!/usr/bin/env python3
"""Static ROM audit: which Mega Man X object classes script the player or the
camera, and which authored spawn records a widened/co-op look-ahead can
allocate out of native order.

Everything is derived from the ROM (pool loops, class dispatch tables, the
stage spawn-record lists, a per-function CFG walk of every class handler) and
cross-referenced against the current C policy (parsed from src/). DiztinGUIsh
metadata from symbols/ is used only as a hint (jump-table bounds, names, M/X
fallback after an unmatched PLP); every use is recorded in the JSON.

Usage (from the project root):
  py -3 -I tools/audit_object_scripts.py --out build/object_script_audit.json
  [--ws-extra 72] [--policy-baseline] [--wram-check <replay.sav>]

JSON sections: pools, spawn_kinds, helper_family, camera_region_system,
classes (per pool/class: effects tiered own/family/generic by how many class
handlers share the function, helper calls, code-spawned children),
records (every stage record, with camera-region rectangles/commands),
a_scripting_enemy_classes, b_ordering_hazards (+ _lock_only, _any_enemy),
b_player_x_preconditions, c_projection_candidates, walker_stats.

Walker limits: indirect stores through pointers ((dp),Y / [dp],Y), JMP (abs)
and JML [abs] vectors, RTS/RTL stack dispatch and callee-pulled return
addresses are recorded, not followed; abs,X/abs,Y stores with an unknown
index are reported only when the base is within $20 below a watched address;
jump-table length comes from diz pointer regions, else from the first target.

stdlib only. The 65816 decoder is imported by path (--decoder).
"""
from __future__ import annotations

import argparse
import bisect
import collections
import hashlib
import importlib.util
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROM = os.path.join(ROOT, 'mmx.sfc')
DEFAULT_DECODER = r'F:\Projects\snesrecomp\snesrecomp\recompiler\snes65816.py'
USA_SHA1 = 'C65216760BA99178100A10D98457CF11496C2097'

# ---------------------------------------------------------------------------
# Addresses this audit cares about (low WRAM, $7E:0000-$1FFF mirror).
# ---------------------------------------------------------------------------
WATCH = {}
def _watch(lo, hi, cat):
    for a in range(lo, hi + 1):
        WATCH[a] = cat
_watch(0x0BAA, 0x0BAB, 'player_lock')      # action / sub-action
_watch(0x0C12, 0x0C12, 'player_lock')      # saved action ($84:9F8A family)
_watch(0x0C15, 0x0C16, 'player_lock')      # saved action / forced action
_watch(0x0BAD, 0x0BAE, 'player_pos')
_watch(0x0BB0, 0x0BB1, 'player_pos')
_watch(0x1E4D, 0x1E55, 'camera_pos')       # target x/y, speed
_watch(0x1E56, 0x1E61, 'camera_bounds')    # authored limits + live left/right locks
_watch(0x1E62, 0x1E67, 'camera_pos')       # scroll speed/state
_watch(0x1E68, 0x1E69, 'camera_bounds')    # live top lock
_watch(0x1E6A, 0x1E6D, 'camera_pos')       # previous camera x/y (scan anchor)
_watch(0x1E6E, 0x1E6F, 'camera_bounds')    # live bottom lock
for _a in (0x1F0C, 0x1F0E, 0x1F0F, 0x1F10, 0x1F13, 0x1F19, 0x1F23, 0x1F31,
           0x1F3B, 0x1F48, 0x1F49, 0x00D1, 0x00D2, 0x00D3, 0x00D4):
    WATCH[_a] = 'scene'
WRITE_CATS = ('player_lock', 'player_pos', 'camera_bounds', 'camera_pos', 'scene')
READ_CATS = ('camera_bounds', 'player_lock', 'scene')   # plain reads recorded
CMP_CATS = ('camera_bounds', 'player_lock', 'player_pos', 'scene', 'camera_pos')

# Helper family search window: tiny RTL leaves in bank $84 that pose/lock the
# world body. Entry points are derived (JSL targets inside the window whose
# walk writes $0BAA/$0C16), not hard-coded.
HELPER_WINDOW = (0x84, 0x9F60, 0xA081)

# Object-pool update loops in bank $80, called from $80:D21D.. (entered with
# SEP #$30, D = slot). The second address is the variant the same caller uses
# while that pool's freeze flag ($1F13..$1F18) is set; it is parsed as a check.
POOL_LOOPS = [
    ('player_shots', 0xD43A, 0xD457),
    ('enemies', 0xD4EA, 0xD507),
    ('enemy_shots', 0xD3DD, 0xD3FA),
    ('effects', 0xD48D, 0xD4AA),
    ('items', 0xD2DE, 0xD300),
    ('decor', 0xD345, None),
    ('events', 0xD39B, 0xD3BC),
]

# ---------------------------------------------------------------------------
def h16(v):
    return None if v is None else '$%04X' % v

def fa(bank, addr):
    return '%02X:%04X' % (bank, addr)


class Rom:
    def __init__(self, path):
        data = open(path, 'rb').read()
        self.header = len(data) % 0x8000 == 512
        if self.header:
            data = data[512:]
        self.data = data
        self.sha1 = hashlib.sha1(data).hexdigest().upper()

    def off(self, bank, addr):
        if (bank & 0xFE) == 0x7E or addr < 0x8000:
            return None
        o = ((bank & 0x7F) << 15) | (addr & 0x7FFF)
        return o if o < len(self.data) else None

    def byte(self, bank, addr):
        return self.data[self.off(bank, addr)]

    def word(self, bank, addr):
        o = self.off(bank, addr)
        return self.data[o] | (self.data[o + 1] << 8)


class Diz:
    """Third-party DiztinGUIsh classification, used as hints only."""
    def __init__(self, symdir):
        self.ok = False
        self.regions = []
        self.mx = {}
        self.db = {}
        self.labels = {}
        try:
            with open(os.path.join(symdir, 'classification.tsv')) as f:
                next(f)
                for line in f:
                    s, n, k = line.rstrip('\n').split('\t')
                    self.regions.append((int(s, 16), int(n), k))
            self.regions.sort()
            self.starts = [r[0] for r in self.regions]
            with open(os.path.join(symdir, 'instruction_metadata.tsv')) as f:
                next(f)
                for line in f:
                    p = line.split('\t')
                    self.mx[int(p[0], 16)] = (int(p[1]), int(p[2]))
                    self.db[int(p[0], 16)] = int(p[3], 16)
            with open(os.path.join(symdir, 'imported_symbols.tsv')) as f:
                next(f)
                for line in f:
                    p = line.rstrip('\n').split('\t')
                    if len(p) >= 3 and p[1] != 'ram':
                        a = int(p[0], 16)
                        self.labels[((a >> 16) | 0x80) << 16 | (a & 0xFFFF)] = p[2]
            self.ok = True
        except (OSError, ValueError):
            self.ok = False

    @staticmethod
    def key(bank, addr):
        return ((bank & 0x7F) << 16) | addr

    def region(self, bank, addr):
        if not self.ok:
            return None
        k = self.key(bank, addr)
        i = bisect.bisect_right(self.starts, k) - 1
        if i < 0:
            return None
        s, n, kind = self.regions[i]
        return (s, n, kind) if k < s + n else None

    def kind(self, bank, addr):
        r = self.region(bank, addr)
        return r[2] if r else None

    def label(self, bank, addr):
        return self.labels.get(((bank | 0x80) << 16) | addr)


def load_decoder(path):
    if os.path.isdir(path):
        path = os.path.join(path, 'snes65816.py')
    spec = importlib.util.spec_from_file_location('snes65816_audit', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------
# Instruction decoding wrapper
# ---------------------------------------------------------------------------
class Dec:
    def __init__(self, rom, s):
        self.rom = rom
        self.s = s
        self.mode = {getattr(s, k): k for k in ('IMP', 'ACC', 'IMM', 'DP', 'DP_X', 'DP_Y', 'ABS',
                     'ABS_X', 'ABS_Y', 'LONG', 'LONG_X', 'REL', 'REL16', 'STK', 'INDIR',
                     'INDIR_X', 'INDIR_Y', 'INDIR_LY', 'INDIR_L', 'INDIR_DPX', 'DP_INDIR',
                     'STK_IY') if hasattr(s, k)}
        self.cache = {}

    def at(self, bank, addr, m, x):
        k = (bank, addr, m, x)
        if k in self.cache:
            return self.cache[k]
        o = self.rom.off(bank, addr)
        r = None
        if o is not None and o + 4 <= len(self.rom.data):
            i = self.s.decode_insn(self.rom.data, o, addr, bank, m, x)
            if i is not None:
                r = (i.mnem, self.mode.get(i.mode, str(i.mode)), i.operand, i.length, i.opcode)
        self.cache[k] = r
        return r


# ---------------------------------------------------------------------------
# Pools and class tables
# ---------------------------------------------------------------------------
def linear(dec, bank, addr, m, x, limit=40):
    out = []
    for _ in range(limit):
        i = dec.at(bank, addr, m, x)
        if not i:
            break
        out.append((addr,) + i)
        mn, md, op, ln, _ = i
        if mn == 'REP':
            m = 0 if op & 0x20 else m; x = 0 if op & 0x10 else x
        elif mn == 'SEP':
            m = 1 if op & 0x20 else m; x = 1 if op & 0x10 else x
        if mn in ('RTS', 'RTL', 'JMP'):
            break
        addr = (addr + ln) & 0xFFFF
    return out


def parse_pool_loop(dec, name, entry):
    """REP #$20; LDA #base; TCD ... ADC #step; CMP #end, with the class
    dispatch either inline or one JSR away."""
    ins = linear(dec, 0x80, entry, 1, 1, 60)
    base = step = end = None
    table = None
    via = None
    for k, (a, mn, md, op, ln, opc) in enumerate(ins):
        if mn == 'LDA' and md == 'IMM' and base is None and k + 1 < len(ins) and ins[k + 1][1] == 'TCD':
            base = op
        elif mn == 'ADC' and md == 'IMM' and step is None:
            step = op
        elif mn == 'CMP' and md == 'IMM' and step is not None and end is None:
            end = op
        elif mn in ('JSR', 'JMP') and md == 'INDIR_X' and table is None:
            table, via = op, a
        elif mn == 'JSR' and md == 'ABS' and table is None:
            for (a2, mn2, md2, op2, ln2, opc2) in linear(dec, 0x80, op, 1, 1, 20):
                if mn2 in ('JSR', 'JMP') and md2 == 'INDIR_X':
                    table, via = op2, a2
                    break
    if None in (base, step, end, table):
        raise SystemExit('pool loop %s at $80:%04X did not match the expected shape' % (name, entry))
    return dict(name=name, loop=fa(0x80, entry), dispatch=fa(0x80, via), base=base, step=step,
                end=end, slots=(end - base) // step, table=table)


def read_class_table(rom, diz, table):
    """Entries until the first stub (tables directly precede their stubs)."""
    ents = []
    lo = 0x10000
    i = 0
    while table + 2 * i < lo and i < 256:
        v = rom.word(0x80, table + 2 * i)
        if v > table:
            lo = min(lo, v)
        ents.append(v)
        i += 1
    r = diz.region(0x80, table)
    diz_n = r[1] // 2 if r and r[2] == 'pointer16' and r[0] == Diz.key(0x80, table) else None
    return ents, diz_n


def parse_kind_allocators(dec, rom):
    """$82:82AC: JMP ($82B1,X) by kind; each allocator LDX #base ... CPX #end."""
    ins = linear(dec, 0x82, 0x82AC, 0, 0, 3)
    jt = [i for i in ins if i[1] == 'JMP' and i[2] == 'INDIR_X']
    if not jt:
        raise SystemExit('kind allocator $82:82AC not recognised')
    tbl = jt[0][3]
    first = rom.word(0x82, tbl)
    n = (first - tbl) // 2
    kinds = {}
    for k in range(n):
        tgt = rom.word(0x82, tbl + 2 * k)
        base = step = end = None
        for (a, mn, md, op, ln, opc) in linear(dec, 0x82, tgt, 1, 0, 14):
            if mn == 'LDX' and md == 'IMM' and base is None:
                base = op
            elif mn == 'ADC' and md == 'IMM':
                step = op
            elif mn == 'CPX' and md == 'IMM':
                end = op
        kinds[k] = dict(allocator=fa(0x82, tgt), base=base, step=step, end=end)
    return tbl, kinds


# ---------------------------------------------------------------------------
# Stage spawn records ($80:DB71 builder, $85 per-stage lists)
# ---------------------------------------------------------------------------
def parse_stage_pointer_table(dec, rom):
    for (a, mn, md, op, ln, opc) in linear(dec, 0x80, 0xDB71, 1, 1, 30):
        if mn == 'LDA' and md == 'ABS_X':
            return op
    raise SystemExit('spawn builder $80:DB71 not recognised')


def parse_stage(rom, ptr_tbl, stage):
    p = rom.word(0x85, ptr_tbl + 2 * stage)
    start = p
    if rom.byte(0x85, p) == 0xFF:
        # Unused slots (13-15) point at a lone $FF: never a stage index.
        return dict(stage=stage, list='85:%04X' % start, end='85:%04X' % (p + 1), records=[],
                    unused=True)
    counter = 0xFFFF
    cols = []
    recs = []
    guard = 0
    while True:
        c = rom.byte(0x85, p); p += 1
        if c == counter:
            break
        if counter != 0xFFFF and c < counter:
            raise SystemExit('stage %d: non-monotonic column byte at $85:%04X' % (stage, p - 1))
        counter = c
        while True:
            d = [rom.byte(0x85, p + k) for k in range(7)]
            recs.append(dict(stage=stage, column=c, desc=p, raw=d))
            p += 7
            guard += 1
            if guard > 4000:
                raise SystemExit('stage %d: runaway record list' % stage)
            if d[6] & 0x80:
                break
        cols.append(c)
    out = []
    for i, r in enumerate(recs):
        d = r['raw']
        x = (d[5] | (d[6] << 8)) & 0x1FFF
        y = d[1] | (d[2] << 8)
        out.append(dict(stage=stage, index=i, column=r['column'], desc='85:%04X' % r['desc'],
                        desc_addr=r['desc'], kind=d[0] & 0x0F, filter=d[0] & 0xF8,
                        y=y, cls=d[3], sub=d[4], x=x, raw=' '.join('%02X' % b for b in d)))
    return dict(stage=stage, list='85:%04X' % start, end='85:%04X' % p, records=out)


def build_wram_table(stage_info):
    """Reproduce $80:DB71's WRAM output (minus the live bytes)."""
    tbl = {}
    flags = []
    ptr = 0xFA02
    e = 0
    by_col = collections.OrderedDict()
    for r in stage_info['records']:
        by_col.setdefault(r['column'], []).append(r)
    for c, rs in by_col.items():
        while e <= c:
            tbl[e] = ptr; e += 1
        for r in rs:
            flags.append((ptr, r['y'], r['desc_addr']))
            ptr += 5
    while e <= 0x100:
        tbl[e] = ptr; e += 1
    return tbl, flags


def validate_wram(stage_info, wram):
    tbl, flags = build_wram_table(stage_info)
    bad = []
    for e, v in tbl.items():
        w = wram[0xF800 + 2 * e] | (wram[0xF801 + 2 * e] << 8)
        if w != v:
            bad.append('F800[%d]=%04X expected %04X' % (e, w, v))
    for (a, y, d) in flags:
        wy = wram[a + 1] | (wram[a + 2] << 8)
        wd = wram[a + 3] | (wram[a + 4] << 8)
        if wy != y or wd != d:
            bad.append('%04X: y %04X/%04X desc %04X/%04X' % (a, wy, y, wd, d))
    return dict(table_entries=len(tbl), flag_entries=len(flags), mismatches=bad[:20],
                mismatch_count=len(bad))


# ---------------------------------------------------------------------------
# CFG walker
# ---------------------------------------------------------------------------
STORE_OPS = {'STA': 'A', 'STX': 'X', 'STY': 'Y', 'STZ': 'Z'}
RMW_OPS = {'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR', 'TSB', 'TRB'}
READ_OPS = {'LDA', 'LDX', 'LDY', 'CMP', 'CPX', 'CPY', 'BIT', 'AND', 'ORA', 'EOR', 'ADC', 'SBC'}
BRANCHES = {'BPL', 'BMI', 'BEQ', 'BNE', 'BCC', 'BCS', 'BVC', 'BVS'}
BAD_OPS = {'BRK', 'COP', 'STP', 'WDM'}
MAX_STACK = 12
MAX_STATES = 30000


class Func:
    __slots__ = ('key', 'insns', 'effects', 'calls', 'unresolved', 'exits', 'notes',
                 'tables', 'done', 'states', 'diz_mx_mismatch', 'diz_data_insns')

    def __init__(self, key):
        self.key = key
        self.insns = set()
        self.effects = {}
        self.calls = {}
        self.unresolved = {}
        self.exits = set()
        self.notes = set()
        self.tables = {}
        self.done = False
        self.states = 0
        self.diz_mx_mismatch = 0
        self.diz_data_insns = set()


def fkey_str(k):
    b, a, m, x, d = k
    ds = 'obj' if d == 'OBJ' else ('?' if d is None else '%04X' % d)
    return '%02X:%04X/m%dx%d/D=%s' % (b, a, m, x, ds)


class Walker:
    def __init__(self, rom, dec, diz, table_cap=64):
        self.rom = rom
        self.dec = dec
        self.diz = diz
        self.funcs = {}
        self.inprog = set()
        self.table_cap = table_cap
        self.table_log = {}

    # -- jump tables -----------------------------------------------------
    def jump_table(self, bank, tbl, site):
        k = (bank, tbl)
        if k in self.table_log:
            return self.table_log[k]['targets']
        targets = []
        reason = None
        reg = self.diz.region(bank, tbl)
        diz_bound = None
        if reg and reg[2] in ('pointer16', 'data16'):
            diz_bound = (reg[0] + reg[1] - Diz.key(bank, tbl)) // 2
        lo = 0x10000
        i = 0
        while True:
            if i >= self.table_cap:
                reason = 'cap'; break
            if diz_bound is not None and i >= diz_bound:
                reason = 'diz_region_end'; break
            a = tbl + 2 * i
            if a >= lo:
                reason = 'reached_first_target'; break
            if site[0] == bank and tbl < site[1] and a >= site[1]:
                reason = 'reached_dispatch'; break
            if a > 0xFFFE or self.rom.off(bank, a) is None:
                reason = 'bank_end'; break
            v = self.rom.word(bank, a)
            if v < 0x8000:
                reason = 'non_rom_entry'; break
            if diz_bound is None and i > 0:
                kd = self.diz.kind(bank, v)
                if kd is not None and kd != 'code' and kd != 'unknown':
                    reason = 'entry_points_at_diz_%s' % kd; break
                if (Diz.key(bank, v) not in self.diz.mx) and kd == 'code':
                    reason = 'entry_not_diz_insn_boundary'; break
            if v > tbl:
                lo = min(lo, v)
            targets.append(v)
            i += 1
        self.table_log[k] = dict(table=fa(bank, tbl), site=fa(*site), entries=len(targets),
                                 stop=reason, diz_bound=diz_bound,
                                 targets=targets)
        return targets

    # -- functions -------------------------------------------------------
    def func(self, bank, addr, m, x, d, db=None):
        key = (bank, addr, m, x, d)
        f = self.funcs.get(key)
        if f is not None:
            return f
        f = Func(key)
        self.funcs[key] = f
        self.inprog.add(key)
        try:
            self._walk(f, db)
        finally:
            self.inprog.discard(key)
            f.done = True
        return f

    def _eff(self, f, rec):
        k = tuple(sorted((kk, str(vv)) for kk, vv in rec.items()))
        f.effects[k] = rec

    def _mem_target(self, md, op, D, DB, X, Y, x):
        """Return (resolved_addr|None, how) for a data access."""
        if md == 'ABS':
            if DB is not None and DB == 0x7F:
                return None, 'abs_7f'
            return op, 'abs'
        if md in ('ABS_X', 'ABS_Y'):
            r = X if md == 'ABS_X' else Y
            if r is not None and (DB is None or DB != 0x7F):
                return (op + r) & 0xFFFF, md.lower() + '_const'
            return None, md.lower()
        if md in ('LONG', 'LONG_X'):
            b = op >> 16
            a = op & 0xFFFF
            if md == 'LONG_X':
                if X is None:
                    return None, 'long_x'
                a = (a + X) & 0xFFFF
            if b == 0x7E or ((b & 0x7F) < 0x40 and a < 0x2000):
                return a, 'long'
            return None, 'long_other'
        if md in ('DP', 'DP_X', 'DP_Y'):
            if D == 'OBJ':
                return None, 'dp_obj'
            if D is None:
                return None, 'dp_unknown_D'
            a = (D + op) & 0xFFFF
            if md != 'DP':
                r = X if md == 'DP_X' else Y
                if r is None:
                    return None, 'dp_idx_constD'
                a = (a + r) & 0xFFFF
            return a, 'dp_constD'
        return None, md.lower()

    def _walk(self, f, db_entry):
        bank, entry, m0, x0, d0 = f.key
        rom, dec, diz = self.rom, self.dec, self.diz
        # state: (pc, m, x, D, DB, stack) -> regs (A=(lo,hi), X, Y, Asrc)
        seen = {}
        work = [(entry, m0, x0, d0, db_entry, (), ((None, None), None, None, None))]
        while work:
            pc, m, x, D, DB, stk, regs = work.pop()
            skey = (pc, m, x, D, DB, stk)
            old = seen.get(skey)
            if old is not None:
                merged = tuple(o if o == n else None for o, n in zip(old[1:], regs[1:]))
                ma = tuple(o if o == n else None for o, n in zip(old[0], regs[0]))
                new = (ma,) + merged
                if new == old:
                    continue
                regs = new
            seen[skey] = regs
            f.states += 1
            if f.states > MAX_STATES:
                f.notes.add('state_cap_hit'); break
            A, X, Y, Asrc = regs
            i = dec.at(bank, pc, m, x)
            if i is None:
                f.unresolved[(pc, 'undecodable')] = dict(pc=fa(bank, pc), why='undecodable/non-ROM')
                continue
            mn, md, op, ln, opc = i
            f.insns.add(pc)
            dk = Diz.key(bank, pc)
            dmx = diz.mx.get(dk)
            if dmx is not None and dmx != (m, x):
                f.diz_mx_mismatch += 1
                if self._len_sensitive(bank, pc, m, x, dmx):
                    f.notes.add('diz_mx_length_conflict@%s' % fa(bank, pc))
            kd = diz.kind(bank, pc)
            if kd is not None and kd not in ('code', 'unknown'):
                f.diz_data_insns.add(pc)
            nxt = (pc + ln) & 0xFFFF
            succ = []          # list of (pc, m, x, D, DB, stk, regs)
            nm, nx, nD, nDB, nstk = m, x, D, DB, stk
            nA, nX, nY, nAsrc = A, X, Y, Asrc

            def push(cells):
                return (stk + tuple(cells))[-MAX_STACK:]

            if mn in BAD_OPS:
                f.unresolved[(pc, 'bad')] = dict(pc=fa(bank, pc), why='hit %s (data decoded as code?)' % mn)
                continue

            # ---------------- control flow
            if mn in BRANCHES:
                succ.append(op); succ.append(nxt)
            elif mn in ('BRA', 'BRL'):
                succ.append(op)
            elif mn == 'JMP' and md == 'ABS':
                succ.append(op)
            elif mn == 'JMP' and md == 'LONG':
                tb, ta = op >> 16, op & 0xFFFF
                self._call(f, pc, tb, ta, m, x, D, DB, 'jml')
                # a PEA/PER-pushed return address makes this a call
                ret = self._pushed_return(stk)
                if ret is not None:
                    f.notes.add('pushed_return_jml')
                    work.append((ret, m, x, D, DB, stk[:-3] if len(stk) >= 3 else (), ((None, None), None, None, None)))
                else:
                    for em in self._exit_of(tb, ta, m, x, D):
                        f.exits.add(em)
                continue
            elif mn == 'JMP' and md in ('INDIR', 'INDIR_X') and opc != 0x7C:
                f.unresolved[(pc, 'jmpind')] = dict(pc=fa(bank, pc), why='%s ($%04X) indirect (vector)' % (
                    'JML [abs]' if opc == 0xDC else 'JMP (abs)', op))
                continue
            elif mn == 'JMP' and opc == 0x7C:
                for t in self.jump_table(bank, op, (bank, pc)):
                    work.append((t, m, x, D, DB, stk, ((None, None), None, None, None)))
                f.tables[pc] = op
                continue
            elif mn == 'JSR' and md == 'ABS':
                ex = self._call(f, pc, bank, op, m, x, D, DB, 'jsr')
                nm, nx = ex
                nA, nX, nY, nAsrc = (None, None), None, None, None
                succ.append(nxt)
            elif mn == 'JSR' and opc == 0xFC:
                exs = set()
                for t in self.jump_table(bank, op, (bank, pc)):
                    exs.add(self._call(f, pc, bank, t, m, x, D, DB, 'jsr_table'))
                f.tables[pc] = op
                if len(exs) == 1:
                    nm, nx = exs.pop()
                elif exs:
                    nm, nx = self._mx_fallback(bank, nxt, m, x, f, 'jsr_table_mixed_exit')
                nA, nX, nY, nAsrc = (None, None), None, None, None
                succ.append(nxt)
            elif mn == 'JSL':
                tb, ta = op >> 16, op & 0xFFFF
                if (tb & 0xFE) == 0x7E:
                    f.unresolved[(pc, 'ramcall')] = dict(pc=fa(bank, pc), why='JSL into WRAM $%06X' % op)
                    nm, nx = m, x
                else:
                    nm, nx = self._call(f, pc, tb, ta, m, x, D, DB, 'jsl')
                nA, nX, nY, nAsrc = (None, None), None, None, None
                succ.append(nxt)
            elif mn in ('RTS', 'RTL', 'RTI'):
                if stk:
                    f.notes.add('return_with_pushed_bytes')
                    if self._pushed_return(stk) is not None or len(stk) >= 2:
                        f.unresolved[(pc, 'rtsdispatch')] = dict(
                            pc=fa(bank, pc), why='%s with %d pushed bytes (stack dispatch?)' % (mn, len(stk)))
                f.exits.add((m, x))
                continue
            # ---------------- flags / registers
            elif mn == 'REP':
                if op & 0x20: nm = 0
                if op & 0x10: nx = 0
            elif mn == 'SEP':
                if op & 0x20: nm = 1
                if op & 0x10:
                    nx = 1
                    nX = None if X is None else X & 0xFF
                    nY = None if Y is None else Y & 0xFF
            elif mn == 'XCE':
                f.notes.add('xce')
            # ---------------- stack
            elif mn in ('PHA', 'PHX', 'PHY'):
                if mn == 'PHA':
                    wide = m == 0
                    lo, hi = A
                else:
                    wide = x == 0
                    r = X if mn == 'PHX' else Y
                    lo = None if r is None else r & 0xFF
                    hi = None if r is None else r >> 8
                nstk = push([hi, lo] if wide else [lo])
            elif mn == 'PHB':
                nstk = push([('B', DB)])
            elif mn == 'PHK':
                nstk = push([('B', bank)])
            elif mn == 'PHD':
                nstk = push([('Dh', D), ('Dl', D)])
            elif mn == 'PHP':
                nstk = push([('P', m, x)])
            elif mn == 'PEA':
                nstk = push([op >> 8, op & 0xFF])
            elif mn == 'PER':
                nstk = push([('R', op, 'h'), ('R', op, 'l')])
            elif mn == 'PEI':
                nstk = push([None, None])
            elif mn in ('PLA', 'PLX', 'PLY', 'PLB', 'PLD', 'PLP'):
                size = {'PLA': 1 if m else 2, 'PLX': 1 if x else 2, 'PLY': 1 if x else 2,
                        'PLB': 1, 'PLD': 2, 'PLP': 1}[mn]
                cells = []
                s2 = list(stk)
                for _ in range(size):
                    if s2:
                        cells.append(s2.pop())
                    else:
                        cells.append('UNDER')
                nstk = tuple(s2)
                if 'UNDER' in cells:
                    f.notes.add('stack_underflow_%s' % mn)
                    f.unresolved[(pc, 'under')] = dict(pc=fa(bank, pc),
                                                       why='%s pulls caller stack (return-address/inline-arg use?)' % mn)
                if mn == 'PLP':
                    c = cells[0]
                    if isinstance(c, tuple) and c[0] == 'P':
                        nm, nx = c[1], c[2]
                    else:
                        nm, nx = self._mx_fallback(bank, nxt, m, x, f, 'plp_unmatched')
                elif mn == 'PLB':
                    c = cells[0]
                    nDB = c[1] if isinstance(c, tuple) and c[0] == 'B' else (c if isinstance(c, int) else None)
                elif mn == 'PLD':
                    lo, hi = cells[0], cells[1]
                    if isinstance(lo, tuple) and lo[0] == 'Dl' and isinstance(hi, tuple) and hi[0] == 'Dh' and lo[1] == hi[1]:
                        nD = lo[1]
                    elif isinstance(lo, int) and isinstance(hi, int):
                        nD = lo | (hi << 8)
                    else:
                        nD = None
                elif mn == 'PLA':
                    v = [c if isinstance(c, int) else None for c in cells]
                    nA = (v[0], A[1]) if m else (v[0], v[1])
                    nAsrc = None
                else:
                    v = [c if isinstance(c, int) else None for c in cells]
                    val = v[0] if x else (None if None in v else v[0] | (v[1] << 8))
                    if mn == 'PLX': nX = val
                    else: nY = val
            # ---------------- transfers / constants
            elif mn == 'TCD':
                nD = None if None in A else A[0] | (A[1] << 8)
            elif mn == 'TDC':
                nA = (D & 0xFF, D >> 8) if isinstance(D, int) else (None, None)
                nAsrc = None
            elif mn in ('TAX', 'TAY'):
                if x:
                    v = A[0]
                else:
                    v = None if None in A else A[0] | (A[1] << 8)
                if mn == 'TAX': nX = v
                else: nY = v
            elif mn in ('TXA', 'TYA'):
                r = X if mn == 'TXA' else Y
                lo = None if r is None else r & 0xFF
                hi = None if r is None else r >> 8
                nA = (lo, A[1]) if m else (lo, hi)
                nAsrc = None
            elif mn == 'TXY':
                nY = X
            elif mn == 'TYX':
                nX = Y
            elif mn == 'XBA':
                nA = (A[1], A[0]); nAsrc = None
            elif mn in ('INX', 'DEX', 'INY', 'DEY'):
                r = X if mn in ('INX', 'DEX') else Y
                if r is not None:
                    r = (r + (1 if mn[:2] == 'IN' else -1)) & (0xFF if x else 0xFFFF)
                if 'X' in mn: nX = r
                else: nY = r
            elif mn in ('TSX', 'TSC'):
                if mn == 'TSX': nX = None
                else: nA = (None, None)
            elif mn in ('TXS', 'TCS'):
                f.notes.add('stack_pointer_write')
            elif mn in ('INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR') and md == 'ACC':
                nA = (None, None) if not m else (None, A[1]); nAsrc = None
            elif mn == 'MVN' or mn == 'MVP':
                f.notes.add('block_move')
                nA = (None, None); nX = None; nY = None

            # ---------------- loads (constants)
            if mn == 'LDA':
                if md == 'IMM':
                    nA = (op & 0xFF, A[1]) if m else (op & 0xFF, op >> 8)
                    nAsrc = None
                else:
                    nA = (None, A[1]) if m else (None, None)
                    t, how = self._mem_target(md, op, D, DB, X, Y, x)
                    nAsrc = t if (t is not None and t in WATCH) else None
            elif mn in ('LDX', 'LDY'):
                v = op if md == 'IMM' else None
                if mn == 'LDX': nX = v
                else: nY = v
            elif mn in ('AND', 'ORA', 'EOR') and md == 'IMM':
                if m:
                    lo = A[0]
                    if lo is not None:
                        lo = {'AND': lo & op, 'ORA': lo | op, 'EOR': lo ^ op}[mn] & 0xFF
                    nA = (lo, A[1])
                else:
                    if None not in A:
                        w = A[0] | (A[1] << 8)
                        w = {'AND': w & op, 'ORA': w | op, 'EOR': w ^ op}[mn] & 0xFFFF
                        nA = (w & 0xFF, w >> 8)
                    else:
                        nA = (None, None)
            elif mn in ('AND', 'ORA', 'EOR', 'ADC', 'SBC'):
                nA = (None, A[1]) if m else (None, None)
                nAsrc = None

            # ---------------- memory effects
            if md not in ('IMM', 'IMP', 'ACC', 'REL', 'REL16', 'STK') and mn not in ('JMP', 'JSR', 'JSL', 'PEA', 'PEI', 'PER'):
                t, how = self._mem_target(md, op, D, DB, X, Y, x)
                is_store = mn in STORE_OPS
                is_rmw = mn in RMW_OPS and md != 'ACC'
                if is_store or is_rmw:
                    wide = (x == 0) if mn in ('STX', 'STY') else (m == 0)
                    width = 2 if wide else 1
                    if t is not None:
                        hit = [a for a in range(t, t + width) if a in WATCH]
                        if hit:
                            val = None
                            if mn == 'STZ':
                                val = 0
                            elif mn == 'STA':
                                val = A[0] if width == 1 else (None if None in A else A[0] | (A[1] << 8))
                            elif mn in ('STX', 'STY'):
                                r = X if mn == 'STX' else Y
                                val = r
                            self._eff(f, dict(type='write', pc=fa(bank, pc), op=mn, addr=t, width=width,
                                              value=val, how=how, cats=sorted({WATCH[a] for a in hit})))
                    else:
                        if how in ('abs_x', 'abs_y'):
                            near = [w for w in range(op, op + 0x20) if w in WATCH]
                            if near:
                                self._eff(f, dict(type='write_indexed', pc=fa(bank, pc), op=mn, base=op,
                                                  how=how, cats=sorted({WATCH[a] for a in near})))
                        elif how not in ('dp_obj',):
                            f.notes.add('unresolved_store_%s' % how)
                            if how in ('dp_unknown_D', 'abs_7f', 'long_x', 'long_other', 'dp_idx_constD'):
                                pass
                            else:
                                f.unresolved.setdefault((pc, 'ptrstore'), dict(
                                    pc=fa(bank, pc), why='%s store via %s' % (mn, how)))
                elif mn in READ_OPS and t is not None and t in WATCH:
                    cat = WATCH[t]
                    if mn in ('CMP', 'CPX', 'CPY') :
                        pass
                    elif cat in READ_CATS:
                        self._eff(f, dict(type='read', pc=fa(bank, pc), op=mn, addr=t, cats=[cat]))
                # compares: LDA watched; CMP #imm / CMP watched
                if mn == 'CMP' and Asrc is not None:
                    rhs = op if md == 'IMM' else t
                    cats = sorted({WATCH[Asrc]} | ({WATCH[t]} if (t is not None and t in WATCH) else set()))
                    if any(c in CMP_CATS for c in cats):
                        self._eff(f, dict(type='compare', pc=fa(bank, pc), lhs=Asrc,
                                          rhs_imm=op if md == 'IMM' else None,
                                          rhs_addr=None if md == 'IMM' else rhs, cats=cats))
            elif mn == 'CMP' and md == 'IMM' and Asrc is not None:
                self._eff(f, dict(type='compare', pc=fa(bank, pc), lhs=Asrc, rhs_imm=op, rhs_addr=None,
                                  cats=[WATCH[Asrc]]))

            for t in succ:
                work.append((t, nm, nx, nD, nDB, nstk, (nA, nX, nY, nAsrc)))
            if not succ and mn not in ('BRA', 'BRL'):
                work.append((nxt, nm, nx, nD, nDB, nstk, (nA, nX, nY, nAsrc)))

    def _len_sensitive(self, bank, pc, m, x, dmx):
        a = self.dec.at(bank, pc, m, x)
        b = self.dec.at(bank, pc, dmx[0], dmx[1])
        return a is not None and b is not None and a[3] != b[3]

    def _pushed_return(self, stk):
        if len(stk) >= 2:
            lo, hi = stk[-1], stk[-2]
            if isinstance(lo, tuple) and lo[0] == 'R':
                return (lo[1] + 1) & 0xFFFF
            if isinstance(lo, int) and isinstance(hi, int) and len(stk) >= 3 and isinstance(stk[-3], tuple) and stk[-3][0] == 'B':
                return ((hi << 8) | lo) + 1 & 0xFFFF
        return None

    def _mx_fallback(self, bank, addr, m, x, f, why):
        dmx = self.diz.mx.get(Diz.key(bank, addr))
        if dmx is not None:
            f.notes.add('%s:diz_mx' % why)
            return dmx
        f.notes.add('%s:assumed_unchanged' % why)
        return (m, x)

    def _exit_of(self, tb, ta, m, x, D):
        k = (tb, ta, m, x, D)
        g = self.funcs.get(k)
        if g is None or not g.exits:
            return {(m, x)}
        return g.exits

    def _call(self, f, site, tb, ta, m, x, D, DB, kind):
        if self.rom.off(tb, ta) is None:
            f.unresolved[(site, 'badcall')] = dict(pc=fa(f.key[0], site), why='%s to non-ROM %02X:%04X' % (kind, tb, ta))
            return (m, x)
        k = (tb, ta, m, x, D)
        f.calls[(site, k)] = dict(site=fa(f.key[0], site), kind=kind, callee=k)
        if k in self.inprog:
            f.notes.add('recursive_call')
            return (m, x)
        g = self.func(tb, ta, m, x, D, DB)
        if len(g.exits) == 1:
            return next(iter(g.exits))
        if not g.exits:
            return (m, x)
        dmx = self.diz.mx.get(Diz.key(f.key[0], (site + (3 if kind.startswith('jsr') else 4)) & 0xFFFF))
        if dmx is not None and dmx in g.exits:
            return dmx
        return (m, x) if (m, x) in g.exits else sorted(g.exits)[0]


# ---------------------------------------------------------------------------
# Policy parsing (src/) so the audit tracks the current classification.
# ---------------------------------------------------------------------------
def _cfunc(text, name):
    """Body of a C function definition (brace-matched)."""
    m = re.search(r'\b' + re.escape(name) + r'\([^;{]*\)\s*\{', text)
    if not m:
        return None
    i = m.end()
    depth = 1
    while depth and i < len(text):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def _cases(body):
    return sorted({int(v, 16) for v in re.findall(r'case (0x[0-9a-fA-F]+):', body or '')})


def parse_policy(src):
    """Read the classification the C policy currently applies. Every value
    used by the cross-reference comes from here; parse failures are reported
    in policy.errors and make the affected column unreliable."""
    out = dict(boss=[], scripted=[], world_script={}, errors=[])
    try:
        t = open(os.path.join(src, 'mmx_wide_policy.c')).read()
        body = _cfunc(t, 'MmxWidePolicy_IsBossEncounter')
        out['boss'] = _cases(body)
        out['boss_names'] = {int(a, 16): b.strip() for a, b in re.findall(
            r'case (0x[0-9a-fA-F]+):\s*/\*\s*(.*?)\s*\*/', body or '')}
        sbody = _cfunc(t, 'MmxWidePolicy_IsScriptedEncounter')
        out['scripted_function'] = 'MmxWidePolicy_IsScriptedEncounter' if sbody else 'MmxWidePolicy_IsBossEncounter'
        out['scripted'] = sorted(set(out['boss']) | set(_cases(sbody)))
        body = _cfunc(t, 'MmxWidePolicy_SpawnRecordAllowed') or ''
        mm = re.search(r'kind == 3 && \(MmxWidePolicy_Is\w+\(object_id\)((?:\s*\|\|\s*object_id == 0x[0-9a-fA-F]+)*)',
                       body)
        if mm:
            out['ws_native_kind3_extra'] = sorted({int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', mm.group(1))})
            out['ws_native_kind3_uses'] = re.search(r'MmxWidePolicy_Is\w+', mm.group(0)).group(0)
        else:
            out['errors'].append('SpawnRecordAllowed kind-3 native clause not recognised')
        body = re.sub(r'\s+', ' ', (_cfunc(t, 'MmxWidePolicy_ForceNativeSpawnTiming') or '').strip())
        out['force_native_spawn_timing'] = body
        a = re.search(r'(0x[0-9a-fA-F]+) - lookahead', body)
        b = re.search(r'stage == (\d+) && camera >= start && camera <= (0x[0-9a-fA-F]+)', body)
        if a and b:
            out['force_native'] = dict(stage=int(b.group(1)), start_base=int(a.group(1), 16),
                                       end=int(b.group(2), 16))
        else:
            out['errors'].append('ForceNativeSpawnTiming shape changed; its suppression is not modelled')
    except OSError as e:
        out['errors'].append('mmx_wide_policy.c: %r' % (e,))
    try:
        t = open(os.path.join(src, 'mmx_coop.c')).read()
        name = 'world_script_object' if _cfunc(t, 'world_script_object') else 'vile_script_object'
        body = _cfunc(t, name)
        if body is None:
            out['errors'].append('mmx_coop.c: no world/vile_script_object')
        else:
            out['world_script_function'] = name
            ws = collections.defaultdict(set)
            cls_body = _cfunc(t, 'world_script_class')
            if cls_body and 'world_script_class(' in body:
                for c in re.findall(r'c==(0x[0-9a-fA-F]+)', cls_body):
                    ws['any'].add(int(c, 16))
                ws['any'].update(_cases(cls_body))
            eff_body = _cfunc(t, 'world_script_effect')
            if eff_body and 'world_script_effect(' in body:
                for c in re.findall(r'c==(0x[0-9a-fA-F]+)', eff_body):
                    ws['effects_pool_any'].add(int(c, 16))
            # Conditional enemy classes written inline (e.g. ridden RT-55J).
            for c in re.findall(r'g_ram\[d\+10\]==(0x[0-9a-fA-F]+) && armor_ridden', body):
                ws['any'].add(int(c, 16))
            # Lock-follows-holder: lock-capable objects keep a held seat.
            lc = _cfunc(t, 'lock_capable') or ''
            follow = collections.defaultdict(set)
            pools = (('0xe68', 'enemies'), ('0x1428', 'effects'), ('0x1628', 'items'),
                     ('0x1928', 'decor'), ('0x1d08', 'events'))
            cur = None
            for line in lc.split('\n'):
                cur = next((p for k, p in pools if 'd>=%s' % k in line), cur)
                vals = [int(v, 16) for v in re.findall(r'(?:case |c==)(0x[0-9a-fA-F]+)', line)]
                if cur and vals:
                    follow[cur].update(vals)
            out['lock_follow'] = {k: sorted(v) for k, v in follow.items()}
            out['lock_follow_hooked'] = bool(re.search(r'if\(lock_capable\(cpu->D\)\)', _cfunc(t, 'view_actor_hook') or ''))
            # Classes with dedicated seat ownership: Gulpfer/bounce pad keep
            # their captured seat in .3F; Storm Eagle's lift ($48) has its
            # own rider hook ($87:C0AE/C0B4).
            owned = {int(v, 16) for v in re.findall(r'==(0x[0-9a-fA-F]+)', _cfunc(t, 'couch_nearest_target') or '')}
            if re.search(r'0x87c0ae\s*,\s*eagle_lift_hook', t):
                owned.add(0x48)
            out['dedicated_ownership'] = sorted(owned)
            stage = 'any'
            for line in body.split('\n'):
                s = re.search(r'g_ram\[0x1f7a\]\s*(==|!=)\s*(\d+)', line)
                if s:
                    stage = s.group(2)
                if 'd>=0x1428' in line:
                    for c in re.findall(r'g_ram\[d\+10\]==(0x[0-9a-fA-F]+)', line):
                        ws['effects_pool_stage_%s' % stage].add(int(c, 16))
                    continue
                for c in re.findall(r'\bc==(0x[0-9a-fA-F]+)', line):
                    ws[stage].add(int(c, 16))
            out['world_script'] = {k: sorted(v) for k, v in ws.items()}
            hook = _cfunc(t, 'view_actor_hook') or ''
            out['projection_hook_skips'] = name if re.search(r'if\(%s\(cpu->D\)\) return;' % name, hook) \
                else 'pattern not found'
        body = _cfunc(t, 'scene_tick') or ''
        m = re.search(r'if\(!state\.scene_owner &&[^;]*?begin_scene\(r\);', body, re.S)
        if m:
            expr = m.group(0)
            out['scene_trigger_flags'] = sorted({int(v, 16) for v in re.findall(r'r\[0x(1f[0-9a-f]{2})\]', expr)})
            gated = re.search(r'r\[0xc16\] && \(([^)]*)\)', expr)
            out['scene_trigger_flags_need_c16'] = sorted({int(v, 16) for v in re.findall(
                r'r\[0x(1f[0-9a-f]{2})\]', gated.group(1))}) if gated else []
            out['scene_trigger_expr'] = re.sub(r'\s+', ' ', expr)
        else:
            out['errors'].append('scene_tick begin_scene trigger not recognised')
        body = _cfunc(t, 'boss_fight')
        out['coop_boss_fight_extra'] = sorted({int(v, 16) for v in re.findall(r'c==(0x[0-9a-fA-F]+)', body or '')})
    except OSError as e:
        out['errors'].append('mmx_coop.c: %r' % (e,))
    try:
        t = open(os.path.join(src, 'mmx_rtl.c')).read()
        m = re.search(r'bool scripted=(.*?);', t)
        out['coop_view_scripted_expr'] = m.group(1) if m else None
        out['coop_view_force_native'] = bool(m and 'MmxViewNativeTiming' in m.group(1))
        if out['coop_view_force_native']:
            body = _cfunc(t, 'MmxViewNativeTiming') or ''
            mm = re.search(r'lookahead=(\d+)\+', body)
            out['coop_view_force_native_lookahead_base'] = int(mm.group(1)) if mm else None
        m = re.search(r'MmxCoopViewContains\(g_ram,coop,x,y,(.*?)\);', t, re.S)
        out['coop_view_rect_expr'] = re.sub(r'\s+', ' ', m.group(1)) if m else None
    except OSError as e:
        out['errors'].append('mmx_rtl.c: %r' % (e,))
    return out


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom', default=DEFAULT_ROM)
    ap.add_argument('--decoder', default=DEFAULT_DECODER, help='path to snes65816.py (or its directory)')
    ap.add_argument('--symbols', default=os.path.join(ROOT, 'symbols'))
    ap.add_argument('--src', default=os.path.join(ROOT, 'src'))
    ap.add_argument('--out', required=True, help='JSON output path')
    ap.add_argument('--library-threshold', type=int, default=4,
                    help='a function reached by more than this many distinct class handlers is library code')
    ap.add_argument('--generic-threshold', type=int, default=16,
                    help='library functions shared by more handlers than this are generic (reported, not flagged)')
    ap.add_argument('--table-cap', type=int, default=128)
    ap.add_argument('--ws-extra', type=int, default=72,
                    help='widescreen/co-op side margin in px ((width-256)/2 rounded up to 8; 16:9 -> 72)')
    ap.add_argument('--policy-baseline', action='store_true',
                    help='ignore IsScriptedEncounter / MmxViewNativeTiming extensions (origin/main classification)')
    ap.add_argument('--wram-check', default=None,
                    help='replay .sav holding a stage WRAM image (validates the record decoding)')
    ap.add_argument('--wram-offset', type=int, default=166269)
    args = ap.parse_args()

    rom = Rom(args.rom)
    s = load_decoder(args.decoder)
    dec = Dec(rom, s)
    diz = Diz(args.symbols)
    policy = parse_policy(args.src)
    if args.policy_baseline:
        policy['scripted'] = list(policy['boss'])
        policy['ws_native_kind3_uses'] = 'MmxWidePolicy_IsBossEncounter (forced by --policy-baseline)'
        policy['coop_view_force_native'] = False
        policy['baseline'] = True
    out = dict(tool='tools/audit_object_scripts.py', rom=dict(path=args.rom, sha1=rom.sha1,
               usa=rom.sha1 == USA_SHA1, copier_header=rom.header), diz_available=diz.ok,
               parameters=dict(library_threshold=args.library_threshold,
                               generic_threshold=args.generic_threshold, table_cap=args.table_cap,
                               ws_extra=args.ws_extra), policy=policy)
    if rom.sha1 != USA_SHA1:
        print('warning: ROM SHA-1 %s is not the USA 1.0 image this audit was written for' % rom.sha1)

    # ---- pools
    pools = []
    for name, entry, frozen in POOL_LOOPS:
        p = parse_pool_loop(dec, name, entry)
        if frozen is not None:
            q = parse_pool_loop(dec, name, frozen)
            diff = {k: '$%04X' % q[k] for k in ('base', 'end', 'step', 'table') if q[k] != p[k]}
            p['freeze_variant'] = dict(loop=q['loop'], differs=diff)
        ents, diz_n = read_class_table(rom, diz, p['table'])
        p['classes'] = len(ents)
        p['diz_table_entries'] = diz_n
        p['entries'] = ents
        pools.append(p)
    ktbl, kinds = parse_kind_allocators(dec, rom)
    kind_pool = {}
    for k, v in kinds.items():
        for p in pools:
            if p['base'] == v['base'] and p['end'] == v['end'] and p['step'] == v['step']:
                kind_pool[k] = p['name']
        v['pool'] = kind_pool.get(k)
    out['pools'] = [{k: (('$%04X' % v) if k in ('base', 'end', 'step', 'table') else v)
                     for k, v in p.items() if k != 'entries'} for p in pools]
    out['spawn_kinds'] = dict(table=fa(0x82, ktbl), kinds={str(k): v for k, v in kinds.items()})

    # ---- walk every class root
    W = Walker(rom, dec, diz, args.table_cap)
    roots = []
    for p in pools:
        for cls, stub in enumerate(p['entries']):
            f = W.func(0x80, stub, 1, 1, 'OBJ', None)
            mains = sorted({c['callee'][:2] for c in f.calls.values() if c['kind'] in ('jsl', 'jml')})
            roots.append(dict(pool=p['name'], cls=cls, stub=stub, key=f.key, mains=mains))

    def closure(k):
        seen = set()
        st = [k]
        while st:
            c = st.pop()
            if c in seen:
                continue
            seen.add(c)
            g = W.funcs.get(c)
            if g:
                for cl in g.calls.values():
                    st.append(cl['callee'])
        return seen

    # share = distinct main handlers reaching a function address
    reach = {}
    for r in roots:
        r['closure'] = closure(r['key'])
        ident = tuple(r['mains']) if r['mains'] else (('stub', r['stub']),)
        for k in r['closure']:
            reach.setdefault(k[:2], set()).add(ident)
    share = {a: len(v) for a, v in reach.items()}

    # ---- helper family (derived)
    hb, hlo, hhi = HELPER_WINDOW
    helper = {}
    for k, g in W.funcs.items():
        if k[0] == hb and hlo <= k[1] < hhi:
            ws = [e for e in g.effects.values() if e['type'] == 'write' and 'player_lock' in e['cats']]
            if ws:
                h = helper.setdefault(k[1], dict(entry=fa(hb, k[1]), writes=set()))
                for e in ws:
                    h['writes'].add('%s %s=%s' % (e['op'], h16(e['addr']), h16(e['value']) if e['value'] is not None else 'A'))
    for a, h in helper.items():
        w = h['writes']
        sets_c16 = any('$0C16' in s_ for s_ in w)
        zero_all = all(s_.endswith('=$0000') or s_.startswith('STZ') for s_ in w)
        h['role'] = 'release' if zero_all else ('lock' if sets_c16 else 'pose')
        if any('=$0C12' in s_ or '=$0C15' in s_ for s_ in w) or any(s_.startswith('STA $0BAA=A') for s_ in w):
            h['role'] = 'restore/release' if any(s_.startswith('STZ $0C16') for s_ in w) else h['role']
        h['writes'] = sorted(w)
    out['helper_family'] = {fa(hb, a): v for a, v in sorted(helper.items())}

    # ---- per-class summaries, effects tiered by how widely a function is shared
    T_OWN, T_GEN = args.library_threshold, args.generic_threshold

    def tier(sh):
        return 'own' if sh <= T_OWN else ('family' if sh <= T_GEN else 'generic')

    classes = []
    lib_funcs = {}
    for r in roots:
        effects, helper_calls, unresolved, lib_edges = [], [], [], set()
        for k in sorted(r['closure'], key=lambda q: (q[0], q[1], q[2], q[3], str(q[4]))):
            g = W.funcs.get(k)
            if g is None:
                continue
            sh = share.get(k[:2], 0)
            tr = tier(sh)
            for cl in g.calls.values():
                ck = cl['callee']
                if ck[0] == hb and ck[1] in helper:
                    helper_calls.append(dict(site=cl['site'], helper=fa(hb, ck[1]), role=helper[ck[1]]['role'],
                                             tier=tr, from_func=fa(k[0], k[1])))
                elif tr == 'own' and cl['kind'] in ('jsl', 'jml') and tier(share.get(ck[:2], 0)) != 'own':
                    lib_edges.add('%s->%s(%d)' % (cl['site'], fa(ck[0], ck[1]), share.get(ck[:2], 0)))
            if k[0] == hb and k[1] in helper:
                continue
            for e in g.effects.values():
                effects.append(dict(e, func=fa(k[0], k[1]), share=sh, tier=tr))
            if tr != 'generic':
                unresolved.extend(g.unresolved.values())
            if tr != 'own' and g.effects:
                lib_funcs.setdefault(fa(k[0], k[1]), dict(share=sh, tier=tr, effects=list(g.effects.values()),
                                                          classes=set()))['classes'].add(
                    '%s:$%02X' % (r['pool'], r['cls']))
        name = None
        for (b, a) in r['mains']:
            name = name or diz.label(b, a)
        if r['pool'] == 'enemies' and r['cls'] in policy.get('boss_names', {}):
            name = policy['boss_names'][r['cls']]
        seen_e = set()
        uniq = []
        for e in sorted(effects, key=lambda e: (e['pc'], e['type'])):
            kk = (e['pc'], e['type'], e.get('addr'), e.get('value'), e.get('rhs_imm'), e.get('rhs_addr'))
            if kk not in seen_e:
                seen_e.add(kk)
                uniq.append(e)
        hseen = set()
        hc = []
        for h in helper_calls:
            if (h['site'], h['helper']) not in hseen:
                hseen.add((h['site'], h['helper']))
                hc.append(h)
        classes.append(dict(pool=r['pool'], cls=r['cls'], cls_hex='$%02X' % r['cls'], stub=fa(0x80, r['stub']),
                            handlers=[fa(b, a) for b, a in r['mains']], name=name, functions=len(r['closure']),
                            effects=uniq, helper_calls=hc, library_edges=sorted(lib_edges),
                            unresolved=unresolved, _closure=r['closure']))

    # ---- code-spawned children
    allocators, spawns = find_spawns(W, dec, rom, pools)
    out['allocators'] = allocators
    for c in classes:
        sp = set()
        for k in c['_closure']:
            if tier(share.get(k[:2], 0)) != 'generic':   # e.g. the shared death-drop spawner
                sp |= spawns.get(k[:2], set())
        c['spawns'] = sorted('%s:$%02X@%s' % s_ for s_ in sp)
        c['_spawn_keys'] = sorted({(s_[0], s_[1]) for s_ in sp})

    # ---- table-driven stores (generic pattern) and the camera-region system
    table_stores = find_table_stores(W, dec)
    out['table_driven_stores'] = [dict(func=fkey_str(k), site=fa(k[0], s_), addr_table=h16(a), value_table=h16(v))
                                  for (k, s_, a, v) in table_stores]
    regions = decode_camera_regions(W, dec, rom, diz, table_stores)
    out['camera_region_system'] = {k: v for k, v in regions.items()
                                   if k not in ('by_stage_sub', 'region', 'executor_key')}
    region_classes = set()
    if regions.get('executor'):
        ek = regions['executor_key']
        for c in classes:
            if any(k[:2] == ek for k in c['_closure']):
                region_classes.add((c['pool'], c['cls']))
                c['effects'].append(dict(type='write_table', pc=fa(*ek), addr=None, cats=['camera_bounds'],
                                         func=fa(*ek), share=share.get(ek, 0), tier='own',
                                         note='camera-region commands via %s; per-record rectangles/commands '
                                              'in records[].camera_region' % regions['executor']))
    out['camera_region_system']['classes'] = sorted('%s:$%02X' % rc for rc in region_classes)

    def eff_cats(c, tiers=('own', 'family')):
        cats = collections.Counter()
        for e in c['effects']:
            if e['tier'] in tiers and e['type'] in ('write', 'write_indexed', 'write_table'):
                for cat in e['cats']:
                    cats[cat] += 1
        for h in c['helper_calls']:
            if h['tier'] in tiers:
                cats['helper_' + h['role']] += 1
        return cats

    for c in classes:
        own = eff_cats(c)
        c['write_categories'] = dict(own)
        c['generic_write_categories'] = dict(eff_cats(c, ('generic',)))
        c['lock'] = bool(own.get('player_lock') or any(own.get('helper_' + r_) for r_ in
                                                         ('lock', 'release', 'restore/release')))
        c['pose'] = bool(own.get('helper_pose'))
        c['camera'] = bool(own.get('camera_bounds'))
        c['scene'] = bool(own.get('scene'))
        c['moves_player'] = bool(own.get('player_pos'))
        c['camera_reads'] = sorted({h16(e['addr'] if e['type'] == 'read' else e['lhs']) for e in c['effects']
                                    if e['tier'] in ('own', 'family') and e['type'] in ('read', 'compare')
                                    and WATCH.get(e['addr'] if e['type'] == 'read' else e['lhs'])
                                    == 'camera_bounds'}
                                   | {h16(e['rhs_addr']) for e in c['effects']
                                      if e['tier'] in ('own', 'family') and e['type'] == 'compare'
                                      and e.get('rhs_addr') is not None
                                      and WATCH.get(e['rhs_addr']) == 'camera_bounds'})
        c['player_x_triggers'] = sorted({e['rhs_imm'] for e in c['effects'] if e['type'] == 'compare'
                                         and e['tier'] in ('own', 'family') and e.get('lhs') == 0x0BAD
                                         and e.get('rhs_imm') is not None})

    cls_by0 = {(c['pool'], c['cls']): c for c in classes}
    for c in classes:
        seen = set()
        st_ = list(c['_spawn_keys'])
        kids = []
        while st_:
            kk = st_.pop()
            if kk in seen or kk == (c['pool'], c['cls']):
                continue
            seen.add(kk)
            ch = cls_by0.get(kk)
            if ch is None:
                continue
            if ch['lock'] or ch['camera'] or ch['pose']:
                kids.append('%s:%s%s%s%s' % (kk[0], ch['cls_hex'], ' lock' if ch['lock'] else '',
                                             ' camera' if ch['camera'] else '', ' pose' if ch['pose'] else ''))
            st_.extend(ch['_spawn_keys'])
        c['scripting_descendants'] = sorted(kids)

    # ---- stages and records
    ptr_tbl = parse_stage_pointer_table(dec, rom)
    nstages = (rom.word(0x85, ptr_tbl) - ptr_tbl) // 2
    stages = [parse_stage(rom, ptr_tbl, st) for st in range(nstages)]
    out['stage_lists'] = dict(pointer_table=fa(0x85, ptr_tbl), slots=nstages,
                              stages=[dict(stage=s_['stage'], list=s_['list'], end=s_['end'],
                                           records=len(s_['records']), unused=s_.get('unused', False))
                                      for s_ in stages])
    if args.wram_check:
        d = open(args.wram_check, 'rb').read()
        wram = d[args.wram_offset:args.wram_offset + 0x20000]
        st = wram[0x1F7A]
        out['wram_validation'] = dict(file=args.wram_check, stage=st, **validate_wram(stages[st], wram))

    cls_by = {(c['pool'], c['cls']): c for c in classes}
    pool_of_kind = {k: v['pool'] for k, v in kinds.items()}
    boss = set(policy['boss'])
    scripted = set(policy['scripted'])
    ws_native = (scripted if policy.get('ws_native_kind3_uses', '').endswith('ScriptedEncounter') else boss) \
        | set(policy.get('ws_native_kind3_extra', []))
    wso = policy['world_script']
    records = []
    for st in stages:
        for rec in st['records']:
            rec = dict(rec)
            rec['pool'] = pool_of_kind.get(rec['kind'])
            c = cls_by.get((rec['pool'], rec['cls']))
            rec['class_name'] = c['name'] if c else None
            if (rec['pool'], rec['cls']) in region_classes:
                key_ = (rec['stage'], rec['sub'])
                if key_ not in regions['by_stage_sub']:
                    regions['by_stage_sub'][key_] = regions['region'](*key_)
                rec['camera_region'] = regions['by_stage_sub'][key_]
            records.append(rec)
            if c is not None:
                c.setdefault('record_stages', set()).add(rec['stage'])
                c['records'] = c.get('records', 0) + 1
    out['records'] = records

    # ---- (a) scripting enemy classes; the look-ahead treats unclassified ones as ordinary
    flagged_a = []
    for c in classes:
        if c['pool'] != 'enemies' or not (c['lock'] or c['camera'] or c['pose'] or c['scripting_descendants']):
            continue
        recs = [r for r in records if r['pool'] == 'enemies' and r['cls'] == c['cls']]
        flagged_a.append(dict(cls=c['cls_hex'], name=c['name'], handlers=c['handlers'], lock=c['lock'],
                              camera=c['camera'], camera_reads=c['camera_reads'], pose=c['pose'], scene=c['scene'],
                              moves_player=c['moves_player'], scripting_descendants=c['scripting_descendants'],
                              spawns=c['spawns'],
                              boss_classified=c['cls'] in boss, scripted_classified=c['cls'] in scripted,
                              ws_native_pass=c['cls'] in ws_native, coop_view_scripted=c['cls'] in scripted,
                              unclassified_with_records=bool(recs) and c['cls'] not in scripted,
                              records=['st%d %s x=$%04X y=$%04X' % (r['stage'], r['desc'], r['x'], r['y'])
                                       for r in recs],
                              evidence=evidence(c)))
    out['a_scripting_enemy_classes'] = flagged_a

    # ---- (b) ordering hazards
    fn = policy.get('force_native', {})
    W_coop_r, W_coop_l = 320 + args.ws_extra, 64 + args.ws_extra
    W_ws_r, W_ws_l = 256 + args.ws_extra + 32, args.ws_extra + 32
    out['b_parameters'] = dict(
        coop_right='early when x - view.x < %d (320+extra); native when x&~31 <= camera+256' % W_coop_r,
        coop_left='early when view.x - x <= %d (64+extra); native when camera <= x|31' % W_coop_l,
        ws_right='wide anchor camera+256+%d' % (args.ws_extra + 32), ws_left='wide anchor camera-%d' % W_ws_l,
        trigger_activation='region: player enters rect (camera ~= player-128); player_x>=imm: camera ~= imm-128; '
                           'on_spawn: native scan reaches its column (early-eligible enemies: their early column)',
        hazard='trigger activates natively after the record early-spawns but no later than its native spawn',
        y_window='record y within trigger y-range +/- $100 (records +/- $100 of the trigger record)')

    def triggers_for(stage):
        out_t = []
        for t in records:
            if t['stage'] != stage:
                continue
            c = cls_by.get((t['pool'], t['cls']))
            if c is None:
                continue
            early = t['pool'] == 'enemies' and t['cls'] not in scripted
            if t.get('camera_region'):
                rg = t['camera_region']
                cams = [w for w in rg['writes'] if WATCH.get(int(w['addr'][1:], 16)) == 'camera_bounds']
                if cams:
                    out_t.append(dict(rec=t, how='region %s' % rg['rect'], act_r=rg['xmin'], act_l=rg['xmax'] - 1,
                                      ylo=rg['ymin'], yhi=rg['ymax'], early=False,
                                      writes=['%s=%s' % (w['addr'], w['value']) for w in cams]))
            elif c['camera']:
                if c['player_x_triggers']:
                    for px in c['player_x_triggers']:
                        out_t.append(dict(rec=t, how='player_x>=$%04X' % px, act_r=px, act_l=px,
                                          ylo=t['y'] - 0x100, yhi=t['y'] + 0x100,
                                          writes=camera_writes(c), early=early))
                else:
                    out_t.append(dict(rec=t, how='on_spawn_or_contact', act_r=None, act_l=None,
                                      ylo=t['y'] - 0x100, yhi=t['y'] + 0x100, writes=camera_writes(c),
                                      early=early))
        return out_t

    trig_cache = {}
    hazards_all = []
    for r in records:
        if r['pool'] != 'enemies' or r['cls'] in scripted:
            continue
        rc = cls_by[(r['pool'], r['cls'])]
        if r['stage'] not in trig_cache:
            trig_cache[r['stage']] = triggers_for(r['stage'])
        for mode in ('coop', 'ws'):
            if mode == 'ws' and r['cls'] in ws_native:
                continue
            Wr, Wl = (W_coop_r, W_coop_l) if mode == 'coop' else (W_ws_r, W_ws_l)
            for tg in trig_cache[r['stage']]:
                t = tg['rec']
                if t is r or not (tg['ylo'] - 0x100 <= r['y'] <= tg['yhi'] + 0x100):
                    continue
                for direction in ('right', 'left'):
                    if direction == 'right':
                        early_cam = r['x'] - Wr
                        native_cam = (r['x'] & ~31) - 256
                        if tg['act_r'] is not None:
                            tcam = tg['act_r'] - 128
                        elif tg['early']:
                            tcam = t['x'] - Wr
                        else:
                            tcam = (t['x'] & ~31) - 256
                        hit = early_cam < tcam <= native_cam
                        contact = tg['act_r'] is not None or (early_cam < t['x'] - 128 <= native_cam)
                    else:
                        early_cam = r['x'] + Wl
                        native_cam = r['x'] | 31
                        if tg['act_l'] is not None:
                            tcam = tg['act_l'] - 128
                        elif tg['early']:
                            tcam = t['x'] + Wl
                        else:
                            tcam = t['x'] | 31
                        hit = native_cam <= tcam < early_cam
                        contact = tg['act_l'] is not None or (native_cam <= t['x'] - 128 < early_cam)
                    if not hit:
                        continue
                    suppressed = False
                    if fn and r['stage'] == fn.get('stage') and (mode == 'ws' or policy.get('coop_view_force_native')):
                        la = args.ws_extra + (32 if mode == 'ws' else
                                              (policy.get('coop_view_force_native_lookahead_base') or 0))
                        lo = max(0, fn['start_base'] - la)
                        suppressed = lo <= early_cam <= fn['end']
                    hazards_all.append(dict(
                        mode=mode, direction=direction, stage=r['stage'], record=r['desc'], cls='$%02X' % r['cls'],
                        name=r['class_name'], x='$%04X' % r['x'], y='$%04X' % r['y'],
                        record_scripts=bool(rc['lock'] or rc['camera'] or rc['pose']),
                        record_camera=bool(rc['camera'] or rc['camera_reads']),
                        trigger=t['desc'], trigger_pool=t['pool'], trigger_cls='$%02X' % t['cls'],
                        trigger_name=t['class_name'], trigger_x='$%04X' % t['x'], trigger_y='$%04X' % t['y'],
                        trigger_how=tg['how'], trigger_writes=tg['writes'],
                        cam_early='$%04X' % (early_cam & 0xFFFF), cam_trigger='$%04X' % (tcam & 0xFFFF),
                        cam_native='$%04X' % (native_cam & 0xFFFF), force_native_suppresses=suppressed,
                        confidence='likely' if contact else 'only_if_trigger_acts_on_spawn'))
    # A scripting record that waits for the player to reach an X threshold
    # (CMP #imm against $0BAD) and locks the body: natively the player is near
    # camera+128 when the record's column enters at camera+256. An early spawn
    # can lock the body short of the threshold (e.g. a door still between them).
    pre = []
    for r in records:
        if r['pool'] != 'enemies' or r['cls'] in scripted:
            continue
        rc = cls_by[(r['pool'], r['cls'])]
        if not (rc['lock'] and rc['player_x_triggers']):
            continue
        native_player = (r['x'] & ~31) - 256 + 128
        for mode, Wr in (('coop', W_coop_r), ('ws', W_ws_r)):
            if mode == 'ws' and r['cls'] in ws_native:
                continue
            early_cam = r['x'] - Wr
            early_player = early_cam + 128
            for thr in rc['player_x_triggers']:
                if early_player < thr <= native_player + 32 and thr <= r['x'] + 0x200:
                    suppressed = False
                    if fn and r['stage'] == fn.get('stage') and (mode == 'ws' or policy.get('coop_view_force_native')):
                        la = args.ws_extra + (32 if mode == 'ws' else
                                              (policy.get('coop_view_force_native_lookahead_base') or 0))
                        suppressed = max(0, fn['start_base'] - la) <= early_cam <= fn['end']
                    pre.append(dict(mode=mode, stage=r['stage'], record=r['desc'], cls='$%02X' % r['cls'],
                                    name=r['class_name'], x='$%04X' % r['x'], threshold='$%04X' % thr,
                                    native_player_est='$%04X' % native_player,
                                    early_player_est='$%04X' % (early_player & 0xFFFF),
                                    early_camera='$%04X' % (early_cam & 0xFFFF),
                                    force_native_suppresses_at_that_camera=suppressed))
    out['b_player_x_preconditions'] = pre
    out['b_ordering_hazards'] = [h for h in hazards_all if h['record_camera']]
    out['b_ordering_hazards_lock_only'] = [h for h in hazards_all if h['record_scripts'] and not h['record_camera']]
    out['b_ordering_hazards_any_enemy'] = hazards_all

    # ---- (c) world-body scripting under nearest-player projection
    trig = set(policy.get('scene_trigger_flags', []))
    need = set(policy.get('scene_trigger_flags_need_c16', []))

    def scene_set(c):
        return sorted({h16(e['addr']) for e in c['effects'] if e['tier'] in ('own', 'family')
                       and e['type'] == 'write' and e['addr'] in trig
                       and (e.get('value') not in (0, None) or e['op'] in ('INC', 'TSB'))})

    def scene_covers(c):
        s_ = {int(v[1:], 16) for v in scene_set(c)}
        return bool(s_ - need) or bool(s_ & need and c['lock'])

    flagged_c = []
    for c in classes:
        if c['pool'] not in ('enemies', 'effects'):
            continue
        if not (c['lock'] or c['pose'] or c['moves_player']):
            continue
        sts = sorted(c.get('record_stages', ()))
        if c['pool'] == 'enemies':
            cov_any = sorted(k for k, v in wso.items() if (k.isdigit() or k == 'any') and c['cls'] in v)
        else:
            cov_any = sorted(k for k, v in wso.items() if k.startswith('effects_pool') and c['cls'] in v)
        flagged_c.append(dict(pool=c['pool'], cls=c['cls_hex'], name=c['name'], handlers=c['handlers'],
                              record_stages=sts, world_script_stages=cov_any,
                              uncovered_record_stages=[s_ for s_ in sts if 'any' not in cov_any
                                                       and str(s_) not in cov_any]
                              if c['pool'] == 'enemies' else None,
                              boss_classified=c['pool'] == 'enemies' and c['cls'] in boss,
                              scripted_classified=c['pool'] == 'enemies' and c['cls'] in scripted,
                              lock=c['lock'], pose=c['pose'], moves_player=c['moves_player'],
                              scene_flags_set=scene_set(c),
                              scene_flags_could_begin_scene=scene_covers(c),
                              spawned_by=sorted('%s:%s' % (o['pool'], o['cls_hex']) for o in classes
                                                if (c['pool'], c['cls']) in {tuple((s_.split('@')[0].split(':')[0],
                                                    int(s_.split('@')[0].split(':')[1][1:], 16)))
                                                    for s_ in o['spawns']}),
                              evidence=evidence(c)))
    out['c_projection_candidates'] = flagged_c
    for c in flagged_c:
        # Coverage = world_script_object or scripted/boss classification (as the
        # audit was specified). Scene flags are listed as possible mitigation
        # only: the walk does not order them against the lock/release pair.
        # Projection coverage: the object runs against the world actor
        # (world_script_object), or a lock it sets keeps its holder projected
        # (lock_capable + forced_seat). Spawn-timing classification does not
        # decide projection and no longer counts.
        follow = set(policy.get('lock_follow', {}).get(c['pool'], ())) if policy.get('lock_follow_hooked') else set()
        c['lock_follow'] = c['lock'] and int(c['cls'][1:], 16) in follow
        c['dedicated_ownership'] = c['pool'] == 'enemies' and \
            int(c['cls'][1:], 16) in policy.get('dedicated_ownership', ())
        c['uncovered'] = bool((c['lock'] or c['pose']) and not c['world_script_stages']
                              and not c['lock_follow'] and not c['dedicated_ownership'])

    for c in classes:
        c.pop('_closure', None)
        c.pop('_spawn_keys', None)
        c['record_stages'] = sorted(c.get('record_stages', ()))
    out['classes'] = classes
    out['library_functions'] = {k: dict(v, classes=sorted(v['classes'])) for k, v in sorted(lib_funcs.items())}
    out['jump_tables'] = sorted(({k: v for k, v in t.items() if k != 'targets'} for t in W.table_log.values()),
                                key=lambda t: t['table'])
    allu = collections.Counter()
    conflicts = []
    for g in W.funcs.values():
        for n in g.notes:
            if n.startswith('diz_mx_length_conflict@'):
                conflicts.append('%s in %s' % (n.split('@')[1], fkey_str(g.key)))
                continue
            allu[n.split(':')[0] if ':' in n else n] += 1
    unresolved_all = sorted({(u['pc'], u['why']) for g in W.funcs.values() for u in g.unresolved.values()})
    out['walker_stats'] = dict(functions=len(W.funcs), instructions=sum(len(g.insns) for g in W.funcs.values()),
                               jump_tables=len(W.table_log),
                               jump_table_stop_reasons=dict(collections.Counter(t['stop'] for t in W.table_log.values())),
                               notes=dict(allu),
                               diz_mx_disagreements=sum(g.diz_mx_mismatch for g in W.funcs.values()),
                               insns_in_diz_data=sum(len(g.diz_data_insns) for g in W.funcs.values()),
                               unresolved=len(unresolved_all),
                               unresolved_sites=['%s %s' % u for u in unresolved_all],
                               diz_mx_length_conflicts=sorted(conflicts))

    def enc(o):
        if isinstance(o, set):
            return sorted(o)
        if isinstance(o, tuple):
            return list(o)
        return str(o)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as fh:
        json.dump(out, fh, indent=1, default=enc)
    print_summary(out, rom, diz, args)


def camera_writes(c):
    return sorted({'%s=%s' % (h16(e['addr']), h16(e['value']) if e.get('value') is not None else '?')
                   for e in c['effects'] if e['tier'] in ('own', 'family') and e['type'] == 'write'
                   and 'camera_bounds' in e['cats']})


def evidence(c):
    ev = []
    for h in c['helper_calls']:
        if h['tier'] != 'generic':
            ev.append('%s JSL %s (%s)%s' % (h['site'], h['helper'], h['role'],
                                            '' if h['tier'] == 'own' else ' [family lib %s]' % h['from_func']))
    for e in c['effects']:
        if e['tier'] == 'generic':
            continue
        lib = '' if e['tier'] == 'own' else ' [family lib %s x%d]' % (e['func'], e['share'])
        if e['type'] == 'write' and set(e['cats']) & {'player_lock', 'camera_bounds', 'player_pos'}:
            ev.append('%s %s %s=%s%s' % (e['pc'], e['op'], h16(e['addr']),
                                         h16(e['value']) if e.get('value') is not None else '?', lib))
        elif e['type'] == 'write_indexed' and set(e['cats']) & {'player_lock', 'camera_bounds'}:
            ev.append('%s %s %s,%s%s' % (e['pc'], e['op'], h16(e['base']), e['how'][-1].upper(), lib))
        elif e['type'] == 'write_table':
            ev.append('%s %s' % (e['pc'], e['note']))
        elif e['type'] == 'compare' and set(e['cats']) & {'camera_bounds'}:
            ev.append('%s CMP %s vs %s%s' % (e['pc'], h16(e['lhs']),
                                             h16(e['rhs_addr']) if e.get('rhs_addr') is not None
                                             else '#' + h16(e['rhs_imm']), lib))
    return ev


def find_spawns(W, dec, rom, pools):
    """Code-spawned objects: JSL <pool allocator>; ...; LDA #cls; STA $000A,X.
    Allocators are JSL targets that begin REP #$30; LDX #base; LDA $0000,X;
    BEQ (the $82:82B9.. family); base selects the pool."""
    by_base = {}
    for p_ in pools:
        by_base.setdefault(p_['base'], p_['name'])
    alloc = {}
    for g in W.funcs.values():
        for cl in g.calls.values():
            ck = cl['callee']
            if ck[:2] in alloc or cl['kind'] not in ('jsl', 'jsr'):
                continue
            ins = linear(dec, ck[0], ck[1], 1, 1, 4)
            ops = [(i[1], i[2]) for i in ins]
            if len(ins) >= 4 and ops[0] == ('REP', 'IMM') and ins[0][3] & 0x30 == 0x30:
                ins = linear(dec, ck[0], ck[1], 0, 0, 4)
                ops = [(i[1], i[2]) for i in ins]
                if ops[1] == ('LDX', 'IMM') and ops[2] == ('LDA', 'ABS_X') and ins[2][3] == 0 and ops[3][0] == 'BEQ':
                    if ins[1][3] in by_base:
                        alloc[ck[:2]] = by_base[ins[1][3]]
    spawns = collections.defaultdict(set)   # func addr -> {(pool, cls, site)}
    for k, g in W.funcs.items():
        for cl in g.calls.values():
            ck = cl['callee']
            if ck[:2] not in alloc:
                continue
            b = k[0]
            site = int(cl['site'].split(':')[1], 16)
            a = (site + (4 if cl['kind'] == 'jsl' else 3)) & 0xFFFF
            pend = None
            for _ in range(12):
                i = dec.at(b, a, 1, 0)
                if i is None:
                    break
                mn, md, op, ln, _o = i
                if mn == 'LDA' and md == 'IMM':
                    pend = op
                elif mn == 'STA' and md in ('ABS_X', 'ABS_Y') and op == 0x000A and pend is not None:
                    spawns[k[:2]].add((alloc[ck[:2]], pend, fa(b, a)))
                    break
                elif mn in ('RTS', 'RTL', 'JMP', 'JSL', 'JSR', 'BRA', 'BRL'):
                    break
                elif mn in ('REP', 'SEP'):
                    if mn == 'REP' and op & 0x20:
                        break
                a = (a + ln) & 0xFFFF
    return {fa(*k): v for k, v in alloc.items()}, spawns


def find_table_stores(W, dec):
    """LDA tblA,Y ; TAX ; LDA tblB,Y ; STA $0000,X inside walked code."""
    found = []
    seen = set()
    for k, g in W.funcs.items():
        b = k[0]
        for pc in sorted(g.insns):
            if (k[:2], pc) in seen:
                continue
            for (m, x) in ((0, 0), (0, 1), (1, 0), (1, 1)):
                seq = linear(dec, b, pc, m, x, 4)
                if len(seq) < 4:
                    continue
                ops = [(i[1], i[2]) for i in seq]
                if ops == [('LDA', 'ABS_Y'), ('TAX', 'IMP'), ('LDA', 'ABS_Y'), ('STA', 'ABS_X')] and seq[3][3] == 0:
                    found.append((k, pc, seq[0][3], seq[2][3]))
                    seen.add((k[:2], pc))
                    break
    return found


def decode_camera_regions(W, dec, rom, diz, table_stores):
    """$81:F6DC-style executor: while the player is inside [xmin,xmax) x
    [ymin,ymax), each record command n stores word V[n-1] at address A[n-1]."""
    res = dict(executor=None)
    cands = [ts for ts in table_stores if ts[3] == ts[2] + 2]
    if not cands:
        res['error'] = 'no table-driven executor found'
        return res
    k, site, at, vt = cands[0]
    owners = [g.key for g in W.funcs.values() if g.key[0] == k[0] and site in g.insns and g.key[1] <= site]
    k = max(owners, key=lambda q: q[1])
    fkey = k[:2]
    ins = linear(dec, k[0], k[1], 1, 1, 12)
    if not any(i[1] == 'LDA' and i[2] == 'ABS' and i[3] == 0x0BAD for i in ins):
        res['error'] = 'executor %s does not test player X first' % fa(*fkey)
        return res
    # the state that indexes a per-stage list with $1F7A and the record's sub byte ($0B)
    regtbl = None
    for g in W.funcs.values():
        if g.key[0] != k[0] or regtbl:
            continue
        for pc in sorted(g.insns):
            i0 = dec.at(k[0], pc, 0, 0)
            if not (i0 and i0[0] == 'LDA' and i0[1] == 'DP' and i0[2] == 0x0B):
                continue
            seq = linear(dec, k[0], pc, 0, 0, 12)
            if any(i[1] == 'LDA' and i[2] == 'ABS' and i[3] == 0x1F7A for i in seq):
                for i in seq:
                    if i[1] == 'LDA' and i[2] == 'ABS_X':
                        regtbl = i[3]
                        break
            if regtbl:
                break
    db = diz.db.get(Diz.key(k[0], site)) if diz.ok else None
    db_src = 'diz-recorded DB at %s' % fa(k[0], site)
    if db is None and regtbl is not None:
        # No diz: pick the data bank where the per-stage lists and the
        # command address table are both plausible.
        ok = []
        for b in range(0x80, 0x80 + len(rom.data) // 0x8000):
            try:
                lists = [rom.word(b, regtbl + 2 * s_) for s_ in range(13)]
                cmds = [rom.word(b, at + 4 * i) for i in range(8)]
            except TypeError:
                continue
            if all(v >= 0x8000 for v in lists) and all(v < 0x2000 for v in cmds):
                ok.append(b)
        if len(ok) == 1:
            db, db_src = ok[0], 'derived (only bank with plausible region/command tables)'
    if regtbl is None or db is None:
        res['error'] = 'region table or data bank not derived (regtbl=%s db=%s)' % (regtbl, db)
        return res
    res.update(executor=fa(*fkey), executor_key=fkey,
               data_bank='$%02X (%s)' % (db, db_src),
               region_table=fa(db, regtbl), command_addr_table=fa(db, at), command_value_table=fa(db, vt),
               semantics='player x in [xmin,xmax) and y in [ymin,ymax) -> run command bytes until 0')
    by = {}

    def region(stage, sub):
        lst = rom.word(db, regtbl + 2 * stage)
        p = rom.word(db, lst + 2 * sub)
        if p < 0x8000:
            return dict(record=None, error='bad pointer $%04X' % p, writes=[], xmin=0, xmax=0, ymin=0, ymax=0,
                        rect='?')
        w = [rom.word(db, p + 2 * i) for i in range(4)]
        cmds = []
        q = p + 8
        while rom.byte(db, q) and len(cmds) < 16:
            cmds.append(rom.byte(db, q))
            q += 1
        writes = [dict(cmd=cv, addr='$%04X' % rom.word(db, at + (cv - 1) * 4),
                       value='$%04X' % rom.word(db, vt + (cv - 1) * 4)) for cv in cmds]
        return dict(record=fa(db, p), xmax=w[0], xmin=w[1], ymax=w[2], ymin=w[3],
                    rect='x[$%04X,$%04X) y[$%04X,$%04X)' % (w[1], w[0], w[3], w[2]), writes=writes)
    res['region'] = region
    res['by_stage_sub'] = by
    return res


def print_summary(out, rom, diz, args):
    print('ROM %s (USA=%s)  diz=%s' % (rom.sha1[:12], rom.sha1 == USA_SHA1, diz.ok))
    for p in out['pools']:
        fv = p.get('freeze_variant')
        print('pool %-12s base %s step %s slots %-2d table %s classes %-3d (diz %s)%s' % (
            p['name'], p['base'], p['step'], p['slots'], p['table'], p['classes'], p['diz_table_entries'],
            ('  freeze loop %s differs %s' % (fv['loop'], fv['differs'])) if fv and fv['differs'] else ''))
    sl = out['stage_lists']
    print('stage lists %s: %s' % (sl['pointer_table'], ' '.join(
        '%d:%d%s' % (s_['stage'], s_['records'], '(unused)' if s_['unused'] else '') for s_ in sl['stages'])))
    if 'wram_validation' in out:
        v = out['wram_validation']
        print('WRAM check (stage %d): %d table + %d flag entries, %d mismatches' % (
            v['stage'], v['table_entries'], v['flag_entries'], v['mismatch_count']))
    ws = out['walker_stats']
    print('walker: %d functions, %d insns, %d jump tables %s, %d unresolved sites' % (
        ws['functions'], ws['instructions'], ws['jump_tables'], ws['jump_table_stop_reasons'], ws['unresolved']))
    print('helper family:', ', '.join('%s %s' % (k, v['role']) for k, v in out['helper_family'].items()))
    cr = out['camera_region_system']
    print('camera regions: executor %s tables %s/%s/%s classes %s %s' % (
        cr.get('executor'), cr.get('region_table'), cr.get('command_addr_table'), cr.get('command_value_table'),
        cr.get('classes'), cr.get('error', '')))
    print('(a) enemy classes with lock/camera/pose effects:')
    for a in out['a_scripting_enemy_classes']:
        print('  %s %-30s lock=%d cam=%d pose=%d boss=%d scripted=%d wsNative=%d recs=%d%s' % (
            a['cls'], (a['name'] or '')[:30], a['lock'], a['camera'], a['pose'], a['boss_classified'],
            a['scripted_classified'], a['ws_native_pass'], len(a['records']),
            ('  children: ' + ', '.join(a['scripting_descendants'])) if a['scripting_descendants'] else ''))
    hz = out['b_ordering_hazards']
    print('(b) ordering hazards, records whose class writes/reads camera bounds: %d '
          '(lock/pose-only records: %d, any enemy: %d; JSON)' % (
              len(hz), len(out['b_ordering_hazards_lock_only']), len(out['b_ordering_hazards_any_enemy'])))
    for h in hz:
        print('  %-4s %-5s st%d %s %s x=%s <- %s %s:%s x=%s %s %s cam early/trig/native %s/%s/%s %s%s' % (
            h['mode'], h['direction'], h['stage'], h['record'], h['cls'], h['x'], h['trigger'], h['trigger_pool'],
            h['trigger_cls'], h['trigger_x'], h['trigger_how'], ','.join(h['trigger_writes'][:3]),
            h['cam_early'], h['cam_trigger'], h['cam_native'], h['confidence'],
            ' [ForceNativeSpawnTiming suppresses]' if h['force_native_suppresses'] else ''))
    print('(b2) lock + player-X precondition records reachable early: %d' % len(out['b_player_x_preconditions']))
    for h in out['b_player_x_preconditions']:
        print('  %-4s st%d %s %s x=%s waits $0BAD>=%s; player native~%s early~%s (camera %s)%s' % (
            h['mode'], h['stage'], h['record'], h['cls'], h['x'], h['threshold'], h['native_player_est'],
            h['early_player_est'], h['early_camera'],
            ' [ForceNativeSpawnTiming suppresses]' if h['force_native_suppresses_at_that_camera'] else ''))
    print('(c) lock/pose/move classes in projected pools:')
    for c in out['c_projection_candidates']:
        if not (c['lock'] or c['pose']):
            continue
        print('  %-7s %s %-26s st=%s worldScript=%s sceneFlags=%s boss=%d lock=%d pose=%d by=%s%s' % (
            c['pool'], c['cls'], (c['name'] or '')[:26], c['record_stages'], c['world_script_stages'],
            ','.join(c['scene_flags_set']) or '-', c['boss_classified'], c['lock'], c['pose'],
            ','.join(c['spawned_by']), '  UNCOVERED' if c['uncovered'] else ''))
    print('    (+%d move-player-only classes in JSON c_projection_candidates)' % sum(
        1 for c in out['c_projection_candidates'] if not (c['lock'] or c['pose'])))
    print('wrote', args.out)


if __name__ == '__main__':
    sys.setrecursionlimit(20000)
    main()
