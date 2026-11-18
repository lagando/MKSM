#!/usr/bin/env python3
# Variáveis globais gravadas só por código trocado por stub (ou alcançável só dele) e lidas por código do jogo:
# o padrão do bug do heap do IOP (ARQUITETURA.md §5). Não conhece as trocas feitas em tempo de execução
# (game_overrides_mksm.cpp). Uso:
#   llvm-objdump -d --triple=mipsel --mcpu=mips3 --no-show-raw-insn extraido_full/SLUS_210.87 > /tmp/ee.txt
#   python3 scripts/auditar_stubs.py sm.toml /tmp/ee.txt
import re, sys, bisect, collections
toml, dis = sys.argv[1], sys.argv[2]
lines = open(toml).read().split('\n')
def entries(lo, hi):
    out = {}
    for l in lines[lo:hi]:
        for name, a in re.findall(r'"([A-Za-z_0-9.$]+)@0x([0-9A-Fa-f]+)"', l):
            out[int(a, 16)] = name
    return out
st = next(i for i, l in enumerate(lines) if l.startswith('stubs = ['))
se = next(i for i in range(st, len(lines)) if lines[i].startswith(']'))
ep = next(i for i, l in enumerate(lines) if l.startswith('entry_points = ['))
ee = next(i for i in range(ep, len(lines)) if lines[i].startswith(']'))
stubs = entries(st, se)
names = entries(ep, ee); names.update(stubs)
ins = {}
rx = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
for l in open(dis):
    m = rx.match(l)
    if m:
        ins[int(m.group(1), 16)] = (m.group(2), m.group(3))
starts = sorted(set(names) | {t for (op, args) in ins.values() if op == 'jal' for t in [int(args.split()[0], 16)]})
def func_of(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i] if i >= 0 else None
writes = collections.defaultdict(set); reads = collections.defaultdict(set); calls = collections.defaultdict(set)
LD = {'lw','lh','lhu','lb','lbu','ld','lwu','lq','lwc1'}; SD = {'sw','sh','sb','sd','sq','swc1'}
cur = None; regs = {}
for a in sorted(ins):
    f = func_of(a)
    if f != cur: cur, regs = f, {}
    op, args = ins[a]
    parts = [p.strip() for p in args.split(',')]
    if op == 'jal':
        calls[int(parts[0].split()[0], 16)].add(f); continue
    if op == 'lui' and len(parts) == 2:
        regs[parts[0]] = int(parts[1], 16) << 16 & 0xFFFFFFFF; continue
    if op in ('addiu', 'daddiu', 'ori') and len(parts) == 3 and parts[1] in regs:
        v = int(parts[2], 16) if parts[2].startswith(('0x','-0x')) else int(parts[2])
        regs[parts[0]] = (regs[parts[1]] | v) if op == 'ori' else (regs[parts[1]] + v) & 0xFFFFFFFF; continue
    m = re.match(r'(-?(?:0x)?[0-9a-f]+)\((\$\w+)\)', parts[-1]) if len(parts) == 2 else None
    if m and (op in LD or op in SD) and m.group(2) in regs:
        off = int(m.group(1), 16) if 'x' in m.group(1) else int(m.group(1))
        addr = (regs[m.group(2)] + off) & 0xFFFFFFFF
        if 0x100000 <= addr < 0x2000000:
            (writes if op in SD else reads)[addr].add(f)
    if op in LD and len(parts) == 2 and parts[0] in regs and not (m and m.group(2) == parts[0] and False):
        regs.pop(parts[0], None)
    elif parts and parts[0].startswith('$') and op not in SD and op not in ('beq','bne','beql','bnel','jr','jalr','b'):
        regs.pop(parts[0], None)
# functions reachable only from stubbed code never run either
dead = set(stubs)
changed = True
while changed:
    changed = False
    for f, callers in calls.items():
        if f not in dead and callers and callers <= dead:
            dead.add(f); changed = True
for g in sorted(writes):
    w = writes[g]
    if w & dead and not (w - dead):
        rd = reads.get(g, set()) - dead
        if rd:
            ws = ', '.join(f"{names.get(f, hex(f))}" for f in sorted(w))
            rs = ', '.join(f"{names.get(f, hex(f))}" for f in sorted(rd)[:6])
            print(f"0x{g:06x}  escrita só por: {ws}  |  lida por: {rs}{' ...' if len(rd) > 6 else ''}")
