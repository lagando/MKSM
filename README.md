# Mortal Kombat: Shaolin Monks — PS2 Recomp

> **Recompilação estática** do Mortal Kombat: Shaolin Monks de **PlayStation 2** (USA, `SLUS-210.87`) para PC (Linux e Windows), feita com o [**PS2Recomp**](https://github.com/ran-j/PS2Recomp).

Isto **não é um emulador** e **não é um port** com gráficos refeitos. O código MIPS do PS2 (o ELF do jogo) é traduzido para C++ e compilado como programa nativo. O hardware que não é CPU (GS, VU, DMA, IOP, SPU2) continua emulado pelo runtime do PS2Recomp.

## Progresso

**~45% concluído** (estimativa do autor, atualizada em 04/11/2026)

```
[█████████░░░░░░░░░░░] 45%
```

| Etapa | Estado |
|---|---|
| Recompilar o ELF (13.651 arquivos C++) e compilar o runtime | ✅ Feito |
| Boot, threads, leitura de disco (CRI ADXF / `GAMEDATA.WAD`) | ✅ Feito |
| Telas de save, menu principal, seleção de personagem | ✅ Feito |
| Primeira fase 3D (Goro's Lair) com HUD, pausa e mapa | ✅ Feito |
| Câmera da fase | ✅ Corrigida |
| Música e falas (ADX) | ✅ Funcionando |
| GS acelerado na GPU, VU1 com JIT x86-64 | 🟡 Em andamento |
| Velocidade cheia (30 fps; hoje ~15–20 na fase) | 🟡 Em andamento |
| Filmes CRI (FMV) | ❌ Tela preta |
| Resto das fases testadas do início ao fim | ❌ Falta |
| Build para Windows | ❌ Falta |

## O que você precisa

- Sua **própria cópia** do jogo (PS2, USA). **Este repositório não contém ISO, ELF, código gerado nem nenhum dado do jogo**, e não ajuda a obtê-los.
- Linux com GCC/CMake (o Windows/MSVC é o alvo principal do PS2Recomp, mas ainda não foi testado aqui).
- Paciência: o build completo do código gerado leva horas.

## Como compilar e rodar

O passo a passo completo (extrair a ISO, gerar o C++, aplicar os patches no PS2Recomp) está em [`ESTADO.md`](ESTADO.md#reproduzir-do-zero-resumo). Depois de pronto:

```bash
cmake --build PS2Recomp/out/build --target ps2EntryRunner
```

```bash
cd PS2Recomp && MKSM_SKIP_INTRO=1 ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87
```

## Estrutura do repositório

| Caminho | O que é |
|---|---|
| `sm.toml` | Configuração do PS2Recomp para este jogo |
| `patches/` | Mudanças no runtime do PS2Recomp (diff + arquivos novos) |
| `scripts/` | Ferramentas de análise e teste |
| [`ESTADO.md`](ESTADO.md) | Estado técnico detalhado e como reproduzir do zero |
| [`ARQUITETURA.md`](ARQUITETURA.md) | Threads, locks, fluxo de um quadro, armadilhas do jogo |
| [`PLANO.md`](PLANO.md) | Diário de cada correção, com evidências |
| [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) | Plano de velocidade (GPU, RAM, testes) |
| [`MODDING.md`](MODDING.md) | Endereços e formatos do jogo, para mods |

## Créditos

- [PS2Recomp](https://github.com/ran-j/PS2Recomp), de ran-j: o recompilador e o runtime.
- Mortal Kombat: Shaolin Monks © Midway / Warner Bros. Projeto de fãs, sem fins lucrativos e sem afiliação.
