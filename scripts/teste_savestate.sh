#!/usr/bin/env bash
# Save state de depuração (PLANO_CORRECOES.md, fase 1): salva e carrega sozinho e mostra as linhas [savestate]
# e o [perf] depois do carregamento. O "ram=" do carregado tem de ser igual ao do salvo.
# uso: scripts/teste_savestate.sh [menu|luta] [raiz com PS2Recomp/ e extraido_full/]
set -euo pipefail
CENARIO=${1:-menu}
RAIZ=$(cd "${2:-$(dirname "$0")/..}" && pwd)
EXE=$RAIZ/PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner
ELF=$RAIZ/extraido_full/SLUS_210.87
D=$(mktemp -d)
cd "$D"
export MKSM_INI=$D/mksm.ini # o savestate.bin fica nesta pasta temporária
touch mksm.ini
case "$CENARIO" in
menu)
    MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=20 MKSM_SAVESTATE=1 \
        MKSM_SAVESTATE_AUTO="save@25,load@32" PS2X_PERF=1 timeout 60 "$EXE" "$ELF" > log.txt 2>&1 || true ;;
luta)
    MKSM_PAD_CLOCK=reads MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=24.5 \
        MKSM_AUTO_HOLD="x@36-36.2,s@50-50.2,R@53.5-55.5,s@61.8-62" MKSM_SAVESTATE=1 \
        MKSM_SAVESTATE_AUTO="save@80,load@95,load@115" PS2X_PERF=1 timeout 150 "$EXE" "$ELF" > log.txt 2>&1 || true ;;
*) echo "cenário: menu ou luta" >&2; exit 2 ;;
esac
grep -a "\[savestate\]" log.txt || echo "(nenhuma linha [savestate])"
grep -a -n "\[savestate\] carregado" log.txt | head -1 | cut -d: -f1 | while read -r n; do
    tail -n +"$n" log.txt | grep -a "perf\] t=" | head -8 | cut -c1-160
done
grep -a -c "guest-branch\|missing-target" log.txt | sed 's/^/linhas guest-branch\/missing-target: /'
echo "log: $D/log.txt"
