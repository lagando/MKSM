#!/usr/bin/env bash
# Roda o jogo com as bibliotecas desta pasta. Configuração em mksm.ini (ao lado deste arquivo).
set -euo pipefail
d="$(cd "$(dirname "$0")" && pwd)"
if [[ -f "$d/mksm.ini" ]]; then
    while IFS='=' read -r chave valor; do
        chave=${chave%$'\r'} valor=${valor%$'\r'}
        [[ -z "$chave" || "$chave" == \#* ]] && continue
        export "$chave=$valor"
    done < "$d/mksm.ini"
fi
export PS2X_GS_RECORD_DIR="${PS2X_GS_RECORD_DIR:-$d/gsrec}" PS2X_CAPTURE_DIR="${PS2X_CAPTURE_DIR:-$d/capturas}"
export MKSM_INI="$d/mksm.ini" # o menu Gráfico grava aqui
mkdir -p "$PS2X_GS_RECORD_DIR" "$PS2X_CAPTURE_DIR"
cd "$d/jogo" # o runtime cria mc0/ na pasta atual antes de saber onde está o ELF
# Bibliotecas: primeiro as do sistema (combinam com o processador da pessoa; as da pasta vêm do PC que montou a
# pasta e podem exigir instruções que este processador não tem, como AVX2). Da pasta, só as que faltarem no sistema.
# MKSM_LIBS=pacote força as da pasta (glibc inclusa), como antes.
sysld=/lib64/ld-linux-x86-64.so.2
if [[ "${MKSM_LIBS:-}" != pacote && -x "$sysld" ]]; then
    extra="${XDG_CACHE_HOME:-$HOME/.cache}/mksm-libs"
    mkdir -p "$extra" && find "$extra" -maxdepth 1 -type l -delete
    for _ in 1 2 3 4 5 6; do # uma biblioteca da pasta pode puxar outra que também falte
        lista=$("$sysld" --library-path "$extra" --list "$d/bin/ps2EntryRunner" 2>&1 || true)
        if grep -q "version .GLIBC_" <<<"$lista"; then extra=""; break; fi # glibc do sistema velha demais
        faltam=$(awk '/not found/ {print $1}' <<<"$lista")
        [[ -z "$faltam" ]] && break
        for so in $faltam; do
            [[ -e "$d/lib/$so" ]] && ln -sf "$d/lib/$so" "$extra/$so"
        done
    done
    if [[ -n "$extra" ]]; then
        usadas=$(find "$extra" -maxdepth 1 -type l -printf '%f ')
        [[ -n "$usadas" ]] && echo "Bibliotecas da pasta (faltam no sistema): $usadas" >&2
        exec "$sysld" --library-path "$extra" "$d/bin/ps2EntryRunner" "$d/jogo/SLUS_210.87" "$@"
    fi
fi
exec "$d/lib/ld-linux-x86-64.so.2" --library-path "$d/lib" "$d/bin/ps2EntryRunner" "$d/jogo/SLUS_210.87" "$@"
