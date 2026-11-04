# Áudio: estado atual e plano (05/11)

## Como está hoje

- **SPU2 não é emulado.** No IOP (`ps2xIOP/src/emulator/core/iop_memory.cpp`), as escritas nos registradores do SPU2 (`0x1F900000`–`0x1FA00000`) só vão para um mapa. Uma DMA para o SPU2 (canais 4 e 7) só marca o status e dispara a interrupção depois de um atraso; os dados não vão para lugar nenhum. Não existe RAM de som nem vozes, e nada sai.
- **`PS2AudioBackend`** (`ps2xRuntime/src/lib/ps2_audio.cpp`) é um tocador de amostras VAG por RPC, feito para outro jogo. O MKSM nunca o chama.
- O raylib já abre o dispositivo de áudio (`InitAudioDevice` em `ps2_runtime.cpp`).

## Como o MKSM faz som

Tudo passa pelos módulos reais do IOP, rodando no interpretador:

| Módulo | Papel |
|---|---|
| `LIBSD.IRX` | biblioteca da Sony para o SPU2 (`sceSd*`): registradores, transferências |
| `SDRDRV.IRX` | driver de som da Sony: efeitos sonoros tocados por vozes a partir da RAM de som |
| `CRI_ADXI.IRX` | ADX da CRI: decodifica música e falas no IOP e manda PCM ao SPU2 por **AutoDMA** |
| `SNDFI.IRX` | camada de som do jogo por cima dos anteriores |

**Medido (live, 75 s):** o ADX usa AutoDMA nos dois núcleos (`ADMAS` = 1 no núcleo 0 e 2 no núcleo 1). Cada transferência tem 2 KB (`BCR` = 32 blocos de 16 palavras), o que dá 512 quadros estéreo de 16 bits, ou 10,7 ms a 48 kHz.

**Corrigido:** o atraso dessa DMA era de 1.024 ciclos, ~384× mais rápido que o tocar real. O driver ADX decodificava áudio sem parar, e isso era a maior parte do custo do IOP. Agora a DMA com AutoDMA ligado leva 768 ciclos do IOP por quadro, o tempo real. Isso também é a base para tocar o som de verdade.

## Plano para ter som

1. **Música e falas (ADX):** no início de cada DMA com AutoDMA, copiar o bloco da RAM do IOP (endereço no `MADR` do canal 4/7) para um anel por núcleo. Uma `AudioStream` do raylib a 48 kHz estéreo consome o anel. O formato do bloco AutoDMA (256 amostras do canal esquerdo, depois 256 do direito, a cada 1 KB) precisa ser conferido. É pequeno e dá a maior parte do som.
2. **RAM de som:** DMAs sem AutoDMA e escritas manuais (`TSA`/`STD`) vão para uma RAM de 2 MB.
3. **Vozes (efeitos):** 24 vozes por núcleo: ADPCM da RAM de som, pitch, ADSR, volume, key on/off, mistura. É o que o `SDRDRV` usa para socos, golpes e menus.
4. **Depois:** reverb, IRQ por endereço, efeitos.

O passo 1 é independente dos outros e pode ser testado sozinho.
