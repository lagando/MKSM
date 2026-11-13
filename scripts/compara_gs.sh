#!/usr/bin/env bash
# Compara o renderizador atual com o novo (PS2X_GS_HW=1) em cenas gravadas do GS.
# uso: scripts/compara_gs.sh [cena ...]      (sem argumentos: todas as gsrec/*.gsr)
# Para cada cena: PSNR das duas páginas de campo (0x00 e 0x46) e o tempo de cada renderizador.
# PSNR >= 30: quase igual. 20-30: diferenças visíveis (ver as imagens). < 20: algo errado.
# As imagens ficam em ${COMPARA_DIR:-/tmp/compara_gs}/<cena>_{atual,novo}_fb{00,46}.ppm
set -euo pipefail
RAIZ=/run/media/valk/linux_arquivos/projetos/ShaolinMonks
REPLAY=$RAIZ/PS2Recomp/out/build/ps2xTest/ps2x_gs_replay
OUT=${COMPARA_DIR:-/tmp/compara_gs}
mkdir -p "$OUT"
cd "$RAIZ"
cenas=("$@")
if [ ${#cenas[@]} -eq 0 ]; then
  for f in gsrec/*.gsr; do cenas+=("$(basename "$f" .gsr)"); done
fi
for c in "${cenas[@]}"; do
  t1=$("$REPLAY" "gsrec/$c.gsr" --ppm "$OUT/${c}_atual" 2>&1 | grep -o 'total[^,]*\|[0-9.]* ms total' | head -1)
  t2=$(PS2X_GS_HW=1 "$REPLAY" "gsrec/$c.gsr" --ppm "$OUT/${c}_novo" 2>&1 | grep -o '[0-9.]* ms total' | head -1)
  python3 - "$OUT" "$c" "$t1" "$t2" <<'PY'
import sys
import numpy as np
from PIL import Image
out, c, t1, t2 = sys.argv[1:5]
res = []
for page in ('00', '46'):
    a = np.asarray(Image.open(f'{out}/{c}_atual_fb{page}.ppm').convert('RGB')).astype(float)
    b = np.asarray(Image.open(f'{out}/{c}_novo_fb{page}.ppm').convert('RGB')).astype(float)
    mse = ((a - b) ** 2).mean()
    res.append('igual' if mse == 0 else f'{10 * np.log10(255 ** 2 / mse):.1f} dB')
print(f'{c:12s} fb00 {res[0]:>9s}  fb46 {res[1]:>9s}   atual {t1:>14s}   novo {t2:>14s}')
PY
done
