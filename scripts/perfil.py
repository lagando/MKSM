#!/usr/bin/env python3
"""Resume um ps2_profile.txt (PS2X_PROFILE) por função.

uso: scripts/perfil.py ps2_profile.txt PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner [linhas]

Mostra a porcentagem de amostras por função do executável e, para as amostras fora dele
(libc, driver), quem chamou (a palavra do topo da pilha que cai no executável).
"""
import bisect
import collections
import subprocess
import sys


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    profile, exe = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    base = None
    rows = []
    for line in open(profile):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'base':
            if exe in line and 'r-xp' in line and base is None:
                lo = int(parts[1].split('-')[0], 16)
                base = lo - int(parts[3], 16)
            continue
        rows.append((int(parts[0], 16), int(parts[2], 16) if len(parts) > 2 else 0))
    if base is None:
        print('executável não encontrado nos mapas do perfil')
        return 1
    syms = []
    for line in subprocess.run(['nm', '-C', '--defined-only', exe], capture_output=True, text=True).stdout.splitlines():
        q = line.split(' ', 2)
        if len(q) == 3 and q[1] in 'tTwW':
            syms.append((int(q[0], 16), q[2]))
    syms.sort()
    keys = [s[0] for s in syms]

    def name(address):
        i = bisect.bisect_right(keys, address) - 1
        return syms[i][1][:110] if i >= 0 and 0 <= address < 0x10000000 else None

    inside = collections.Counter()
    callers = collections.Counter()
    for rip, caller in rows:
        n = name(rip - base)
        if n:
            inside[n] += 1
        else:
            inside['(fora do executável)'] += 1
            c = name(caller - base) if caller else None
            if c:
                callers[c] += 1
    total = max(1, len(rows))
    print(f'{len(rows)} amostras')
    for n, v in inside.most_common(top):
        print(f'{100 * v / total:5.1f}% {n}')
    print('-- quem chama o que está fora do executável:')
    for n, v in callers.most_common(8):
        print(f'   {100 * v / total:5.1f}% {n}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
