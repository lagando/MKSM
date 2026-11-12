# Plano de trabalho: desempenho, Kung Lao e raios de luz

Reescrito em 11/11 com tudo o que já foi medido. Três trilhas independentes. Cada passo diz o que fazer, como saber que deu certo e o que fazer se não der. A arquitetura está no `ARQUITETURA.md`; a evidência de cada defeito, no `PLANO.md` (5A.24 raios, 5A.25 Kung Lao, 5A.26 filmes). O registro detalhado do trabalho antigo de desempenho está no fim deste arquivo.

## Onde estamos (11/11)

| Item | Estado |
|---|---|
| Gameplay (Goro's Lair) | **60 quadros/s**, o teto do vsync do PS2 |
| Vazão real da gameplay (`PS2X_UNCAPPED=1`) | **~60–65**: sem folga. GS ~90% ocupado, GPU (GTX 1050) 81–84% |
| Vazão real dos menus | 200–250 |
| Kung Lao como parceiro | invisível na cena de entrada (só o chapéu aparece) |
| Raios de luz do teto e coluna do pilar | barras sólidas cinza-claras em vez de feixes translúcidos |
| Filmes de abertura (CRI/Sofdec) | **abertura inteira**: MIDWAY, autosave, título, OPENING como demonstração; Start pula |

## Como medir

- **Ao vivo:** comandos do `CLAUDE.md`. Até a gameplay: `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,...,x@120-120.3"`. Com `PS2X_PERF=1`, uma linha a cada 2 s.
- **Vazão (novo):** `PS2X_UNCAPPED=1` tira o prazo de tempo real do vblank. A lógica do jogo anda um passo por vsync, então o jogo acelera; o número de "jogo N quadros/s" é a vazão máxima. **Toda otimização de desempenho daqui em diante se mede assim**, porque com o vsync preso o ganho não aparece.
- **Escolher o Kung Lao:** tocar direita de 10 a 40 s (`r@10-10.15,...,r@39-39.15`) antes do script acima.
- **Capturas:** `PS2X_SCREENSHOT_DIR=<pasta> PS2X_SCREENSHOT_EVERY=<quadros>`. Rodando fora do `PS2Recomp/`, copiar o `imgui.ini` para a pasta, senão o painel de debug cobre as capturas.
- **RAM:** `PS2X_DUMP_RAM_AT=s1,s2` grava `ram_<s>.bin` e `vu1_<s>.bin` na pasta atual.
- **GS:** `PS2X_GS_RECORD=<arq> PS2X_GS_RECORD_AT=<s> PS2X_GS_RECORD_FRAMES=<n>`; reproduzir com `ps2x_gs_replay <arq> --ppm <prefixo>` e comparar com `PS2X_GS_GPU=0` (mesmo hash = o GS não tem culpa).
- **Regras de qualquer mudança:** suítes `vu1` 42/0, `vu` 44/0, `gs` 44/33 (as 33 falhas são antigas), `float`; hashes dos seis replays: `titulo 832b159039f24acb`, `menu bc1bbc63f1e40968`, `dificuldade d3c7ea7f1eb1042b`, `fase 1eaa2f596f8adf91`, `fase2 edef4ac4b8f25a78`, `fase3 65173649eaffbb7a`. Mudança na VU passa no `PS2X_VU_JIT_CHECK=20` sem diferença.
- **Máquina livre** (`pgrep -a ps2EntryRunner`, `top`) e tela destravada.

## Trilha A: desempenho (folga e, no futuro, 90 quadros/s)

**Estado (12/11):** a gameplay roda nos 60 quadros/s do vsync do PS2 o tempo todo; nada abaixo aumenta o fps visível, só dá folga ou prepara os 90. Sem trava (`PS2X_UNCAPPED=1`) a vazão é ~60–62 e **nenhuma thread chega a 100%** (GSFrontend 83% com espera ativa, VU1Thread 78%, GSWorker 64%, GameThread 57%): o teto vem das esperas entre elas, não do custo de uma.

**Ordem do que falta:**
1. ✅ **A9a – passo de tempo do jogo (12/11): não existe um global.** 1/60 (`0x3C888889`) aparece em ~20 pontos do código, sempre montado com `lui`/`ori` e passado em `$f12` para funções diferentes (`0x394F50`, `0x102D28`, `0x102940`, `0x1BB218`...; nenhuma dominante), e 3 cópias nos dados (`0x50A5D8`, `0x50A5F0`, `0x58E1B8`) sem referência direta; 1/30 não aparece. Com a lógica medida andando um passo por vsync, o MKSM é travado por quadro: **90 fps de verdade só com interpolação de quadros**, inviável sem conhecer a engine. A trilha fica como folga, não como 90.
2. **Sobreposição entre threads** (o único ganho real de vazão): reduzir as esperas da GameThread na leitura do `VIF1_STAT`, da fila do GS e dos slots da GPU; executar o lote seguinte do EE enquanto a VU1 termina o anterior quando o jogo não lê nada da VU1 no meio.
   - 🔶 Medido (12/11, gameplay sem trava): a GameThread espera a VU1Thread **45% do tempo** (`ps2_vu1_thread::sync`, ~175 vezes/s, ~2,6 ms cada, quase tudo na leitura do `VIF1_STAT`). A espera é exigida pelo determinismo (o valor só existe depois que a VU1 termina o que recebeu), então o ganho vem de **VU1Thread mais rápida**. Perfil dela sem trava: código do JIT 24%, `runJitBlock` 22% (modelo de ciclos), `libc` 9,6%, `processVIF1Data` 8,3%, `run`+`execLower` ~12% (pares interpretados), compilação/tabelas ~5,5%. A arena de código do JIT não enche (<1 MB) e as tabelas por código nunca são descartadas.
   - Pares da VU1 ainda interpretados (histograma por PC, 12/11): (a) o fim de cada programa, `XGKICK` + NOP com E-bit + slot de atraso (`0x1280`–`0x1290`, `0xEC0`–`0xED0`), três pares por execução; (b) contas FMAC/`ILW`/`ISW` dentro da janela de 16 pares antes de um leitor de flags (`0x6C0`–`0x6D0`, `0xE28`, `0x798`/`0x7A0`). Próximo ganho: o JIT calcular MAC/STATUS nessas janelas e compilar o final com XGKICK (o E-bit fica fora). Estimativa ~6% da VU1Thread; não muda o fps visível (60).
   - Decisão (12/11): o par com `XGKICK` do fim é compilável, mas o bloco recusa a entrada enquanto o `XGKICK` anterior ainda transfere (regra de correção do PATH1). Calcular flags no JIT é uma mudança grande no gerador x86, com risco para a VU1, por ≤6% de uma thread e nenhum fps visível: adiado até haver uma meta acima de 60.
3. **A4 restante:** apresentar direto da VRAM da GPU (sem cópia para o host) e a espera de fim de campo; ~1 ms de GPU.
4. **A5 restante:** registro no formato final já no frontend (copiar uma vez), menos trabalho de CLUT por primitivo; custo espalhado, só com reestruturação.
5. **A6/A7** (VU1, IOP): só quando a medida mostrar que viraram o limite.
6. **A8** (som nativo, IOP em HLE): semanas, risco de quebrar o som, não dá fps.

Histórico da regra antiga: o limite parecia ser o desenho (worker do GS ~90%, GPU 81–84%); medido depois, a GPU caiu para ~70% e o worker sozinho não explica o teto.

1. ✅ **Linha de base com vazão (11/11):** gameplay ~60–63 quadros/s com `PS2X_UNCAPPED=1`, GS ~90% ocupado, GPU 81–84%. Refazer a cada passo: Três rodadas de `PS2X_UNCAPPED=1` na gameplay, anotando média de quadros/s, GS ocupado, GPU ocupada e CPU por thread (`threads.sh`). Repetir a cada passo abaixo.
2. ✅ **Onde vai o tempo da GPU (11/11, replay `fase3`, `--repeat 3`, pulando um tipo de envio por vez com uma variável temporária):** campo inteiro **16,0 ms** (~62/s: é o teto ao vivo). Sem cada tipo: triângulos pelo rasterizador **−3,5 ms**, sprites −2,3, decodificação de textura −1,7, tiles (compute) −1,0, uploads −1,1, cópia do campo para a janela −1,2. Para 90/s o campo precisa caber em ~11 ms: triângulos e sprites são onde está a maior parte.
3. ❌ **GPU: variantes do fragment shader (11/11, medido, não feito):** tirar do fragment shader o caminho de sprites (uma variante "só triângulos") deu 15,0 contra 15,2 ms por campo na `fase3`, dentro do ruído (±0,4). Tirar o interlock (imagem errada, só para medir) deu 14,4 ms: ele custa ~0,6 ms. O custo dos triângulos é o trabalho por pixel (amostra de textura, blend, ler e escrever a VRAM no buffer), não o tamanho do shader. O ganho grande seria usar a cor e o blend fixos da GPU (render target) quando o estado do GS permite (blend comum, sem DATE, sem leitura do próprio alvo), uma reescrita do caminho de desenho: só vale com a meta de 90.
4. 🔶 **GPU: menos trabalho por campo.** ✅ (12/11) A cópia do campo para a janela copiava a VRAM inteira (4 MB) na GPU e de novo na CPU (`TakeCapture`); agora só as páginas 0–139 (os dois quadros do MKSM, `0x00` e `0x46`, 1,1 MB): `fase3` ~15,2 → ~14,0 ms por campo, seis hashes iguais, imagem certa ao vivo em todas as telas. Ao vivo sem trava a vazão não mudou (60,4): o limite ao vivo não é só a GPU. Falta: apresentar direto da VRAM da GPU e a espera de fim de campo.
5. 🔶 **Worker do GS: CPU por primitivo (12/11, medido).** Ao vivo sem trava o worker é o limite (GS 96–98% ocupado, GPU 70%). Perfil do `GSWorker` (o profiler agora escolhe, entre threads de mesmo nome, a que mais usou CPU; o `Present` tem um `GSWorker` ocioso): `QueuePrimitive` 24,7%, `PrepareGpuPrimitive` 20,4%, driver 10,6%, CLUT (`SetClut`, `LoadClutUnlocked`, `QueueClutLoad`) ~15%. Com `rdtsc` por trecho no replay `fase3`: ~405 ciclos por primitivo no `QueuePrimitive`, espalhados (25–78 por trecho: corte de lote, cópia, chave de textura/CLUT, notePending, tiles). Sem ponto quente isolado; o ganho pede escrever o registro no formato final já no frontend (copiar uma vez) e cortar o trabalho de CLUT por primitivo.
   - ✅ CLUT recarregada igual (12/11): 43% das cargas pela CPU na `fase3` deixam a paleta como estava (~220 por campo); agora não publicam versão nova (sem `SetClut`, hash de CLUT nem chave de textura nova). Seis hashes iguais, `gs` 44/33; vazão ao vivo 60,4 → 61,7 (dentro do ruído), worker ainda 98%.
   - ❌ Preparar o primitivo na thread do frontend (12/11): exato (seis hashes iguais), mas vazão 59,4 (sem ganho); desfeito. CPU por thread na gameplay sem trava: GSFrontend 83% (boa parte espera ativa), VU1Thread 78%, GSWorker 64%, GameThread 57%: nenhuma em 100%. O teto ~60–62 vem das esperas entre as threads (VIF1_STAT, anel do GS, slots da GPU), não de uma thread; o próximo ganho é sobreposição, não corte de CPU isolado.
6. ⬜ **VU1Thread**, quando o passo 1 mostrar que ela virou o limite. Já descartado com medida: recompilação estática (o custo é o modelo de ciclos, não o despacho) e tirar leituras repetidas. Candidatos: `processVIF1Data` (UNPACK em SIMD por formato), o modelo de ciclos só nos limites de bloco quando o bloco não tem FDIV/EFU/XGKICK.
7. ⬜ **Thread do jogo**, quando for o limite: JIT da IOP conferindo blocos por geração de escrita das páginas em vez de `memcmp` (exige marcar toda escrita na RAM da IOP, inclusive DMA e `writeGuest` da EE); MULT/DIV e entrada com load pendente no JIT da IOP.
8. ⬜ **Fase 6 (longo prazo, não dá fps):** som do CRI nativo e IOP em HLE. Só depois do resto, com teste comparando com o emulado e cuidado com as armadilhas de `notas/audio.md`.
9. ⬜ **90 quadros/s de verdade.** Soltar o vsync só acelera o jogo, porque a lógica anda um passo por vsync. Caminhos, do mais barato ao mais caro:
   - Procurar no executável uma variável de passo de tempo ou velocidade do jogo (engine da Paradox; procurar floats de 1/60 e 1/30 usados na física e na animação). Se existir, vblank a 90 Hz com o passo em 2/3.
   - Se não existir: interpolação de imagem na apresentação (gera quadros intermediários; tem artefatos e latência).
   - Pré-requisito em qualquer caso: vazão de ~95 no passo 1.

## Trilha B: Kung Lao parceiro invisível

**O que já se sabe (PLANO 5A.25):**
- Escolhendo o Liu Kang, o Kung Lao parceiro aparece só com o chapéu. Escolhendo o Kung Lao, o parceiro Liu Kang aparece inteiro.
- O corpo nunca chega ao GS: replay GPU e software dão o mesmo hash e a mesma imagem sem ele.
- Os dados dele são lidos: com o Liu Kang escolhido o CD tem só leituras a mais (222 setores em LBN 1594609 e alguns blocos pequenos).
- Descartados com medida: JIT da VU, VU1 em thread, leituras de CD com erro, instruções VU inválidas, instruções EE sem tradução, comparações `C.cond.S` da EE e `VMAX`/`VMINI` da VU0.

**Passos:**
1. ✅ **Leituras de CD (11/11):** as leituras a mais do caso Liu Kang vêm no fim, com destino em áreas de buffer (`0x1DEE900`, `0xA35800`...), com cara de som e vozes. O modelo é carregado igual nos dois casos.
2. ✅ **Texturas (11/11, replay de 3 quadros aos 69 s com hash do conteúdo de cada textura desenhada):** 8 texturas T8 só aparecem com o Kung Lao jogador (o corpo dele). No caso Liu Kang, uma delas está na VRAM no mesmo endereço (`0x35D0`) e nenhum triângulo a usa: a textura chega, a geometria não.
3. ✅ **Vértices (11/11, contador temporário no `vertexKick`, 66–72 s):** com o Kung Lao parceiro somem ~1,1 M de vértices desenhados (11,3 M contra 12,4 M) e os pulados por ADC quase não mudam (6,25 M contra 6,46 M). O corpo não é gerado: o corte é antes do XGKICK (EE ou microprograma da VU1), não no GS nem no ADC. Suspeita: o corpo usa esqueleto e o chapéu não; matrizes de osso erradas (animação do parceiro) fariam a VU1 descartar o objeto no teste de recorte.
- ✅ **VIF1 (11/11, contador temporário em `processVIF1Data`, 66–72 s):** com o Kung Lao parceiro a EE manda **mais** dados ao VIF1 (733 MB contra 664 MB). O corpo é enviado; quem não gera os vértices é a VU1. B4 (corte na EE) fica improvável; seguir pelo B5.
4. ⬜ **Corte na EE:** seguir o caminho de desenho do objeto (lista de desenho, teste de visibilidade/frustum, LOD por distância) até o desvio que pula o corpo. Suspeitos: comparações de float com valores especiais, conversões float↔int (o PS2 trunca), comandos de cena `hideobj`/`bgcharhide`/`killandhidecharacter` e a variável de debug `OtherPlayerInvisible` (`0x5756B0`).
- 🔶 **Execuções da VU1 por entrada (11/11, contador temporário em `execute`/`startXgkick`, 66–72 s):** nos microprogramas de personagem (entradas `0x6F0`, `0x000` e `0xE50`, chaveadas pelo código), as execuções que terminam **sem nenhum XGKICK** são 3–5× mais comuns com o Kung Lao parceiro: 1209 contra 368, 722 contra 196 e 270 contra 53. A memória de dados nessas execuções vazias não tem valores com expoente 255 nem enormes (as execuções que enviam têm mais expoentes 255 no caso Liu Kang, 146 contra 93 por execução, que podem ser máscaras legítimas).
- ⚠️ **O defeito é intermitente (11/11).** Uma rodada com `PS2X_VU_JIT=0` mostrou o Kung Lao inteiro, e a busca binária dos blocos do JIT da VU0 parecia apontar para a combinação dos blocos `0x8` e `0x60`–`0x78`. Mas duas rodadas seguidas com o JIT todo desligado voltaram a mostrar só o chapéu, e uma rodada sem nenhum bloco da VU0 no JIT também. **Não é o JIT**; os resultados da busca binária foram ruído. Lição: neste defeito, uma rodada só não prova nada; cada teste precisa de pelo menos 3 rodadas.
- Achado à parte, verdadeiro mas não é a causa: o conferidor do JIT não compara `mac`/`status`, e na VU0 eles diferem do interpretador em quase todo bloco (o JIT só calcula flags perto de quem lê; é por desenho).
- ✅ **Leituras extras não decidem (11/11, 3 rodadas com hora de cada `sceCdRead`):** nas rodadas 2 e 3 os 222 setores (LBN 1594609) e os blocos pequenos chegam aos 86–92 s, depois da cena com os personagens (~70–83 s), nas duas. Rodada 3 com o Kung Lao inteiro, rodada 2 sem. Frequência medida até agora: ~1 em 3–4 rodadas mostra o corpo.
- ✅ **Frequência depende da sessão (11/11, 4 rodadas por configuração, mesmo binário):** padrão 4 de 4 com o corpo; `PS2X_VU1_THREAD=0` 3 de 4. Horas antes, com o mesmo código de jogo, quase todas as rodadas saíam sem o corpo. É uma corrida de tempo de host (carga da máquina, ordem das threads), não um erro de cálculo fixo.
5. ⬜ **Corrida (próximo):** o que varia entre rodadas é o tempo de host (threads de GS, VU1, IOP, áudio, vsync de tempo real). Testar com `PS2X_VU1_THREAD=0`, `PS2X_GS_ASYNC=0` e `PS2X_UNCAPPED=1`, 4 rodadas cada, contando quantas mostram o corpo; a variável que leva a 0 ou a 4 de 4 aponta a corrida. Dentro do jogo, olhar o que lê o relógio real (contadores de tempo, `Async_DataLoad`) no caminho do parceiro. O corpo do parceiro chega à VU1 mas às vezes não gera vértices; e o caso Liu Kang tem leituras de CD a mais no fim (222 setores em LBN 1594609 e blocos pequenos), que podem ser o modelo do parceiro chegando depois de a cena começar. Passos: registrar o instante de cada leitura (temporário, em `sceCdRead`) e o quadro em que o corpo aparece/some, em 5 rodadas; ver se o corpo aparece só quando essas leituras terminam antes da cena. Se for isso, conferir o tempo das leituras HLE contra o do PS2 (o jogo pode contar com uma ordem entre a carga e o roteiro da cena) e o ponto em que o jogo espera a carga (`Async_DataLoad`, `0x378A30`).
- Pronto quando: o Kung Lao aparece inteiro na cena com o Liu Kang escolhido, sem mudar os seis hashes de replay.

**Passos para fechar (12/11):**
- ✅ **B6 – contagem por configuração (12/11):** `scripts/kl_corpo.py` (pixels da calça do Kung Lao nos planos abertos, sem os closes do Raiden) bate com o olho nas 12 rodadas antigas. script que roda N=4 vezes e conta pela captura aos ~75 s se o corpo aparece (comparar a área do sprite do parceiro com uma referência). Sem isso cada teste custa 4×5 min de olho.
- ✅ **B7 (12/11):** com o roteiro do pad em segundos reais: padrão 4/4, `PS2X_GS_ASYNC=0` 4/4, `PS2X_VU1_THREAD=0` 2/4, as duas 3/4. Com `MKSM_PAD_CLOCK=reads` (toques nos mesmos quadros do jogo): `PS2X_VU1_THREAD=0` 4/4, padrão 4/4, toques deslocados de 1–4 s 4/4. **12 de 12 com entrada determinística:** a variação vinha do horário dos toques de X do harness (thread do jogo mais lenta → X em outro ponto da cena), não de corrida entre threads do runtime. Não reproduzido de forma fixa; se voltar a aparecer, gravar o horário exato dos toques. Texto original: com o B6, medir `PS2X_VU1_THREAD=0`, `PS2X_GS_ASYNC=0`, `PS2X_UNCAPPED=1` e as três juntas. A que leva a 0/4 ou 4/4 aponta a corrida.
- ⬜ **B8 – ordem fixa entre threads (só depuração):** variável que serializa EE → VU1 → GS a cada comando (sem sobreposição). Se o defeito some sempre, a corrida está numa sincronização frouxa entre elas; bisseccionar ligando a sobreposição de uma de cada vez.
- ⬜ **B9 – fontes de tempo real que o jogo lê:** registrar (temporário) toda leitura de contador de tempo pela EE (`COUNT`, timers `0x10000000`–`0x10001800`, `GetTimerCount`/alarmes) e a ordem das respostas SIF/`Async_DataLoad` (`0x378A30`) no caminho do parceiro, em uma rodada boa e uma ruim; a primeira divergência é a corrida.
- ⬜ **B10 – corrigir:** tornar determinístico o valor divergente (ex.: resposta de carga só no vsync seguinte, como no PS2, ou timer derivado do relógio do EE e não do host). Conferir 8/8 rodadas com o corpo e os seis hashes iguais.

## Trilha C: raios de luz sólidos ("losangos")

**O que já se sabe (PLANO 5A.24):**
- A textura do raio (T4 `0x2F5F`, paleta CT32 `0x2F8B`) tem alfa `0x80` em tudo; o alfa dos vértices vem do pacote VIF montado pela EE: `0x30` em parte dos desenhos (certo) e `0x80` nas barras (cor `7F7F7F80`).
- Blend herdado `ALPHA 0x44` (normal), posto por um `DIRECT` do próprio fluxo logo antes do raio; outros desenhos do raio usam `0x48` (aditivo).
- Descartados com medida: mipmap, faixa de Z, ordem com o bloom, MAX/MINI da VU, JIT da VU1, comparações de float da EE (as barras continuam depois da correção de 11/11).
- No jogo real não há barras.

**Pista nova (11/11):** o `TEST` do raio é `0x7380B`: alfa GEQUAL `0x80` com AFAIL=RGB_ONLY **e DATE=1, DATM=0** (teste de alfa de destino: só desenha onde o bit 7 do alfa do pixel de destino é 0). O DATE já é emulado (CPU e GPU). Se no PS2 real os pixels atrás do raio têm esse bit em 1, o raio é rejeitado ali, e é exatamente o que o print real mostra. Então o suspeito vira **quem escreveu o alfa do quadro antes do raio** (cena com `FBA`, `FBMSK`, alfa de textura, a passada que limpa o quadro).

**Passos:**
1. ✅ **Medir o alfa de destino nos pixels das barras (11/11, replay `fase2`, diagnóstico temporário no `WritePixel`):** 0 pixels do raio recusados pelo DATE; o alfa de destino sob o raio é `0x00` em 99,9% (24 546 contra 30 em `0x80`).
2. ✅ **Quem escreveu esse alfa:** a cena (`2c78`, `2590`) escreve alfa `0x80`–`0x83`; logo antes do raio, sprites de 32×32 com `TEST 0x31001` (alfa NEVER, AFAIL=FB_ONLY), `ALPHA 0x6a` (resultado = Cd) e vértice com alfa 0 zeram só o alfa, junto com os brilhos `1180`/`2580` (e a limpeza `3288` no começo do campo). É o próprio fluxo do jogo limpando o alfa antes do raio, então o DATE deixa o raio passar também no protocolo. **Pista do DATE descartada:** nossa emulação segue o fluxo.
3. ➖ Corrigir no GS: nada a corrigir pelo DATE.
4. ⬜ **Se o alfa de destino estiver certo:** voltar à hipótese da EE: achar a função que escreve as cores do raio no pacote (`xref.py` no endereço de origem do UNPACK em `0xE10`) e conferir a conversão float + 2^23 do alfa de fade.
5. ⬜ **Último recurso:** dump de GS do PCSX2 (`.gs`) desta cena, para comparar registrador a registrador. O PCSX2 não está instalado nesta máquina; precisa do usuário.
- Pronto quando: na cena do Raiden e na gameplay os raios saem como feixes translúcidos, sem barras.

**Passos para fechar (12/11):**
- 🔶 **C6 (12/11):** despejo da RAM na gameplay com as barras na tela (140 s): `7F7F7F80` aparece 3643 vezes (é a cor neutra padrão dos vértices, não serve de assinatura) e o alfa `0x30` não aparece em nenhuma forma (bytes, inteiros, float 48). O alfa certo não está pronto na RAM da EE: deve ser montado na VU1 (registrador de material × cor do vértice). Próximo: no replay, achar o XGKICK do raio e o programa/endereço da VU1 que monta o RGBA. Texto original: no replay `fase2`, guardar o endereço de origem (MADR do DMA VIF1) do UNPACK que leva cor `7F7F7F80` e do que leva alfa `0x30`.
- ⬜ **C7 – achar quem escreve:** `PS2X_DUMP_RAM_AT` no mesmo instante e um watchpoint temporário de escrita da EE nesse endereço (log de PC), depois `xref.py` para subir até a função do raio.
- ⬜ **C8 – entender o alfa:** ler a função: o alfa vem de um fade (float → int com o truque `+2^23`, `CVT.W.S`, saturação em 0x80)? Conferir as instruções usadas contra o comportamento do PS2 (truncamento, sem NaN/inf, `MADD`/`MSUB` com acumulador, `CLAMP`). A barra sólida é o fade preso no máximo.
- ⬜ **C9 – se o código parecer certo:** o valor de entrada do fade pode vir de um timer ou de outra entidade (mesma família da corrida do Kung Lao); registrar o valor ao longo dos quadros.
- ⬜ **C10 – comparação com o PCSX2 (precisa do usuário):** dump `.gs` da cena do Raiden pelo PCSX2, para ver o alfa de vértice certo; com ele, C7 vira só procurar onde o nosso diverge.
- ⬜ **C11 – corrigir** no recompilador ou no runtime, conferindo os seis hashes (as gravações antigas mudam só nos raios; regravar uma de referência).

## Trilha D: filmes de abertura

**Estado (12/11, PLANO 5A.26):** ✅ D1–D4 e D6 feitos: o travamento depois do MIDWAY era o `sceMpegDelete` (`0x4688A0`) sem função gerada, alcançado por um `j` no destroy do Sofdec; ligado a um wrapper HLE. A abertura vai até o título e o `OPENING.SFD` toca como demonstração. Antes: `MIDWAY.SFD` tocava inteiro (30 quadros/s, imagem certa, som). O `GetPicture` decodifica o `.SFD` do disco (arquivo achado pela faixa de LBN no `sceCdRead`), anda o DMA toIPU na medida do vídeo de cada quadro, chama o callback "nodata" no fim e devolve -1. Depois disso a tela fica preta: o laço principal roda (dois envios SIF por quadro, `ra 0x41ECF4`), mas o `OPENING.SFD` nunca é lido. `0x1D32E0` roda uma vez só no boot (só dispara a abertura).

**Passos:**
- ✅ **D1 – o que a thread do jogo faz na tela preta:** perfil da `GameThread` (`PS2X_PROFILE` dos 25 s em diante) e histograma de PCs da EE (só código do jogo, sem a IOP) para achar o laço que espera.
- ✅ **D2 – quem cria o player:** `xref.py` nos chamadores de `0x447728`/`0x44C928` e nas referências às strings `MIDWAY`/`OPENING` do FILELIST; achar a lista de filmes da abertura e o índice que deveria avançar.
- ✅ **D3 – a condição de fim:** registrar (temporário) o estado do player Sofdec (struct em `0x105C3C0` e o handle do player) quadro a quadro no fim do MIDWAY: que campo a rotina da abertura testa e que valor ela espera. Candidatos: `sceMpegIsEnd` (não está ligado no HLE), estado de fim da stream de áudio do CRI (IOP/SIF), contagem de quadros contra o cabeçalho do SFD, o campo em `mpegAddr+0x40` que o modo arquivo hoje pula.
- ✅ **D4 – corrigir o fim:** ligar a função ou escrever o campo que falta (no HLE do MPEG ou no fim do toIPU). Pronto quando o `OPENING.SFD` aparece no log do `sceCdRead` e começa a tocar.
- ✅ **D5 – os outros filmes (12/11):** trocando a entrada 0 da tabela `0x4D3BA0` pelo índice N antes do `0x1D32E0` (override temporário), os 8 tocam com imagem certa: MIDWAY, OPENING, BLITZ, GAUNTLET, ESRB, END006, END011, FINAL; o ESRB curto termina e o jogo segue para o aviso de autosave. abertura inteira até o título; conferir também filmes de meio de jogo (cenas pré-renderizadas, `FINAL.SFD`) com o mesmo caminho.
- ✅ **D6 – pular com botão:** Start durante o filme deve encerrar (o jogo testa o pad e pede fim ao player); conferir que o HLE para limpo.
- ⬜ **D7 – sincronia:** imagem pelo relógio de áudio do CRI (hoje pelos 30/s do vsync); conferir que não desliza num filme longo.
- ✅ **D8 – limpeza (12/11):** `MKSM_SKIP_INTRO` agora é só atalho. `MKSM_SKIP_INTRO` vira só atalho; atualizar `ARQUITETURA.md` e o `PLANO.md` 5A.26; seis hashes iguais.

## Som na gameplay (12/11) ✅

Nos menus perfeito, na gameplay ondulado. Medido: o AutoDMA entregava ~50 200 amostras por segundo real (48 000 é o certo), e o WSOLA comprimia 4–10% o tempo todo. Duas causas: (1) o `runCycles` do IOP não descontava os ciclos que passavam do alvo (4% a mais, menus inclusive); (2) depois de um carregamento o vsync recuperava o atraso correndo, e a IOP chegava a 37–38 MHz por minutos. Correções: dívida de ciclos no IOP, vsync reancorado quando passa de 2 campos de atraso e andamento 1,0 exato com a fila numa faixa folgada. Depois: 48 002/s nos menus, 47 858/s na gameplay, andamento 1,0 em 77 de 79 s.

## Ordem sugerida (12/11)

1. **Trilha D (D1–D8):** a mais perto do fim; cada passo tem ferramenta pronta.
2. **Trilha B (B6–B10):** primeiro o B6 (contagem automática), porque sem ele cada experimento é ruído.
3. **Trilha C (C6–C9; C10 se o usuário fizer o dump do PCSX2):** C6–C7 usam o mesmo tipo de watchpoint que o B9, então vale fazer em seguida.
4. **Trilha A:** só folga (sobreposição entre threads, flags no JIT da VU1, A4/A5, A8). 90 quadros/s exigiria interpolação de quadros; projeto à parte, fora deste plano.

---

## Registro detalhado (até 11/11)

### Onde estamos (09/11)

| Tela | Quadros/s | Real do PS2 | O que limita |
|---|---|---|---|
| Menus | **60** | 60 | nada (teto) |
| Goro's Lair, cutscene | 20–33 | 60 | o GS (ocupado ~100%, jogo esperando o GS até 50%) |
| Goro's Lair, gameplay | **35–40** (média 38, ~64%) | 60 | o GS (ocupado ~97%); a thread do jogo logo atrás |

Medido com GPU ligada (padrão desde 05/11; com `PS2X_GS_GPU=0`, a fase cai para ~7,5). **Em 09/11, com o cache de texturas:** gameplay 27,5–30 quadros/s e o jogo esperando o GS 0%. A GPU já não limita: o próximo ganho é a thread do jogo (Fase 5). **Depois do JIT nativo de desvios/`ILW`/`ISW` e do Z reduzido na GPU (09/11):** gameplay 31,5–35 quadros/s, GS ocupado 89–99%: GPU e thread do jogo voltaram a empatar.

**Uso de CPU por thread na gameplay** (`/proc/<pid>/task`, 05/11): GameThread 85%, GSWorker 29% (o resto esperando a GPU), GSFrontend 21%, GSBand 18%.

**Para dobrar a gameplay é preciso as duas coisas:** GPU mais rápida no desenho **e** tirar trabalho da thread do jogo. Uma sem a outra dá no máximo ~31–32 quadros/s.

### Plano para a gameplay (10/11)

**Por que o PCSX2 é mais rápido na mesma máquina:** (1) desenha triângulos com o rasterizador da GPU (pipeline gráfico), não com um compute shader por tile; (2) gasta pouco CPU por primitivo; (3) roda a VU1 numa thread própria (MTVU). Nós fazemos o contrário nos três. E como o vsync é preso por ciclos, cada quadro lento vira câmera lenta: a 38 quadros/s o jogo anda a ~63% da velocidade.

**Hoje dois limites empatam (~38 quadros/s):** o GS (ocupado ~97%) e a GameThread logo atrás. Por campo pesado (replay da `fase2`, ~31 ms): ~12 ms esperando a GPU e ~19 ms de CPU no `GSWorker` (`QueuePrimitive` ~23%, `PrepareGpuPrimitive` ~19%, driver ~12%). Na GameThread: VU1 ~30–45%, IOP ~12%, laço do vsync ~7%. Só chega a 60 atacando os dois.

Ordem, do maior ganho por esforço ao maior risco:

1. ✅ **Medir a gameplay de verdade (10/11).** `gsrec/fase3.gsr`: 20 campos de gameplay gravados aos 270 s (Liu Kang no Goro's Lair), 182 MB; replay 27,2 ms/campo pela GPU (36,8/s, igual ao vivo), 91,5 ms pela CPU, hash `65173649eaffbb7a` nos dois. Descobertas:
   - **A GPU é o limite principal.** Medido com timestamps do Vulkan (`[perf] GPU ocupada`): ao vivo na gameplay 72–84% ocupada, ~21 ms de GPU por quadro a 38/s; no replay 81–84%. Para 60/s a GPU precisa caber em 16,6 ms.
   - O `GSWorker` gasta só ~8,6 ms de CPU por campo no replay (32% de uma thread): `QueuePrimitive` 28%, `PrepareGpuPrimitive` 22%, driver 20%, `WorkerLoop` 9%. Os ~20% em que a GPU fica parada são esses buracos: cortar CPU por primitivo sozinho dá no máximo ~38/0,8 ≈ 47/s.
   - Esperas ao vivo em 290 s (contadas por ponto de `Flush`): cópia do campo completo 10651× / 65,6 s, carga de CLUT 9089× / 21,8 s, o resto desprezível. A cópia do campo agora é assíncrona (`QueueCapture`, ARQUITETURA §3): sozinha não mudou a gameplay (36,8/s, 34–39), porque o worker passa a esperar ao reusar o slot, com a GPU ocupada; mas é pré-requisito do passo 2.
   - **Onde vai a GPU na gameplay** (timestamps por tipo de dispatch, replay da `fase3`): triângulos em tiles 39%, **sprites 21,6% em ~1200 dispatches por campo**, **cópias de snapshot 7,9% (~800 por campo)**, decodificação de textura 2,2%, sincronização host↔GPU 2,2%, upload 1,2%, captura 1,1%. Os sprites eram os passes de bloom/desfoque (ladrilhos 32×32 lendo e somando no próprio `0x8C`): custo fixo de dispatch e barreira, não de pixel.
   - ✅ **Sprites em lote** (ARQUITETURA §3): `fase3` 27,2 → **21,5 ms** (36,8 → 46,5/s), `fase2` ~31 → 26,8 ms, seis replays bit a bit iguais, `gs` 44/33 (as mesmas falhas sem GPU). Sprites na GPU 21,6% → 7,9% e snapshot 7,9% → 3,5%; ainda sobram ~300 dispatches de sprite por campo (lotes cortados por risco). **Ao vivo na gameplay: 36,8 → 40,3/s (34,5–43,5), GS ocupado 99% → 80%, jogo esperando o GS 0%:** a GameThread volta a ser o limite, com a GPU logo atrás (82–86% ocupada).
   - **Nova ordem:** o ganho grande agora é **desenho mais barato na GPU** (passo 4, ou cortar o custo do `gs_tiles.comp`); o passo 2 vem junto para fechar os buracos. A espera da CLUT (21,8 s) entra no passo 2.
2. 🔶 **GS: CPU do worker (10/11, em andamento).** Com a VU1 em thread, o worker voltou a limitar (100% ocupado, GPU 73–76%). Medido no replay da `fase3`:
   - Espera da carga de CLUT: ~1–2 por campo em ~670 cargas, e os conflitos são reais (CLUT em `0x2850` recém-enviada à GPU; CLUT em `0x25D0` dentro de uma faixa de texturas sendo escrita), não fusão grosseira de faixas. Tirar exige carregar a CLUT na GPU (modo novo no shader, CSM1/CSM2, CT16/CT32, CSA) e recarregar a cópia da CPU só quando um desenho de CPU precisar.
   - Enviar à GPU a cada N primitivos piora: N=1500 → 24,9 ms, N=600 → 33,1 ms (contra ~22): cada envio tem custo fixo na GPU (o cache de texturas é por slot e recomeça; sincronização host↔GPU; barreiras). A granularidade atual fica.
   - O que sobra é CPU por primitivo (`QueuePrimitive` 28%, `PrepareGpuPrimitive` 22%, driver 20%), o item abaixo.
   - **Rodada de 10/11** (replay da `fase3`, seis replays bit a bit iguais, `gs` 44/33, `vu1` 42/0, `vu` 44/0):
     - Achado de medida: o replay esperava a GPU a cada campo e media latência (CPU + GPU somadas). `--sync 0` mede vazão como no jogo; números abaixo com ele (tempo total de 12 repetições).
     - `memset` de 256 bytes no começo de `PrepareGpuPrimitive` aparecia como 70% dela (a loja esperava a fila de escritas do registro anterior). Agora só zera o que cada caminho deixa sem escrever: 25% → 21% do worker.
     - Triângulos (`gs_tiles.comp`) = ~46% da GPU, e **só percorrer as listas sem desenhar já custava ~4 ms por campo**: poucas warps (shader grande) e duas leituras dependentes por entrada. Caixas da lista em memória compartilhada, 64 por vez: 21,1 → 19,8 ms por campo (com espera).
     - Corte de tiles que o triângulo não alcança: pares tile×primitivo −30% (10,9 M → 7,6 M em 2 s), tempo igual (a GPU deixou de ser o único limite).
     - O Z por pixel em `double` (1/32 da velocidade na GTX 1050) **não** pesa: trocado por float só para medir, mesmo tempo.
     - Escrever os registros na RAM em vez do BAR: CPU igual e GPU 45% mais lenta. O custo de CPU não vem de escrever no BAR.
     - **Carga de CLUT na GPU** (ARQUITETURA §3): a espera que sobrava (~0,8 por campo, ~3 ms cada, ~1/3 do tempo do worker parado) é real. As CLUTs moram dentro do buffer do desfoque (§5). Esperar só o envio anterior não pega nada (o conflito é no próprio lote). Com a carga na fila da GPU: **5,18 → 4,54 s** (−12%), esperas 120 → 5 a cada 2 s. Tentados e tirados: espelhar no host todo upload grande sem conflito, e faixas de escrita pelas linhas do triângulo em vez da tesoura (as duas pioram junto com a CLUT na GPU: 4,79 s).
     - **Ao vivo na gameplay: ~46 → ~49 quadros/s (46,5–51)**, GPU 77–84%, GS ocupado ~100%.
     - Limite que sobra no replay: a GPU de novo (`wait ring` ~0,5 s a cada 2 s, GPU ~82% ocupada), com ~2,3 M texels de textura decodificados por campo.
   Texto original: **GS: CPU por primitivo 5–8× menor.** Hoje cada triângulo vira um registro de 256 bytes montado e copiado para a memória da GPU, com varreduras de conflito por primitivo.
   - Estado do desenho (TEX0, ALPHA, TEST, FRAME...) num bloco à parte, deduplicado; o triângulo só guarda vértices (~32 bytes) e o índice do estado.
   - Tiras e leques de triângulos como um registro só (vértices compartilhados), em vez de um por triângulo.
   - Conferência de conflitos por mudança de estado, não por primitivo.
   - Três slots de envio: a CPU monta o lote seguinte enquanto a GPU desenha.
   - Meta: CPU do worker de ~19 para ~5 ms por campo pesado; continua bit a bit igual nos replays.
3. ✅ **VU1 numa thread própria (10/11, ARQUITETURA §2):** gameplay ao vivo **40,3 → 45,8–46,1/s** (43,5–48), imagem certa, `gs` 44/33 (as mesmas falhas), `vu1` 42/0, `vu` 44/0. Agora o GS está 100% ocupado e a GPU 73–76%: a CPU do `GSWorker` voltou a ser o limite (passo 2). Detalhe que pegou: o canal do GIF enfileira PATH3 sem drenar e conta com o drain do fim do `processPendingTransfers`, onde em linha os PATH1/PATH2 do VIF1 saem antes (o árbitro ordena por prioridade); a VU1Thread drena no primeiro envio e no fim de cada lote para manter essa ordem. Texto original do plano: **VU1 numa thread própria (como o MTVU do PCSX2).** O VIF1 entrega o microprograma e a GameThread segue; ela só espera onde lê o resultado (memória/registradores da VU1, `VIF1_STAT`, `FLUSH*`, `CHCR` do canal 1). Determinístico: a ordem dos pacotes ao GIF continua a do fluxo VIF1. Tira ~30–45% da GameThread.
4. **Avaliação de 10/11 (replay da `fase3`, `--sync 0`, ~4,3 s em 12 repetições; GPU ~16,5 ms reais por campo, ao vivo ~49/s):**
   - GPU por tipo de trabalho (tirando cada dispatch, só para medir): triângulos em tiles ~40%, decodificação de textura ~12%, sprites ~8%, cópias de snapshot ~6%, uploads/cópia do campo/sincronização ~3% cada.
   - Dentro dos tiles: o laço das listas já não custa nada; amostrar textura ~0,9 s, cobertura e montagem ~0,35 s, o resto escrita.
   - Tentados sem ganho e desfeitos: `texCache` num binding só de leitura (cache L1 da NVIDIA); cache de texturas persistente entre envios (o jogo reenvia as texturas a cada campo: decodificações iguais, e o cache cheio força espera); decodificar só depois de área suficiente (ler da VRAM custa bem mais que decodificar: proporção 1 → 6,9 s, sempre decodificar → 4,3 s); tabelas do `pixelAddress` empacotadas em bits; unificar triângulo e sprite numa só chamada de `sampleTexture`/`writePixel` (zera a pilha de 1 KB por invocação que o driver mostra, mas fica ~3% mais lento).
   - Estatísticas do driver (`VK_KHR_pipeline_executable_properties`): o shader de tiles usa 40 registros (ocupação boa) e 202 KB de binário. A pilha de 1 KB vem das chamadas reais de `sampleTexture`/`writePixel` (duas chamadas para cada uma) e não pesa.
   - Uso útil das invocações nos tiles: ~42% (área dos triângulos ÷ 64 × pares tile×primitivo). É o que o rasterizador recupera: estimativa de −15 a −20% da GPU.
   - A GTX 1050 tem `VK_EXT_fragment_shader_interlock` (pixel) e `VK_EXT_conservative_rasterization`.
5. ✅ **Rasterizador exato (10/11, ARQUITETURA §3):** `fase3` 4,45 → **3,37 s (−24%)**, seis replays bit a bit iguais, `gs` 44/33 com e sem. Ao vivo a GPU caiu de 77–84% para **60–68%** ocupada, mas os quadros ficaram em 42–48/s, com "jogo esperando o GS" em 0%: o limite agora é a GameThread (IOP ~20% dela, passo 6). Achados: rasterização conservadora + interlock ~2× mais lenta nesta placa (dilatação no vertex shader resolve); a VRAM precisa de `coherent`; o retângulo em dois triângulos desenhava a diagonal duas vezes. Plano original: **GPU: triângulos pelo rasterizador da GPU, exato.** Desenho dos registros atuais por `vkCmdDraw` (vértices tirados do registro no vertex shader; posições x0,y0,x1,y1 nas palavras 60–63), rasterização conservadora, e o fragment shader roda o mesmo `trianglePixel`/`sampleTexture`/`writePixel` entre `beginInvocationInterlockARB`/`end`: a ordem por pixel continua a do GS, a cobertura exata continua a do teste de arestas, e as regras de corte de lote ficam. Começar só por triângulos, ligado por variável e comparado bit a bit nos seis replays. Texto antigo: **GPU: triângulos pelo rasterizador da GPU.** Vertex/fragment shader para os casos comuns (triângulo texturizado com blend e Z), o compute atual só para o resto. É o maior ganho de GPU (o shader hoje leva ~12 ms por campo na GTX 1050) e o maior risco: cobertura de pixel e interpolação precisam bater com a CPU; se não baterem bit a bit, decidir uma tolerância medida nos replays.
6. 🔶 **Thread do jogo (10/11).** Perfil da GameThread na gameplay com o rasterizador: IOP ~20% (interpretador, ~4,7 M instr/s a ~40 ns cada, custo espalhado; as faixas mais quentes são as travas de interrupção e o laço dos 64 canais do driver de som do CRI, trabalho real, não espera), **microprogramas da VU0 ~12% no interpretador**, `libc` ~10%.
   - ✅ **JIT na VU0** (ARQUITETURA §2): o mesmo JIT da VU1, com a geração de código da VU0 e os 16 pares antes do fim do programa interpretados (a EE lê MAC/STATUS depois do `VCALLMS`). `PS2X_VU_JIT_CHECK=20` ao vivo: 12,5 M blocos conferidos, 0 diferenças. Gameplay **~44 → ~51 quadros/s (46,5–54)**; a VU0 interpretada caiu à metade.
   - IOP em thread própria descartada por ora: a IOP lê e escreve a memória da EE no meio da fatia (`readGuest`/`writeGuest`), então rodar em paralelo quebra o determinismo do lockstep.
   - Cache por stub no `IopImportRegistry::decode` (antes varria até 64 KB para trás a cada chamada de import): correto, sem ganho medido (~51 quadros/s igual); fica para evitar o pior caso.
   - O que a IOP executa (histograma de PC ao vivo, 1,4 G instruções): trabalho real do driver de som do CRI, não espera. ~16% são cópias byte a byte desenroladas de campos por canal (`0x72380–0x73300`), ~9% as travas aninhadas em volta de `CpuSuspendIntr` (`0x58340–0x58400`), um laço sobre os 64 canais (`0x57d80`). Não há laço ocioso para pular.
   - **Medida rápida da IOP (10/11):** ns por instrução dentro do `runCycles` em 70 s de boot e menus, mais um hash da RAM da IOP a cada 2^26 ciclos para provar que o comportamento não mudou. Linha de base 21,8 ns.
     - Busca única da instrução (`executeInstruction(cpu, instruction)`; o `step` já lia a palavra para conferir stub de import): **21,8 → 18,8 ns**, hashes iguais.
     - Escritas na RAM em linha (`store8/16/32`): exatas, mas sem ganho fora do ruído; desfeito.
     - `EeScheduler::processPendingEvents` construía um `std::deque` local a cada chamada (a libstdc++ aloca mapa e bloco até vazio): agora reaproveita `m_eventsScratch`.
     - Ao vivo: IOP 21% → 18% e `libc` 10% → 8,7% da GameThread; quadros/s 47–54,5, dentro do ruído (~50).
   - ✅ **JIT da IOP** (`iop_jit.cpp`, ARQUITETURA §2): blocos de ALU, loads/stores na RAM e desvio + delay slot. **21,8 → 11,6 ns por instrução** nos menus, 84% das instruções no código gerado, hash da RAM da IOP idêntico ao interpretador. Na gameplay a IOP caiu de ~18% para ~11% da GameThread (7% disso é achar e conferir o bloco: `unordered_map` + `memcmp` das palavras). Quadros/s 46–56,5 (média ~50,5): **o GS voltou a 96–97% ocupado**, ele é o limite de novo.
   - Para depois: conferir blocos por geração de escrita das páginas em vez de `memcmp`; MULT/DIV e entrada com load pendente (10% das tentativas) no JIT.
   - Histórico: recompilar a IOP (o interpretador já está perto do que um `switch` dá; os ~19 ns restantes estão espalhados). O custo (~40 ns no começo) está espalhado pelo `step`/`runCpu`/`executeInstruction` (busca dupla da instrução, checagens de sentinela, interrupção e DMA por instrução). Caminho: blocos básicos decodificados em cache, com as checagens por bloco; depois, se preciso, recompilar a IOP.
7. 🔶 **Pipeline do GS ao vivo (10/11).**
   - ✅ **Três slots de envio** (eram dois): no replay o worker esperava ~35% do tempo o slot de duas levas atrás, com a GPU a 79%. Com três: GPU 96%, `fase3` 5,29 → 4,53 s (12 repetições, `--sync 0`), seis replays iguais.
   - ✅ Memo da última textura no `QueuePrimitive` (a divisão do `unordered_map` aparecia no perfil): CPU do worker −5%.
   - Ao vivo quase igual (43,5–55,5, média ~51): CPU por thread na gameplay GSFrontend 83%, VU1Thread 80%, GameThread 67%, GSWorker 66%, num i7-7700HQ (4 núcleos, 8 threads). 69% do GSFrontend é a espera ativa do anel; encurtá-la de 100 para 10 µs não mudou nada (49,8 contra 51,7, dentro do ruído).
   - **Quem espera quem ao vivo (10/11, cronômetros temporários em cada espera, gameplay a ~52/s):** GameThread **~45% esperando a VU1Thread** (`ps2_vu1_thread::sync`), 0% esperando o frontend, 0% na cadência; GSFrontend ~25% trabalhando e ~75% esperando dados; GSWorker ~30% esperando a GPU e ~5% a fila. **Todas** as esperas da GameThread vêm de um só ponto: leitura do **`VIF1_STAT`** (`readIORegister`, 0x10003C00), ~160 por segundo (≈3 por campo), ~3 ms cada. O jogo consulta o VIF1 esperando a VU1 terminar o que recebeu; para o valor não depender do tempo do host, a leitura espera todo o trabalho pendente. A espera é legítima (no PS2 a EE também espera ali): **o elo é a velocidade da VU1Thread.** Por isso o JIT da IOP e a cópia do frontend ficaram de lado: aceleram threads que esperam, não a que segura.
   - ✅ **Cache das tabelas decodificadas da VU1 por conteúdo (11/11):** perfil ao vivo da VU1Thread com ~44% em decodificação (`decodeLowerUsage`, `getDecodedInstructionPairForPc`, `decodeInstructionPair`, `decodeUpperUsage`): cada troca de programa subia a geração do código e o `rebuildDecodedCodeCache` decodificava os 2048 pares de novo, para os mesmos 4 programas. Agora as tabelas ficam guardadas por interpretador (mapa `thread_local` no `.cpp`, sem mexer no header) com uma cópia do código, e são reaproveitadas quando o código bate byte a byte. `vu1` 42/0, `vu` 44/0. Gameplay **~52 → ~55 quadros/s** (48,5–58).
   - Perfil da VU1Thread depois do cache (11/11): código do JIT 21,5%, `runJitBlock` 21% (os laços do modelo de ciclos sobre as leituras e escritas do bloco; o `readyMax` já é deslizante, é trabalho real), `progressXgkick` 11%, `processVIF1Data` 10%, compilação/tabelas do JIT ~5%.
   - ✅ `progressXgkick`: o `% m_activeVuDataSize` por qword copiado era uma divisão de verdade (43% da função); a memória de dados é 4 ou 16 KB, então virou máscara. Ao vivo 56,3 de média (53–60,5), dentro do ruído da rodada anterior (55,0).
   - ✅ `processVIF1Data` (UNPACK): `writeIndex % wl` e `/ wl` por vetor eram divisões de verdade (o ponto mais quente da função); viraram contadores. Função 10,2% → 8,7% da VU1Thread.
   - ✅ `prepareJitTable`: o hash de 16 KB por troca de programa era uma cadeia serial de multiplicações; quatro faixas independentes. Saiu do topo do perfil (1,9%).
   - ❌ Leituras repetidas no `runJitBlock` (11/11): a conferência de prontidão na entrada é ~metade do `runJitBlock` (~10% da VU1Thread). Tirar as leituras repetidas do mesmo registrador (só a primeira pode travar) é exato (conferidor: 7,3 milhões de blocos, 0 diferenças, 0 de ciclos), mas o `runJitBlock` foi só de 21,3% para 20,8%: quase não havia repetição. Revertido. Os arrays de prontidão (~1,2 KB) cabem no L1; o custo é o laço com desvios e loads dependentes, não falta de cache, então compactar o layout (mexe no `ps2_vu1.h` e recompila 13 mil arquivos) não deve render. Não feito.
   - ❌ Recompilação estática dos microprogramas: não feita. O custo de `runJitBlock` (21%) são as leituras e escritas dos arrays de prontidão do modelo de ciclos, não o despacho; código estático pagaria o mesmo. Ao vivo 55,4–55,7 (ruído ±4).
   - ✅ **Uma thread pesada por núcleo físico (11/11):** com `PS2X_PERF=1` o GS aparecia 96% ocupado e a CPU por thread somava ~3,2 núcleos num processador de 4 núcleos e 8 threads. O agendador do Linux punha duas das quatro threads pesadas (GameThread, VU1Thread, GSFrontend, GSWorker) nos dois hyperthreads do mesmo núcleo, e as duas andavam devagar. Fixar cada uma num núcleo físico (`ps2_perf::pinHeavyThread`, lido de `/sys/.../thread_siblings_list`): **54,2 → 59,3–59,6 quadros/s** na gameplay (três rodadas fixadas contra uma de controle na sequência), quase sempre entre 59 e 60. `PS2X_PIN=0` desliga; com menos de 4 núcleos não faz nada. Por isso os ganhos da VU1 dos passos anteriores não apareciam no fps: o elo era a disputa de núcleo.
   - ✅ JIT da VU1 sem `clampOperand` nos operandos que o próprio bloco produziu com FMAC/ITOF/ABS (nunca são Inf/NaN): conferidor com 12,6 milhões de blocos, 0 diferenças. Ganho não mensurável ao vivo (abaixo do ruído).
   - ✅ Worker do GS (11/11, perfil no replay `fase3`): `noteSourceWrite` varria as 32 origens de CLUT vigiadas a cada escrita de quadro e Z de todo primitivo (~1/4 do `QueuePrimitive`). Limites `watchLo`/`watchHi` pulam a varredura quando a escrita não chega perto de nenhuma: `QueuePrimitive` 33,6% → 24,7%, CPU do worker −12%; hashes dos seis replays iguais. Ao vivo, com as threads fixadas, o GS fica ~94% ocupado e as imagens novas a 52–57/s com o jogo a 59–60: o worker e a GPU são a folga que resta.
   - ❌ Imagens novas na janela (11/11): com o jogo a 59,5–60 quadros/s, a janela mostra ~54–55 imagens novas por segundo. Uma fila de um quadro entre o worker e o `Present` (o quadro que termina antes do anterior ser mostrado espera em vez de sobrescrever) deu 55,0: a perda não é de dois quadros dentro de um refresh. Revertido. Suspeita seguinte: a cadência irregular de término dos quadros no worker (GS ~94% ocupado).
   - ✅ **Causa achada (11/11):** contando o salto do número do quadro a cada `Present`, ele só era chamado ~55 vezes por segundo: a janela pegava quadro novo só quando o contador de vsync do convidado mudava, e com os dois a ~60 Hz alguns refreshes não viam mudança. Latch a cada quadro da janela: **imagens novas 54,5 → 58,5/s** com o jogo a 59,9 (capturas de tela conferidas).
   - ✅ `kSlots = 2` derrubava o jogo no boot: o `Initialize` criava `capture[0]`, `[1]` e `[2]` à mão, e com dois slots `capture[2]` escrevia além do array, por cima dos vetores do lote de sprites. Agora é um laço sobre `kSlots`. Com dois slots o jogo roda a 60,0, mas a janela mostra 48,7 imagens novas por segundo contra ~54,5 com três: fica três.
   - Próximo: VU1Thread mais rápida (perfil de 10/11: `runJitBlock` 20%, código do JIT 22%, `progressXgkick` 11%, `processVIF1Data` 10%; recompilação estática dos 4 microprogramas desenhada no item da Fase 6).
   - Histórico: achar o elo ao vivo. Candidatos: o trabalho real do GSFrontend (`GSCpuBackend::Submit` copiando o lote, ~14%), a VU1Thread (80%, JIT da VU1) e a disputa de núcleos com hyperthreading.
8. **IOP mais barato:** HLE das funções de som que o CRI consulta em laço (`sceSdBlockTransStatus` e companhia) ou IOP em thread própria com fatias de tempo emulado. ~12% da GameThread.

Expectativa: 1+2 tiram o GS do caminho (~45 quadros/s, limitado pela GameThread); com 3, ~55–60; 4 e 5 dão a folga para os trechos pesados e para a cutscene.


- `PS2X_PERF=1`: uma linha a cada 2 s (janela, imagens novas, quadros do jogo, vsync, GS ocupado, jogo esperando o GS). "GS ocupado" conta tempo de parede, inclusive esperando a GPU.
- `PS2X_PROFILE=<s> PS2X_PROFILE_THREAD=<nome>`: perfil por amostragem só de CPU (esperas não aparecem). Threads: `GameThread`, `GSFrontend`, `GSWorker`, `GSBand`.
- Replay: `ps2x_gs_replay gsrec/<x>.gsr --repeat 3` (tempo por campo) e `--compare` contra a VRAM da CPU (`PS2X_GS_GPU=0 ... --dump-vram`). Toda mudança no GS precisa de **0 bytes de diferença** em `titulo`, `menu`, `dificuldade`, `fase` e `fase2` (hash conhecido de `fase2`: `edef4ac4b8f25a78`).
- Ao vivo até a gameplay: `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,...,x@120-120.3"`; a gameplay começa ~95 s depois da fase carregar (~250 s de execução). A seleção de personagem às vezes demora e o script perde os toques.
- Máquina livre: conferir `pgrep -a ps2EntryRunner` e `top` antes (outro processo pesado já falseou várias medições). Com a tela travada a janela não abre.

### Fase 0: medir rápido

- ✅ `ARQUITETURA.md` e `CLAUDE.md`/`AGENTS.md` com o mapa do sistema.
- ✅ Gravação e replay do GS (`PS2X_GS_RECORD`, `ps2x_gs_replay`), cinco gravações em `gsrec/` (fora do git).
- ✅ Suítes de teste por nome (`ps2x_tests gs vu1 ...`).
- ✅ Medidor `PS2X_PERF` e perfil por thread com threads nomeadas.
- ⬜ **Pular a cutscene de abertura do Goro's Lair.** Medido em 09/11: o jogo já entra no nível aos ~46 s (`func_1BB8A8(0x60)`) e o 3D começa aos ~56 s; o que pesa no teste é a cutscene, até ~120–145 s. Ela não sai com Start. O `Level Select` do jogo (`MODDING.md`) só pula os menus, que já são rápidos. Falta achar o que controla a cutscene (fim forçado ou flag de "já vista").
- ⬜ **Gravação da gameplay** (`fase3.gsr`): a `fase2` é da cutscene; a gameplay tem outro perfil de desenho.

### Fase 1: ganhos rápidos na CPU

- ✅ Janela sem bloquear (`try_lock`) e cópia do campo na troca `0x00`↔`0x46`: janela a 60 Hz.
- ✅ Logs fora do caminho quente (`AGRESSIVE_LOGS` desligado).
- ✅ Pulo do laço de espera (09/11): os dois endereços antigos nunca rodavam na gameplay; o laço quente era o poll do FIELD do GS CSR em `0x2747a4`. Com ele em `idle_loops` e o pulo limitado ao próximo evento dos timers da EE: gameplay média 36,25 → 38,2 quadros/s (35–40), GS ocupado ~97%: o GS volta a ser o limite. O vsync na gameplay é preso por ciclos (EE a ~60% da velocidade, câmera lenta), então quadros/s depende do tempo de host por ciclo emulado.
- ⬜ Cache de texturas decodificadas (`patches/wip_cache_texturas.diff`): sem ganho medido; só fica se o replay provar.
- ⬜ Upload por linhas no `UploadImageLocked`: perdeu importância (uploads de textura vão para a GPU); só os pequenos ficam na CPU.

### Fase 2: desenho na GPU (Vulkan compute)

- ✅ VRAM do GS num buffer da GPU, swizzle no shader, CPU como plano B.
- ✅ Sprites (inclusive bloom que lê o próprio alvo), triângulos com o pixel completo, todos os formatos de textura, CLUT com versões, bilinear, Z, blend, fog.
- ✅ VRAM na memória da GPU sincronizada por faixas; envio sem espera em dois slots; uploads e cópias local→local na GPU.
- ✅ Teste `gs` que compara CPU e GPU byte a byte em cada regra de espera.
- ✅ GPU ligada por padrão (`PS2X_GS_GPU=0` desliga; sem Vulkan cai na CPU).
- ✅ Esperas por CLUT: intervalo exato de blocos dos uploads e upload pequeno espelhado no host (379 → 139 esperas por campo na `fase2`).
- ✅ Carga de CLUT na GPU quando a origem tem escrita pendente (10/11, ver passo 2 do plano da gameplay).
- ✅ Os 800 sprites de feedback por campo da `fase2`: já vão para a GPU (conferido em 09/11 com contador: nenhum primitivo da `fase2` cai na CPU).
- ✅ CPU do `GSWorker` no replay (09/11): por campo da `fase2`, ~12 ms dos ~33 são espera pela GPU (3 flushes); o resto é CPU do worker, que é o gargalo do replay. `QueuePrimitive` varria o mapa do cache de texturas duas vezes por primitivo e refazia o hash da CLUT de 16 em 16 bits: com o limite da faixa e o hash por 64 bits, `fase2` ~33 → ~30,7 ms, bit a bit igual. Sobram `QueuePrimitive` ~23% (divisão do `unordered_map`, cópia do registro para a memória mapeada), `PrepareGpuPrimitive` ~19% (faltas de cache lendo o `batch` que outra thread escreveu) e driver ~12%. Tentados sem ganho e desfeitos: agrupar 64 draws por lock na fila do `GSCpuBackend` (o mutex custava ~9% da thread produtora, mas ela não é o gargalo) e prefetch dos comandos no worker (medida inconclusiva com a máquina carregada).
- ✅ Redução do Z da fase na GPU (09/11): o intervalo da textura do sprite ia até a altura do `TEX0` e parecia ler o próprio alvo; `fase2` 38,3 → 35,5 ms, nenhum primitivo da `fase2` na CPU.
- ✅ Intervalo de blocos pelo retângulo e formato (09/11): uploads, cópias e a carga de CLUT (CSM1) usam os blocos que as células ocupam, não a página inteira nem `CBP..+8`. Esperas por CLUT na `fase2` 540 → 120 em 60 campos (sobra 1 por campo: upload duplo real de uma CLUT CT16 em `0x2850`), `fase2` 35,3 → 30,0 ms, cinco replays bit a bit iguais. Ao vivo a gameplay não muda (~27–34 quadros/s nas duas versões, média 31), porque quem limita é a thread do jogo; "GS ocupado" cai de ~91% para ~82%.
- ⬜ Espera do fim de campo (`flushAll` antes de copiar a VRAM completa): ~9,5 ms por campo na `fase2`, é a GPU executando.
- ⬜ Linhas e pontos ainda na CPU (não aparecem na `fase2`). Na gameplay sobram ~20 sprites/s CT16 que leem o próprio alvo.
- ⬜ Apresentação direto da GPU (hoje a janela recebe uma cópia da VRAM).

### Fase 3: thread do jogo, sem mudar a arquitetura

- ✅ IOP: fila de DMA só quando vence, imports só em `jr $ra`, lotes de 16384 ciclos, AutoDMA no ritmo real, `step()` enxuto (~30% → ~8% da GameThread).
- ✅ VU1 JIT: callouts para `CLIP`/`DIV`/EFU/`FC*`/`ILW`/`ISW`, leitores de Q no meio do bloco (com variante sincronizada), `XGKICK`/`XTOP` no JIT, desvios, tabelas por microprograma, `readyMax`; pares interpretados 62 → 6 milhões em 3 min. Conferidor `PS2X_VU_JIT_CHECK` sem diferença.
- ✅ VU1 JIT, callouts de dentro dos blocos (09/11): por 10 s de gameplay eram ~14 M de correções exatas de MADD, ~12 M de desvios, ~5,5 M de pares que gravam o VI antigo e ~3,4 M de `ILW`/`ISW`. Agora: correção 18, gravação 0, desvios 4 M (os do primeiro par do bloco), `ILW`/`ISW` 0. Sobram `DIV` 7,3 M, `FCAND` 3,7 M, `CLIP` 3,7 M. Conferidor: 0 diferenças em 17,8 M de blocos.
- ⬜ `IBNE` no primeiro par do bloco (3,9 M/10 s) e `DIV` (7,3 M/10 s) nativos.
- ✅ Frontend do GS na thread `GSFrontend` (tirou ~21% da GameThread; ganho em quadros/s só depois que a GPU deixar de ser o limite).
- ⬜ **Custo fixo de entrada/saída dos blocos do JIT** (`runJitBlock` ~10%, código do JIT ~8,5%): blocos maiores, menos conferências por bloco.
- ⬜ **IOP: HLE das funções do LIBSD que o CRI chama em polling** (`sceSdBlockTransStatus` e companhia): corta boa parte dos ~8%.
- ⬜ **libc na GameThread** (mutex/memcpy/malloc ~8,7%): o mutex por primitivo saiu com o `GSFrontend`; medir de novo o que sobrou.

### Fase 4: GPU mais rápida (o próximo ganho grande)

Hoje o `GSWorker` passa ~70% do tempo esperando a GTX 1050 terminar o `gs_tiles.comp`. Mais threads na CPU não ajudam: a fila já está cheia, quem demora é a GPU.

- ✅ **Onde vai o tempo na `fase2`** (replay, 44,8 ms/campo, medido trocando partes do shader): o desenho do shader é ~24,5 ms; o resto (~20 ms) é decodificar, montar lotes e despachar (com o shader vazio: 20,3 ms). Dentro do shader, a leitura de textura (bilinear: 4 leituras na VRAM com swizzle e 4 da CLUT) custa ~10 ms e a escrita do pixel ~5 ms. O Z em `double` (temia-se o fp64 lento da GTX 1050) e a janela de colunas por pixel quase não pesam (0,5 e 2 ms); `restrict` nos buffers não muda nada.
- ✅ **Tiles de 8×8** em vez de 16×16: 44,8 → 42,8 ms, bit a bit igual.
- ✅ **Cache de textura decodificada na GPU** (09/11): `fase2` 42,5 → ~37 ms, os cinco replays bit a bit iguais à CPU. Ao vivo, a gameplay foi de 25–29 para 27,5–30 quadros/s, com "jogo esperando o GS" em 0%: agora quem limita é a thread do jogo. O que custava era a corrente de leituras dependentes (tabela → VRAM → CLUT), não o número delas (CLUT em 32 bits sozinha: 0 ganho). Na `fase2`, chaveando pelo conteúdo da CLUT: ~1100 decodificações em 20 campos (73 M texels), contra 245 M leituras de textura. Pela versão da CLUT seriam 30 mil (1,7 G texels). Detalhes no `ARQUITETURA.md` §3.
- ⬜ **Triângulos pelo rasterizador da GPU** (vertex/fragment), com o pixel do GS em aritmética inteira no fragment shader e a VRAM continuando como buffer. É o ganho maior e o risco maior: a cobertura de pixel e a interpolação têm de bater bit a bit com a CPU. Validar com os cinco replays.
- ⬜ **Sobrepor CPU e GPU de verdade:** com menos esperas, o `GSWorker` pode montar o lote seguinte enquanto a GPU desenha o atual (três slots em vez de dois).

Meta: GS fora do caminho na gameplay (jogo esperando o GS ~0%).

### Fase 5: paralelismo da thread do jogo

Depois da Fase 4, a GameThread (EE + VU1 + IOP em série) é o teto. **Regra para todas as mudanças desta fase:** o resultado tem de ser determinístico, igual a cada execução com as mesmas entradas. Isso é obrigatório para o online P2P (coop e versus, em lockstep ou rollback): um IOP ou VU1 que termina "quando der" muda o estado do jogo entre as máquinas. Paralelizar só com pontos de sincronização fixos, como já é feito no `GSFrontend`.

- **Perfil da GameThread na gameplay (09/11, com o cache de texturas, `PS2X_PROFILE=200`):** VU1 ~50% (código do JIT 10%, `runJitBlock` 8%, pares no interpretador ~15%: `execLower`, `calculateFmacExactResult`, `normalizeFmacResult`, `execUpper`, decodificação; `progressXgkick` 4%), código do jogo 13% (dos quais 4,8% no laço de espera do vsync `0x274688`), IOP 8%, VIF1 3%, e ~11% na `libc`. Na `libc`, a maior parte cai logo depois de um `syscall` de futex no meio de `pthread_cond_signal`, mas um `LD_PRELOAD` que contava as chamadas viu zero vindas da GameThread: a origem continua aberta (o `addr2line` na `libc` sem símbolos dá o exportado mais próximo). Tentado sem ganho medível e desfeito: acordar o `GSFrontend` só quando ele dorme e tirar o `notify_one` do `scheduleEvent`.
- ✅ **Fila do `GSFrontend` sem lock (09/11):** a origem da `libc` na GameThread era o `gsAsyncSubmit` (achado com a varredura de pilha nova do perfil: `gsAsyncSubmit ← GifArbiter::drain ← progressXgkick`, ~7% em `futex`/mutex). Trocada a fila com mutex e `notify_one` por um anel de bytes com espera ativa curta no frontend. Gameplay ao vivo: média 31 → **36 quadros/s** (30,5–39), `libc` 12,8% → 6,6% da GameThread. O GS voltou a ficar perto do limite (ocupado 88–99%).
- **Perfil da GameThread depois disso (09/11):** VU1 ~30% (`runJitBlock` 8,4%, código do JIT 7,7%, `progressXgkick` 4,1%, `execLower` 2,1%, `run` 2,1%, `commitReadyPipelines` 1,8%), IOP ~11,6% (`executeInstruction` 7,7%, `runCycles` 3,9%), laço de espera do vsync `0x274688` 6,6%, `libc` 6,6% (sobram `memcpy` do pacote e `writeIORegister`), VIF1 4,5%, `read64` 3,3%, `checkpointDue` 2,9%.
- ⬜ **Laço de espera do vsync** (`sub_00274688`, 6,6%) e **IOP** (11,6%) são os próximos alvos baratos da GameThread; a GPU precisa cair junto (espera de fim de campo, Fase 4).
- ⬜ **VU1 numa thread própria** (~45% da GameThread com o JIT): o VIF1 entrega o microprograma e o EE só espera onde lê o resultado (VU1 memória/registradores, `VIF1_STAT`, o `CHCR` do canal 1). Medido: o jogo espera o canal 1 ~1,7 ms depois de cada envio de ~8 ms, então sobrepõe pouco sozinho (~15%); vale junto com a próxima.
- ⬜ **IOP numa thread própria** (~8%): ele avança em fatias de tempo emulado combinadas com o EE (por exemplo a cada vsync ou a cada RPC), nunca pelo relógio real.
- ⬜ **Executar o lote seguinte do EE enquanto a VU1 termina o anterior**, quando o jogo não lê nada da VU1 no meio.

Meta: gameplay a 60 quadros/s.

**Vazão sem a trava de tempo real (11/11, `PS2X_UNCAPPED=1`):** menus a 200–250 campos/s, gameplay só **~60–65**, com GS ~90% ocupado e GPU (GTX 1050) 81–84%. O teto atual da gameplay é quase exatamente 60: não há folga. Para 90 seria preciso ~1,5× mais vazão no worker do GS **e** na GPU; a GameThread (IOP, VU0) não é o limite, então JIT da IOP e som nativo não aproximam dos 90. Além disso, a lógica do MKSM anda um passo por vsync: mais vsyncs por segundo aceleram o jogo. Um modo de 90 fps de verdade precisaria separar lógica e imagem (interpolar quadros ou corrigir o passo de tempo do jogo), fora do alcance atual.

### Fase 6: rumo ao 100% nativo

Trocar o que hoje é emulado por código nativo, sem perder o comportamento do jogo. Cada troca precisa de um teste que compare com o emulado.

- ⬜ **Som:** o CRI ADX/AIX hoje roda dentro do IOP emulado (decodificação no EE, mistura no IOP, saída por AutoDMA). Um decodificador e mixer nativos no host eliminariam o IOP do caminho do som. Atenção às armadilhas já achadas (`notas/audio.md`): ordem L/R, criptografia do ADX, `CpuSuspendIntr`.
- ⬜ **IOP inteiro em HLE:** cartão de memória, controle e CD já são HLE em parte. Com o som nativo, o IOP restante pode virar funções do host.
- 🔶 **VU1 recompilada estaticamente** (engenharia reversa feita em 10/11, implementação não começada):
  - **Os programas:** o jogo usa **4 microprogramas de 16 KB**, sempre enviados como 8 `MPG` de 2 KB (destinos `0x0000`–`0x3800`), mais um stub de 8 bytes em `0x0000`. Em ~300 s: A 47 254 envios, B 34 824, C 12 410, D 2 403, ou seja ~320 trocas de programa por segundo (~5 MB/s de microcódigo). Só 14% dos `MPG` regravam o que já estava lá. Os 55 estados de memória de código vistos são combinações desses pedaços; o JIT já guarda tabelas por hash do código (`tablesByCode`). Os programas são código do jogo: ficam fora do git (despejo local, como o `gsrec/`).
  - **Onde vai a VU1Thread** (perfil por thread, gameplay, com a VU1 já em thread): código do JIT 21,9%, **`runJitBlock` 20,3%**, `progressXgkick` 10,6%, `processVIF1Data` (UNPACK) 9,5%, `execLower`/`execUpper` ~7%, **`compileJitBlock` + `prepareJitTable` 3,6%**, `commitReadyPipelines` 3,2%.
  - **Por que `runJitBlock` custa tanto:** a cada entrada de bloco ele aplica o modelo de ciclos da VU1 (quando cada VF/VI/ACC fica pronto, trava de FDIV/EFU, variante com Q sincronizado, `XGKICK` em andamento) e grava os ciclos de saída. É isso que mantém Q, P e o envio ao GIF iguais ao hardware.
  - **Desenho:** um tradutor (script, como o `ps2xRecomp` do EE) que gera uma função C++ por programa, com os pares de instrução em ordem e `goto` nos desvios. Os quatro são reconhecidos pelo hash no `MSCAL`; programa desconhecido cai no JIT/interpretador. O modelo de ciclos precisa ir junto, estático: como o programa é fixo, o tradutor calcula em tempo de geração as latências entre pares do mesmo bloco, e só os limites de bloco consultam o estado de ciclos. Conferência: rodar o gerado e o interpretador lado a lado (como o `PS2X_VU_JIT_CHECK`) até 0 diferenças.
  - **Ganho esperado:** ~20–25% da VU1Thread (entrada/saída de bloco e compilação). Hoje a VU1Thread usa ~73% de um núcleo e não é o único limite: a linha jogo → VU1 → frontend do GS → worker → GPU está equilibrada (GSFrontend 72%, GameThread 60%, GSWorker 54% + esperas da GPU). Por isso o ganho em quadros/s vem só junto com o worker do GS (passo 2).
- **Medição por thread (10/11, gameplay a ~46/s):** VU1Thread 73%, GSFrontend 72%, GameThread 60%, GSWorker 54% (o resto esperando a GPU).
- ⬜ **GS:** com a Fase 4 feita, o desenho já é nativo na GPU; falta só a apresentação direto.

### Histórico das medições

Resumo, em ordem. Os detalhes de cada correção estão no `PLANO.md` (seção 5A) e no `git log`.

- **Início (runs 134–152):** ~2 fps visuais no menu e ~0,5 na fase, tudo desenhado na CPU. Os números visuais eram piores que o ritmo do jogo por causa da janela presa no lock do GS e da cópia tirada no meio do campo; corrigido no item 1 da Fase 1 (janela 20–35 → 60 fps).
- **Fase 2, blocos 1–2 (03/11):** sprites e triângulos na GPU, bit a bit iguais. Com a máquina livre a Intel HD 630 perdia da CPU; a GTX 1050 ganhava pouco (fase 36,8 ms na CPU contra 44,7 ms na GPU).
- **Bloco 3 (04/11):** esperas só em conflito real e CLUT com versões: fase 36,8 → 27,7 ms; menus a 47 campos/s ao vivo.
- **Bloco 4 (04/11):** VRAM na memória da GPU (antes a GPU lia pelo PCIe): fase 27,7 → 20,8 ms. A fase passou a ser limitada pela thread do jogo.
- **Bloco 5 (04/11):** IOP sem custos por instrução, JIT da VU1 de 68% para 84% dos pares, ciclos da VU1 pulando até o próximo evento, sem zerar 64 KB por XGKICK: menus a 60, fase 17–22 → 26–37.
- **Bloco 6 (04–05/11):** pipeline da VU1 em cache, envio da GPU sem espera, uploads de textura na GPU: fase 31–39 ao vivo. AutoDMA no ritmo real: 34–41.
- **Raiz quadrada corrigida (05/11):** a fase passou a desenhar a cena certa e caiu para 14–20. As medidas anteriores eram de uma câmera olhando para uma parede.
- **Fase real, rodadas 1–3 (05/11):** gravação `fase2.gsr` (13 MB de comandos por campo); flushes por campo ~75 → ~19, replay 60 → 49 ms; JIT da VU1 até 93,7% dos pares, depois pares interpretados 62 → 6 milhões; IOP 12,6% → ~8%. Descoberta: o que se media era a cutscene de ~95 s; a gameplay roda a 24–28 quadros/s. VIF1 assíncrono sozinho sobreporia só ~15%.
- **GPU ligada por padrão (05/11):** sem a variável, a fase era desenhada na CPU: 7,5 → 25–27,5 quadros/s na cutscene. Cinco replays com 0 bytes de diferença.
- **Frontend do GS em thread própria (05/11):** −21% de CPU na GameThread, quadros/s iguais (A/B com `PS2X_GS_ASYNC=0`): o limite já era a GPU.
- **Shader da GPU (05/11):** partição do tempo da `fase2` (acima, Fase 4) e tiles de 8×8: 44,8 → 42,8 ms.
- **Fila do `GSFrontend` sem lock (09/11):** gameplay 31 → 36 quadros/s de média; `libc` na GameThread 12,8% → 6,6%.
- **Blocos exatos de uploads e CLUT (09/11):** `fase2` 35,3 → 30,0 ms; ao vivo igual (limite na thread do jogo), GS ocupado 91% → 82%.
- **CLUT sem esperar a GPU (05/11):** 339 de 379 esperas vinham de cargas de CLUT presas a uploads; intervalo exato dos uploads e espelho no host: 379 → 139 esperas, `fase2` 51 → 44 ms. O tempo total de espera quase não caiu (686 → 550 ms em 888): o resto é a GPU executando.
- **XGKICK em blocos (11/11):** dentro dos dados de um GIFtag, `progressXgkick` copia de uma vez todos os qwords que o crédito de ciclos paga (todas as leituras de uma chamada veem a mesma memória da VU). Perfil da `VU1Thread`: `progressXgkick` 11,1% → 8,6%, mas a `libc` (memcpy) sobe 6,8% → 10,3%; ganho pequeno, porque a função roda a cada ciclo e quase sempre copia um qword só. Conferidor do JIT: 12,6 M blocos, 0 diferenças; gameplay 58,3 quadros/s (perto do teto de 60).
- **XGKICK adiado (11/11):** dentro dos dados de um GIFtag, longe do último qword, o PATH1 só deve leituras; `advanceOneCycle` guarda o ciclo como crédito e as leituras acontecem antes do próximo commit de store na memória da VU, no fim da tag e no fim de cada `run` (o VIF pode escrever entre execuções). Mesmos bytes, muito menos chamadas. `progressXgkick` sai do topo do perfil da `VU1Thread` (8,6% → <3,7%), `libc` 10,3% → 8,3%; gameplay 59,8 quadros/s; conferidor do JIT 13,6 M blocos, 0 diferenças; teste "XGKICK observes stores committed before a future PATH1 qword" passa.

## Teste do usuário (12/11): seis problemas

1. **Imagem pixelada.** ✅ (filtro) A janela ampliava o quadro de 640x448 sem filtro. Filtro bilinear ligado (`ps2_runtime.cpp`). Falta: renderizar em resolução interna maior (upscale no GS da GPU).
2. **Chiado no canal direito nos filmes.** Ver o áudio do Sofdec (ADX/PCM intercalado do SFD): ordem L/R ou um bloco errado no canal direito.
3. **Ataques com atraso.** Medir a latência do pad até a imagem: fila do GS assíncrono, buffer de vsync e leitura do pad a cada quadro.
4. **Texturas faltando, dá para ver através das paredes.** Gravar um `.gsr` no lugar e achar a primitiva descartada (teste de Z, clipping ou textura não enviada).
5. **Cores diferentes do original.** Comparar com os prints: modulação de cor da textura (TFX/TCC), alfa e níveis de cinza no shader da GPU.
6. **Cutscene do Raiden em câmera lenta.** Medido: o vsync caía para 36–55/s porque o relógio do EE só anda com o trabalho da CPU. Pulo de quadro no `EeScheduler` (ver ARQUITETURA §tempo): agora 55–60/s, e a janela parou de oscilar entre 60 e 7 fps. Sobram duas quedas de ~4 s a 36/s, presas na VU1Thread (o JIT da VU1 e o VIF1 dominam o perfil). Próximo: perfilar a VU1Thread nessas quedas. O item 3 (golpes lentos) provavelmente era isso; conferir com o usuário.

### Comparação com prints do jogo original (12/11)

Prints do YouTube guardados em `../referencias_original/` (fora do git: imagens do jogo), contra `PS2X_SCREENSHOT` e captura da janela da mesma cena (Goro's Lair, Raiden):
- **Conferido no replay:** CPU e GPU dão o mesmo hash na `fase3`, e a mistura dos dois circuitos (PMODE) já está no `Present`. A diferença não vem da GPU nem da saída de vídeo.
- ✅ **Campos misturados na janela** (`PS2X_DEINTERLACE=0` desliga): média do campo atual com o último da outra página, como a TV. Tira o tremido entre campos.
- **Causa principal provável do aspecto "pixelado/ruidoso": o mipmap não é emulado** (PLANO 5A.25: só amostramos o nível 0). Paredes e chão distantes usam `MMIN`>1 com `MIPTBP1/2`; amostrar só o nível 0 com ponto dá exatamente o ruído de blocos que se vê nas capturas, e o original é liso. **Próximo passo:** emular LOD (`LCM`, `L`, `K`, Q por pixel) e os níveis `MIPTBP1/2` no `gs_tiles.comp` e no backend CPU, com teste.
- **Falta o desfoque/brilho de tela cheia.** O original é todo suave, com brilho em volta das luzes e dos personagens. O nosso sai nítido e duro. É a mesma suspeita da trilha C: um passe que reduz o quadro, desfoca e mistura de volta não está saindo (ou sai com alfa 0). Isso também explica a reclamação de "pixelado" e parte das "cores diferentes". Próximo: num `.gsr` da fase, listar os desenhos que leem o próprio framebuffer (página `0x00`/`0x46` como textura) e conferir o alfa e o resultado de cada um.
- **Raios de luz:** no original, feixes suaves em degradê; no nosso, placas claras chapadas no alto da tela.
- **Chão:** no nosso, riscos pretos serrilhados nas rachaduras; no original, sombras suaves. Provavelmente é o mesmo passe de mistura.
