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

## Passo 1 feito: música e falas (05/11)

- `IopMemory` guarda o bloco (`MADR`, tamanho) de cada DMA com AutoDMA e o entrega ao *sink* quando a DMA termina (`finishSpuDma`, chamado ao despachar a IRQ 0x24/0x28). Interface: `ps2xIOP/include/ps2x/iop/spu2_stream.h`.
- `ps2xRuntime/src/lib/ps2_spu2_stream.cpp` separa o bloco (por KB: 256 amostras esquerdas, depois 256 direitas), guarda num anel de 0,5 s por núcleo e mistura os dois núcleos numa `AudioStream` do raylib (48 kHz estéreo, callback na thread de áudio).
- `PS2X_AUDIO=0` desliga; `PS2X_SPU2_DUMP=<arquivo>` grava a mistura em PCM cru (16 bits estéreo, 48 kHz).
- **Formato conferido na gravação crua:** 256+256 por KB é o único arranjo contínuo (os outros dão saltos ~10× maiores nas fronteiras). Música no menu, silêncio no carregamento e outra música na fase.
- **Ainda não ouvido por uma pessoa.** Dois pontos para escutar:
  - nas emendas de cada bloco de 256 amostras o sinal dá um degrau ~2× maior que o passo normal (sem trocar canal nem deslocar amostra). Pode ser do próprio ADX ou um estalo a ~187 Hz;
  - quando o jogo roda abaixo de 60 campos/s (fase), o tempo emulado anda mais devagar que o real e o som chega mais devagar que toca: o anel esvazia e entra silêncio. Corrigir com reamostragem pelo ritmo de chegada, se incomodar.

## Primeira escuta (05/11)

Relato: zumbido agudo nas telas; sem som ao escolher opção e dar start (efeitos, esperado: vozes não emuladas); na fase dá para reconhecer o som, mas muito picotado.

**Zumbido: corrigido (05/11).**
- O espectro tinha os harmônicos de 187,5 Hz (48000/256): nas emendas de 256 amostras, blocos começavam fora de fase ou repetiam as últimas 64 amostras.
- Origem: o mixer do ADX (`CRI_ADXI`, laço em `0x56500`–`0x567c8` com base `0x51e00`) soma 32 amostras por volta das filas de PCM decodificado de cada stream. A thread decodificadora estava faminta e o mixer relia dados velhos. O culpado era o atalho do IOP que cobrava 16× os ciclos de threads que não cedem: ela recebia 1/16 das instruções por segundo de tempo emulado.
- Correção: atalho removido. Depois do AutoDMA no ritmo real ele não economizava quase nada (fase 33–40 sem ele, 34–42 com ele). Emendas: salto na emenda / salto dentro do bloco de 13,9 para ~0,8–1,0; harmônicos iguais às frequências vizinhas.
- **Não funcionou antes:** manter o `CHCR` ocupado até a IRQ ou avançar `MADR`/`BCR` no fim da DMA: o driver para de mandar música. Ele alterna dois buffers de 2 KB (`MADR` 0x609C0/0x611C0) e reescreve `BCR`/`MADR` a cada disparo.

**Som picotado na fase: corrigido (05/11).** A fase roda a ~35–40 campos/s, então o som chega mais devagar do que toca. Agora cada núcleo toca com velocidade proporcional ao quanto tem na fila (cheia com 100 ms: velocidade normal; abaixo disso, mais lenta e mais grave, sem buracos; acima, até 10% mais rápida para alcançar). Medido: 0 silêncios de 10 ms na fase (antes faltavam pedaços).

**Nota de teste:** sem `MKSM_AUTO_PAD` o jogo fica na tela de título, que não tem música; o PCM sai todo zero e parece defeito, mas não é.

## Plano para ter som

1. **Música e falas (ADX):** feito, ver acima.
2. **RAM de som:** DMAs sem AutoDMA e escritas manuais (`TSA`/`STD`) vão para uma RAM de 2 MB.
3. **Vozes (efeitos):** 24 vozes por núcleo: ADPCM da RAM de som, pitch, ADSR, volume, key on/off, mistura. É o que o `SDRDRV` usa para socos, golpes e menus.
4. **Depois:** reverb, IRQ por endereço, efeitos.

Os passos 2 e 3 andam juntos: as vozes tocam o que está na RAM de som.
