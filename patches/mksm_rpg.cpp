// Modo RPG do Shaolin Monks (PLANO_RPG.md). Endereços achados nas tarefas A1-A7 (anotados no plano):
//   XP:          saldo de cada jogador em [0x4D8B80]+0x4BA4+(j-1)*0x60; func_174C30 e o combate (func_1744A0) gravam nele.
//   Dano:        func_284FE0(objeto, dano) desconta a vida em obj+0x398 (máxima em obj+0x3A0); dano <= 0 cura.
//   Jogadores:   objeto do jogador 1 em [0x6826A8], do 2 em [0x6826AC].
//   Vida máxima: save+0x90 (blocos) × save+0x94 (vida por bloco, da tabela 0x4EBC58 por upgrade e modo).
//   Quadro:      func_183978 copia a vida do jogador para o save a cada quadro de jogo.
//   Dificuldade: func_1DAF90(1 Normal, 2 Hard) grava save+0xD4; jogo novo chama de 0x130908 (ra 0x130910), carregar de 0x1304E8.
// Com o modo desligado, cada troca chama a original sem tocar em nada (regra 1 do plano).
#include "mksm_rpg.h"
#include "ps2x/iop/savestate_io.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <iterator>
#include <cstdint>
#include "mksm_itens.inc"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

bool mksmBloomEnabled(); // ps2_runtime.cpp: menu Gráfico / MKSM_BLOOM
namespace mksm_rpg
{
    namespace
    {
        double envNumber(const char *name, double fallback)
        {
            const char *v = std::getenv(name);
            if (!v || !*v)
                return fallback;
            std::string s(v);
            std::replace(s.begin(), s.end(), ',', '.'); // "0,1" no mksm.ini
            char *end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            return end != s.c_str() && std::isfinite(d) ? d : fallback;
        }

        struct Config
        {
            double xpFirst = envNumber("RPG_XP_PRIMEIRO", 300), xpStep = envNumber("RPG_XP_AUMENTO", 100), xpAccel = envNumber("RPG_XP_ACELERA", 50);
            int maxLevel = std::clamp(static_cast<int>(envNumber("RPG_NIVEL_MAX", 100)), 2, 1000);
            int pointsPerLevel = std::clamp(static_cast<int>(envNumber("RPG_PONTOS_POR_NIVEL", 1)), 1, 100);
            int cap = std::max(0, static_cast<int>(envNumber("RPG_TETO_ATRIBUTO", 20))); // 0 = sem teto
            int capWisdom = std::max(0, static_cast<int>(envNumber("RPG_TETO_SABEDORIA", 5))); // a Sabedoria acelera os níveis: teto próprio
            bool loseXpOnDeath = envNumber("RPG_PERDE_XP_AO_MORRER", 1) != 0; // morreu e continuou: perde o XP desde o checkpoint
            // Acréscimo por degrau (usuário, 17/11): +100 até o nível 10, +200 do 10 ao 29, +300 do 30 em diante.
            double xpExtra[3] = {envNumber("RPG_XP_EXTRA_1", 100), envNumber("RPG_XP_EXTRA_2", 200), envNumber("RPG_XP_EXTRA_3", 300)};
            double perPoint[AttrCount] = {envNumber("RPG_FORCA_POR_PONTO", 5),  envNumber("RPG_DEFESA_POR_PONTO", 3),
                envNumber("RPG_VIDA_POR_PONTO", 5),   envNumber("RPG_ROUBO_POR_PONTO", 1),
                envNumber("RPG_REGEN_POR_PONTO", 0.1), envNumber("RPG_SABEDORIA_POR_PONTO", 3)};
                double enemyDamage = envNumber("RPG_INIMIGO_DANO", 30), enemyHealth = envNumber("RPG_INIMIGO_VIDA", 30);
                double enemyPerLevel = envNumber("RPG_INIMIGO_POR_NIVEL", 0);
        };
        const Config &cfg()
        {
            static const Config c;
            return c;
        }

        // Custo do nível k para o k+1 (PLANO_RPG.md 3.2): 300, 400, 550, 750... mais o acréscimo da faixa (400, 500, 650, 850...).
        int64_t stepCost(const Config &c, int k)
        {
            const double extra = k < 10 ? c.xpExtra[0] : k < 30 ? c.xpExtra[1] : c.xpExtra[2];
            const double cost = c.xpFirst + c.xpStep * (k - 1) + c.xpAccel * (k - 1.0) * (k - 2.0) / 2.0 + extra;
            return std::max<int64_t>(1, std::llround(cost));
        }
        // XP ganho em que o nível começa (o 1 começa em 0).
        int64_t levelStartXp(const Config &c, int level)
        {
            int64_t base = 0;
            for (int k = 1; k < level && k < c.maxLevel; ++k)
                base += stepCost(c, k);
            return base;
        }
        // Teto de pontos de um atributo (0 = sem teto).
        int capFor(const Config &c, int attr)
        {
            if (attr != Sabedoria || c.capWisdom == 0)
                return c.cap;
            return c.cap ? std::min(c.cap, c.capWisdom) : c.capWisdom;
        }
        // Nível do XP ganho, com o XP dentro do nível e o custo do degrau (0 no máximo).
        int levelFor(const Config &c, int64_t xp, int64_t *inLevel = nullptr, int64_t *step = nullptr)
        {
            int level = 1;
            int64_t base = 0;
            while (level < c.maxLevel && xp >= base + stepCost(c, level))
                base += stepCost(c, level++);
            if (inLevel)
                *inLevel = level < c.maxLevel ? xp - base : 0;
            if (step)
                *step = level < c.maxLevel ? stepCost(c, level) : 0;
            return level;
        }

        // Fração do efeito de um atributo com n pontos (0,15 = 15%), com os limites de 3.3.
        double effect(const Config &c, int attr, int n)
        {
            const double e = c.perPoint[attr] * n / 100.0;
            if (attr == Defesa)
                return std::min(e, 0.9);
            if (attr == Regen)
                return std::min(e, 0.1);
            return e;
        }
        double enemyScale(const Config &c, double base, int level) { return 1.0 + (base + c.enemyPerLevel * (level - 1)) / 100.0; }
        // Dano que o jogador causa e recebe, já com o Desafio RPG (3.11).
        int32_t dealtDamage(const Config &c, int32_t base, int forca, int level)
        {
            const double d = base * (1.0 + effect(c, Forca, forca)) / enemyScale(c, c.enemyHealth, level);
            return std::max<int32_t>(1, static_cast<int32_t>(std::lround(d)));
        }
        int32_t takenDamage(const Config &c, int32_t base, int defesa, int level)
        {
            const double d = base * enemyScale(c, c.enemyDamage, level) * (1.0 - effect(c, Defesa, defesa));
            return std::max<int32_t>(1, static_cast<int32_t>(std::lround(d)));
        }

        // Um perfil por jogador: no co-op cada um tem XP, nível e pontos próprios.
        struct Player
        {
            int64_t xpGained = 0;
            int points[AttrCount] = {};
            double xpCarry = 0.0, regenCarry = 0.0;
            int shownLevel = 1, levelUps = 0;
            int64_t lastXp = -1; // saldo de XP do jogo visto no último quadro (-1: ainda não visto)
        };
        struct State
        {
            bool active = false, armed = false, coop = false;
            Player players[2];
            double healCarry = 0.0;
        };
        std::mutex g_mutex;
        State g_state;
        std::atomic<bool> g_inStory{false}; // a janela mostra o RPG só numa partida de história ou co-op
        int g_fighter[2] = {-1, -1}; // escolhido na tela de dificuldade (fica para o próximo jogo novo)
        bool g_fighterPending = false; // jogo novo começou: gravar na primeira sala montada

        int spentLocked(const Player &p) { int n = 0; for (int v : p.points) n += v; return n; }
        int levelLocked(const Player &p) { return levelFor(cfg(), p.xpGained); }
        int freeLocked(const Player &p) { return (levelLocked(p) - 1) * cfg().pointsPerLevel - spentLocked(p); }
        Player *playerLocked(int i) { return i >= 0 && i < 2 ? &g_state.players[i] : nullptr; }

        std::filesystem::path cardFile(int port, int slot)
        {
            std::filesystem::path dir = ".";
            if (const char *ini = std::getenv("MKSM_INI"); ini && *ini)
                dir = std::filesystem::path(ini).parent_path();
            return dir / ("rpg_mc" + std::to_string(port) + "_slot" + std::to_string(slot + 1) + ".ini");
        }

        // Arquivo do save (A5): cabeçalho de 0x1328 bytes e 9 slots a cada 0x5430 bytes; os primeiros 0x5400 de cada um
        // são o bloco do save da RAM. (Com passo 0x5400, o slot 2 em diante não batia ao carregar: o RPG não voltava.)
        constexpr size_t kHeader = 0x1328, kSlotStride = 0x5430, kSlotSize = 0x5400;
        static_assert(kHeader + 8 * kSlotStride + kSlotSize <= 198876, "9 slots dentro do arquivo do save");
        std::vector<uint8_t> g_card[2]; // cópia do arquivo do cartão (último lido ou gravado)
        std::string g_cardPath[2];
        std::vector<uint8_t> readFile(const std::string &path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const char *kKeys[AttrCount] = {"FORCA", "DEFESA", "VIDA", "ROUBO", "REGENERACAO", "SABEDORIA"};
        const char *kSuffix[2] = {"", "_J2"}; // as chaves do jogador 2 terminam em _J2

        // Lê o perfil de um cartão. Fronteira de confiança: o arquivo pode ter sido editado à mão.
        bool readCard(int port, int slot, State &out)
        {
            std::ifstream in(cardFile(port, slot));
            if (!in)
                return false;
            const Config &c = cfg();
            State s;
            s.active = true;
            for (std::string line; std::getline(in, line);)
            {
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                const std::string key = line.substr(0, eq);
                const long long value = std::atoll(line.c_str() + eq + 1);
                for (int j = 0; j < 2; ++j)
                {
                    if (key == std::string("XP_GANHO") + kSuffix[j])
                        s.players[j].xpGained = std::clamp<long long>(value, 0, 1000000000000LL);
                    for (int a = 0; a < AttrCount; ++a)
                        if (key == std::string(kKeys[a]) + kSuffix[j])
                            s.players[j].points[a] = static_cast<int>(std::clamp<long long>(value, 0, capFor(c, a) ? capFor(c, a) : 100000));
                }
            }
            for (Player &p : s.players)
            {
                if (spentLocked(p) > (levelFor(c, p.xpGained) - 1) * c.pointsPerLevel)
                    std::fill(std::begin(p.points), std::end(p.points), 0);
                p.shownLevel = levelFor(c, p.xpGained);
            }
            out = s;
            return true;
        }

        void writeCardLocked(int port, int slot)
        {
            const std::filesystem::path path = cardFile(port, slot);
            std::error_code ec;
            if (!g_state.active)
            {
                std::filesystem::remove(path, ec); // um jogo normal gravou por cima: o save deixou de ser RPG
                return;
            }
            std::ofstream out(path, std::ios::trunc);
            out << "# Modo RPG do slot " << slot + 1 << " do memory card " << port + 1 << " (o save fica no mc" << port << "/). Copie junto com o save.\n";
            out << "NIVEL=" << levelLocked(g_state.players[0]) << '\n';
            for (int j = 0; j < 2; ++j)
            {
                const Player &p = g_state.players[j];
                out << "XP_GANHO" << kSuffix[j] << '=' << p.xpGained << '\n';
                for (int a = 0; a < AttrCount; ++a)
                    out << kKeys[a] << kSuffix[j] << '=' << p.points[a] << '\n';
            }
        }

        uint32_t rd32(const uint8_t *rdram, uint32_t addr)
        {
            uint32_t v = 0;
            std::memcpy(&v, rdram + (addr & PS2_RAM_MASK), 4);
            return v;
        }
        void wr32(uint8_t *rdram, uint32_t addr, uint32_t v) { std::memcpy(rdram + (addr & PS2_RAM_MASK), &v, 4); }
        uint32_t saveBlock(const uint8_t *rdram) { return rd32(rdram, 0x4D8B80u); }
        // Lutador de cada jogador na história: save+0xCC (1) e save+0xD0 (2). Sozinho (save+0x88 = 0) só o jogador 1
        // está em campo — o 0xD0 guarda o último escolhido na tela, não um jogador 2. No co-op (0x88 = 1) os dois contam.
        // O objeto em 0x6826AC não serve de sinal: ele existe em solo também, com vida de inimigo.
        int fighterId(const uint8_t *rdram, uint32_t save, int j)
        {
            if (!save || j < 0 || j > 1 || (j == 1 && rd32(rdram, save + 0x88u) != 1u))
                return -1;
            return static_cast<int>(rd32(rdram, save + (j ? 0xD0u : 0xCCu)));
        }
        bool inGameplay(const uint8_t *rdram) // fora de pausa, menu e carregamento (N21)
        {
            return rd32(rdram, 0x4F4B68u) == 1u && rd32(rdram, 0x6BEE2Cu) == 0u && rd32(rdram, 0x4C49E8u) == 0u;
        }

        PS2Runtime::RecompiledFunction g_origBossBlock, g_origFatFull, g_origFat, g_origFatOver, g_origDamage, g_origFrame, g_origDifficulty, g_origRoom;

        // func_1CEE00 monta a sala e lê o lutador de cada jogador em save+0xCC/0xD0 (PLANO_PERSONAGENS.md).
        void hookRoom(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                static const bool testEveryRoom = std::getenv("MKSM_TEST_LUTADOR") != nullptr; // roteiro de teste: também no Versus
                if (g_fighterPending || testEveryRoom)
                    if (const uint32_t save = saveBlock(rdram))
                    {
                        for (int j = 0; j < 2; ++j)
                            if (g_fighter[j] >= 0)
                                wr32(rdram, save + (j ? 0xD0u : 0xCCu), static_cast<uint32_t>(g_fighter[j]));
                        g_fighterPending = false;
                    }
            }
            g_origRoom(rdram, ctx, runtime);
        }

        // XP ganho: a subida do saldo de cada jogador desde o último quadro, com a Sabedoria. O saldo do jogo não muda
        // (regra 6). Medido no quadro porque o crédito do combate (func_1744A0) grava o saldo direto, sem func_174C30.
        // Queda do saldo: com o menu de pausa aberto (Select, 0x6BEE2C; menu do jogo, 0x4C49E8) é compra de golpe, que não
        // tira nível; fora dele é o jogo voltando ao checkpoint (morreu e continuou: medido, o saldo volta ao do checkpoint).
        // Aí o RPG perde o mesmo XP (com a Sabedoria), sem cair abaixo do começo do nível atual: o XP desde o checkpoint
        // não conta duas vezes e os pontos já gastos continuam valendo.
        void creditXpLocked(const uint8_t *rdram, uint32_t save, int players)
        {
            for (int j = 0; j < players; ++j)
            {
                Player &p = g_state.players[j];
                const int64_t now = static_cast<int32_t>(rd32(rdram, save + 0x4BA4u + j * 0x60u));
                const int64_t before = p.lastXp;
                p.lastXp = now;
                if (before < 0 || now == before) // primeiro quadro (ou save recém-carregado) ou nada mudou
                    continue;
                if (now < before)
                {
                    const bool menu = rd32(rdram, 0x6BEE2Cu) == 1u || rd32(rdram, 0x4C49E8u) == 1u;
                    if (!menu && cfg().loseXpOnDeath)
                    {
                        const double scale = 1.0 + effect(cfg(), Sabedoria, p.points[Sabedoria]);
                        const int64_t lost = std::llround(static_cast<double>(before - now) * scale);
                        p.xpGained = std::max(levelStartXp(cfg(), levelLocked(p)), p.xpGained - lost);
                        p.xpCarry = 0.0;
                    }
                    continue;
                }
                const double gain = (now - before) * (1.0 + effect(cfg(), Sabedoria, p.points[Sabedoria])) + p.xpCarry;
                const int64_t whole = static_cast<int64_t>(gain);
                p.xpCarry = gain - whole;
                p.xpGained += whole;
                const int level = levelLocked(p);
                if (level > p.shownLevel)
                    p.levelUps += level - p.shownLevel, p.shownLevel = level;
            }
        }

        // Dano: Defesa e Desafio no que cada jogador recebe; Força, Desafio e Roubo de vida no que causam.
        // Chamada por func_1CBF48 (o golpe: dano base menos a armadura da vítima, vezes a força do atacante) com
        // o objeto do atacante em a2: no co-op, a Força e o Roubo de vida são de quem bateu.
        void hookDamage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t target = getRegU32(ctx, 4), attacker = getRegU32(ctx, 6);
            const int32_t damage = static_cast<int32_t>(getRegU32(ctx, 5));
            int32_t heal = 0;
            uint32_t healed = 0;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                const int32_t targetMax = static_cast<int32_t>(rd32(rdram, target + 0x3A0u));
                // Cura (dano <= 0) e morte instantânea (abismo, fatality: o dano é a vida inteira) passam iguais.
                const uint32_t save = saveBlock(rdram);
                const bool story = save && rd32(rdram, save + 0x88u) <= 1u; // 0 sozinho, 1 co-op; o Versus fica de fora
                const bool normalHit = story && damage > 0 && (targetMax <= 0 || damage < targetMax);
                const uint32_t objs[2] = {rd32(rdram, 0x6826A8u), rd32(rdram, 0x6826ACu)};
                int32_t scaled = damage;
                if (g_state.active && normalHit)
                {
                    const Config &c = cfg();
                    if (target == objs[0] || (target == objs[1] && objs[1]))
                    {
                        const Player &p = g_state.players[target == objs[0] ? 0 : 1];
                        scaled = takenDamage(c, damage, p.points[Defesa], levelLocked(p));
                    }
                    else if (attacker != 0 && (attacker == objs[0] || attacker == objs[1]))
                    {
                        const int who = attacker == objs[0] ? 0 : 1;
                        const Player &p = g_state.players[who];
                        scaled = dealtDamage(c, damage, p.points[Forca], levelLocked(p));
                        const double h = scaled * effect(c, Roubo, p.points[Roubo]) + g_state.healCarry;
                        heal = static_cast<int32_t>(h);
                        g_state.healCarry = h - heal;
                        healed = objs[who];
                    }
                    else // atacante que não é o objeto de um jogador (projétil, armadilha): Força do jogador 1, média no co-op
                    {
                        const Player &p1 = g_state.players[0], &p2 = g_state.players[1];
                        const bool two = g_state.coop && objs[1];
                        const int forca = two ? (p1.points[Forca] + p2.points[Forca] + 1) / 2 : p1.points[Forca];
                        const int level = two ? (levelLocked(p1) + levelLocked(p2) + 1) / 2 : levelLocked(p1);
                        scaled = dealtDamage(c, damage, forca, level);
                    }
                }


                // Chefes jogáveis: os golpes deles foram feitos para bater no jogador (pouca vida), então na história
                // eles causam pouco: na história batem 1,5×. Vale com ou sem RPG, só para o lutador escolhido pelo jogador.
                // Muitos golpes acertam por outro objeto (projétil, onda, arma): o bônus vale para todo dano em quem
                // não é jogador, desde que um jogador seja chefe (no co-op, o maior bônus dos dois).
                struct BossTuning { int id; double dealt, taken; };
                static constexpr BossTuning kBossTuning[] = {
                    {64, 1.5, 1.0}, {47, 1.5, 1.0}, {57, 1.5, 1.0}, {81, 1.5, 1.0}, {58, 1.5, 1.0},
                }; // Shao Kahn, Goro (2) e Kintaro (2)
                if (normalHit)
                {
                    const BossTuning *t[2] = {nullptr, nullptr};
                    for (int j = 0; j < 2; ++j)
                        if (objs[j])
                            for (const BossTuning &bt : kBossTuning)
                                if (bt.id == fighterId(rdram, save, j))
                                    t[j] = &bt;
                    const bool targetIsPlayer = target == objs[0] || (objs[1] && target == objs[1]);
                    if (targetIsPlayer)
                    {
                        const BossTuning *tt = t[target == objs[0] ? 0 : 1];
                        if (tt)
                            scaled = std::max<int32_t>(1, static_cast<int32_t>(std::lround(scaled * tt->taken)));
                    }
                    else
                    {
                        double dealt = 1.0;
                        if (attacker == objs[0] || (objs[1] && attacker == objs[1]))
                            dealt = t[attacker == objs[0] ? 0 : 1] ? t[attacker == objs[0] ? 0 : 1]->dealt : 1.0;
                        else
                            for (const BossTuning *tt : t)
                                if (tt)
                                    dealt = std::max(dealt, tt->dealt);
                        if (dealt != 1.0)
                            scaled = static_cast<int32_t>(std::lround(scaled * dealt));
                    }
                }
                if (scaled != damage)
                    SET_GPR_S32(ctx, 5, scaled);
            }

            g_origDamage(rdram, ctx, runtime);
            if (heal > 0 && healed)
            {
                const int32_t hp = static_cast<int32_t>(rd32(rdram, healed + 0x398u)), max = static_cast<int32_t>(rd32(rdram, healed + 0x3A0u));
                if (hp > 0 && hp < max)
                    wr32(rdram, healed + 0x398u, static_cast<uint32_t>(std::min(max, hp + heal)));
            }
        }

        // Protótipo da função multalityLocked para ser chamada no hookFrame
        void multalityLocked(uint8_t *rdram);

        // A cada quadro de jogo: vida máxima com o atributo Vida e Regeneração. Sozinho, o jogador tem toda a
        // vida (save+0x90 × save+0x94); no co-op (save+0x88 = 1) a vida é um total dividido: cada um tem metade,
        // aqui com a Vida de cada um, e save+0x94 fica na média (o jogo soma as duas metades no save).
        // Botões só do jogador (func_16C968(objeto, nó) → 1 se o nó casa com o controle): os nós enxertados em
        // 0xB9000–0xBA000 (item, agarrão, R1+botão) não valem para inimigos, e o L1 original da Mileena/Jade não vale
        // para o jogador (L1 livre; o golpe foi copiado para R1+bola). A lista de golpes é uma só por bloco.
        PS2Runtime::RecompiledFunction g_origMatch;
        float rdf(uint8_t *rdram, uint32_t a) { const uint32_t u = rd32(rdram, a); float f; std::memcpy(&f, &u, 4); return f; }
        void wrf(uint8_t *rdram, uint32_t a, float f) { uint32_t u; std::memcpy(&u, &f, 4); wr32(rdram, a, u); }
        // Jogar o item enxertado: o Liu Kang vira sozinho para o inimigo; a Mileena & cia. jogavam para onde estavam virados.
        // Vira o jogador para o inimigo mais perto. Objetos de personagem: 0x5E3780 + i*0x33C0 (1 e 2 = jogadores);
        // +0x80/+0x88 = X/Z, +0x94 = direção (4096 por volta), frente = (-sen, -cos); matriz em +0x150/+0x158/+0x170/+0x178.
        struct Aim { float th; int ticks; } g_aim[2] = {};
        void setFacing(uint8_t *rdram, uint32_t player, float th)
        {
            wrf(rdram, player + 0x94u, th * 4096.0f / 6.2831853f);
            const float c = std::cos(th), sn = std::sin(th);
            wrf(rdram, player + 0x150u, c); wrf(rdram, player + 0x158u, -sn); wrf(rdram, player + 0x170u, sn); wrf(rdram, player + 0x178u, c);
        }
        void faceNearestEnemy(uint8_t *rdram, uint32_t player)
        {
            const float px = rdf(rdram, player + 0x80u), pz = rdf(rdram, player + 0x88u);
            float best = 4000.0f * 4000.0f, dx = 0, dz = 0;
            bool found = false;
            for (uint32_t i = 3; i < 16; ++i)
            {
                const uint32_t o = 0x5E3780u + i * 0x33C0u;
                if (rd32(rdram, o + 0x230u) != 2u) continue;
                const float ex = rdf(rdram, o + 0x80u), ez = rdf(rdram, o + 0x88u);
                if (ex == 0.0f && ez == 0.0f) continue;
                const float d2 = (ex - px) * (ex - px) + (ez - pz) * (ez - pz);
                if (d2 < best) { best = d2; dx = ex - px; dz = ez - pz; found = true; }
            }
            if (!found) return;
            const float th = std::atan2(-dx, -dz);
            setFacing(rdram, player, th);
            g_aim[player == 0x5E6B40u ? 0 : 1] = {th, 20}; // o jogo desfaz o giro nos quadros seguintes: reaplica até soltar
        }
        bool isPlayerObj(uint32_t o) { return o == 0x5E6B40u || o == 0x5E6B40u + 0x33C0u; }
        void hookMatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t obj = getRegU32(ctx, 4), node = getRegU32(ctx, 5);
            const bool player = isPlayerObj(obj);
            const bool copia = node >= 0xB9000u && node < 0xBD000u; // tudo que enxertamos (item, agarrão, R1+botão) é só do jogador
            const int32_t blk = player ? static_cast<int16_t>(rd32(rdram, obj + 0x1E0Cu) >> 16) : -1;
            const uint32_t bt = rd32(rdram, node + 0x14u);
            if (blk == 68 && bt == 0x80u && !(rd32(rdram, node + 0x8u) & 0x10000u))
            {
                const uint32_t head = 0xB9000u + (obj == 0x5E6B40u ? 0u : 0x800u), r1sq = head + 0x510u;
                if (node != r1sq && rd32(rdram, head + 0x24u) == r1sq && rd32(rdram, r1sq + 0x14u) == 0x80u)
                {
                    ctx->r[5] = _mm_set_epi64x(0, r1sq);
                    g_origMatch(rdram, ctx, runtime);
                    const bool r1 = getRegU32(ctx, 2) != 0u;
                    ctx->r[4] = _mm_set_epi64x(0, obj); ctx->r[5] = _mm_set_epi64x(0, node);
                    if (r1) { setReturnU32(ctx, 0u); return; }
                }
            }
            // Jogador: o L1 sem condição (+0x08 = 0) da Mileena/Jade/Ermac chefe sai (foi copiado para R1+bola); no Ermac
            // também o R1 sozinho (copiado para R1+triângulo) e o R2 dele (copiado para R1+R2; o R2 vira agarrão).
            if (player ? !copia && rd32(rdram, node + 0x8u) == 0u &&
                             ((bt == 0x4u && (blk == 66 || blk == 67 || blk == 68)) || (blk == 68 && (bt == 0x8u || bt == 0x2u)) || (blk == 81 && bt == 0x2u))
                       : copia)
            { setReturnU32(ctx, 0u); return; }
            g_origMatch(rdram, ctx, runtime);
            // só nos lutadores que ganharam o item (os do Versus já miram sozinhos); qualquer lista em pé, anim 0x9A = jogar
            if (player && (blk == 66 || blk == 67 || blk == 68 || blk == 47 || blk == 57 || blk == 58 || blk == 81 || blk == 64) && rd32(rdram, node) == 0x9Au && rd32(rdram, obj + 0x275Cu) != 0u && getRegU32(ctx, 2) != 0u)
                faceNearestEnemy(rdram, obj);
        }


        // Terceiro lutador na reserva: num co-op sem o Kung Lao, a Mileena/Jade/Ermac chefe não têm par de agarrão. Depois da
        // carga do jogador 2, o Kung Lao inteiro é carregado como "jogador 3" (func_1CE6E8(3, 1, …)) na área de memória 0x60
        // (livre), criada nos 32 MB extras (0x2000000–0x3000000). Ele não entra na fase: só as animações ficam registradas.
        // Base para um terceiro/quarto jogador no futuro (PLANO_PERSONAGENS.md).
        constexpr uint32_t kReserveArea = 0x60u, kReserveSlot = 0xCu;
        PS2Runtime::RecompiledFunction g_origLoadChar, g_origRequest, g_origRegAnims;
        void hookLoadChar(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static int32_t loaded[2] = {-1, -1};
            const int32_t player = static_cast<int32_t>(getRegU32(ctx, 4)), fighter = static_cast<int32_t>(getRegU32(ctx, 5));
            const R5900Context args = *ctx;
            g_origLoadChar(rdram, ctx, runtime);
            if (player == 1 || player == 2) loaded[player - 1] = fighter;
            const auto needs = [](int32_t f) { return f == 66 || f == 67 || f == 68 || f == 47 || f == 57 || f == 58 || f == 81 || f == 64; };
            if (player != 2 || loaded[0] == 1 || loaded[1] == 1 || !(needs(loaded[0]) || needs(loaded[1]))) return;
            const uint32_t e = 0x64E998u + kReserveArea * 0x68u, model = 0x64E998u + 0x21u * 0x68u;
            if (rd32(rdram, e) != 0u && rd32(rdram, e) != 0x2000000u) return; // a área não está livre: não arrisca
            std::memcpy(rdram + e, rdram + model, 0x68u);
            wr32(rdram, e + 0x0u, 0x2000000u); wr32(rdram, e + 0x4u, 0x1000000u);
            wr32(rdram, e + 0x8u, 0x2000000u); wr32(rdram, e + 0xCu, 0x3000000u); wr32(rdram, e + 0x44u, 0x1000000u);
            const R5900Context after = *ctx;
            *ctx = args;
            ctx->r[4] = _mm_set_epi64x(0, 3); ctx->r[5] = _mm_set_epi64x(0, 1); ctx->r[9] = _mm_set_epi64x(0, kReserveArea);
            g_origLoadChar(rdram, ctx, runtime);
            *ctx = after;
        }
        // O pedido do pacote de golpes do jogador 3 usaria a vaga 4 (a do jogador 2): usa uma vaga livre.
        void hookRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (getRegU32(ctx, 9) == kReserveArea) ctx->r[5] = _mm_set_epi64x(0, kReserveSlot);
            g_origRequest(rdram, ctx, runtime);
        }
        // func_3205E0(índice do jogador, …) registra os golpes do lutador lido no objeto do jogador (+0x1E0E). O jogador 3 não
        // tem objeto (0x5ED300 é de outro): durante a chamada põe o Kung Lao ali e depois devolve o objeto e a vaga 0x6FB158.
        void hookRegAnims(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (getRegU32(ctx, 4) != 2u) { g_origRegAnims(rdram, ctx, runtime); return; }
            static uint8_t obj[0x33C0];
            uint8_t slot[8];
            const uint32_t o = 0x5E6B40u + 2u * 0x33C0u;
            std::memcpy(obj, rdram + o, sizeof obj); std::memcpy(slot, rdram + 0x6FB158u, sizeof slot);
            wr32(rdram, o + 0x1E0Cu, (rd32(rdram, o + 0x1E0Cu) & 0xFFFFu) | (1u << 16));
            g_origRegAnims(rdram, ctx, runtime);
            std::memcpy(rdram + o, obj, sizeof obj); std::memcpy(rdram + 0x6FB158u, slot, sizeof slot);
        }

        void hookFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                const uint32_t save = saveBlock(rdram), mode = save ? rd32(rdram, save + 0x88u) : 9u;
                if (save && mode <= 1u)
                    multalityLocked(rdram);
                for (int k = 0; k < 2; ++k)
                    if (g_aim[k].ticks > 0)
                    {
                        const uint32_t o = 0x5E6B40u + k * 0x33C0u;
                        setFacing(rdram, o, g_aim[k].th);
                        --g_aim[k].ticks;
                    }
                // Bloom desligado por padrão (MKSM_BLOOM=1 ou o menu Gráfico liga): limiar 0xFF em 0x4F6E70+0x28 (MODDING.md). Com ele,
                // o renderizador novo copia a imagem a cada leitura do brilho; numa GTX 1050 a luta no telhado do
                // Evil Monastery caía de 60 para ~33 quadros/s.
                // O menu Gráfico liga e desliga a qualquer hora: guarda o limiar do jogo para devolver.
                static uint32_t bloomGame = 0xFFFFFFFFu;
                const uint32_t limiar = rd32(rdram, 0x4F6E98u);
                if (!mksmBloomEnabled())
                {
                    if (limiar != 0xFFu)
                        bloomGame = limiar, wr32(rdram, 0x4F6E98u, 0xFFu);
                }
                else if (limiar == 0xFFu && bloomGame != 0xFFFFFFFFu)
                    wr32(rdram, 0x4F6E98u, bloomGame);
                const uint32_t objs[2] = {rd32(rdram, 0x6826A8u), mode == 1u ? rd32(rdram, 0x6826ACu) : 0u};
                // Golpe correndo: a lista de golpes de cada estado fica em 0x5CCF88 + bloco*0x6C (func_16A750);
                // correndo, o jogo usa a do campo +0x38. Fora do Versus ela não existe (0) e, correndo, nenhum botão
                // fazia nada. Com a lista parada (+0x00) ali, sai o golpe normal do botão.
                // shortcut: um inimigo com o mesmo bloco de um jogador também ganha a lista; raro (Shao contra Shao).
                for (uint32_t j = 0; j < 2; ++j)
                {
                    const int32_t block = static_cast<int16_t>(rd32(rdram, 0x5E6B40u + j * 0x33C0u + 0x1E0Cu) >> 16);
                    const uint32_t entry = 0x5CCF88u + static_cast<uint32_t>(block) * 0x6Cu;
                    if (objs[j] && block >= 0 && block < 256 && rd32(rdram, entry + 0x38u) == 0u)
                        wr32(rdram, entry + 0x38u, rd32(rdram, entry));
                }
                // Pegar item do chão com R2 nos lutadores sem a lista "segurando item" (Mileena, Jade): nó do Liu Kang
                // (anim 0x98, flag 0x800000 = só em cima de item) depois da raiz da lista em pé (o objeto guarda a raiz),
                // e a lista 17 com o arremesso 0x9A. A tabela é por bloco: um inimigo do mesmo lutador também ganha.
                for (uint32_t j = 0; j < 2; ++j)
                {
                    const int32_t block = static_cast<int16_t>(rd32(rdram, 0x5E6B40u + j * 0x33C0u + 0x1E0Cu) >> 16);
                    if (!objs[j] || block < 0 || block >= 256) continue;
                    const uint32_t entry = 0x5CCF88u + static_cast<uint32_t>(block) * 0x6Cu, head = 0xB9000u + j * 0x800u;
                    const uint32_t root = rd32(rdram, entry);
                    // Defesa (L2) do Kintaro 81: na tabela de animações por lutador (0x62EEA8 + n*0x370), a vaga 45 (+0xB4) é a
                    // defesa; nos outros é 0x22 (a de jogador, que o jogo conta como defesa; o tutorial do L2 só passa com ela) e
                    // no 81 é a dele de inimigo (0x7C9). shortcut: um Kintaro 81 inimigo também passa a defender com a 0x22.
                    if (block == 81 && rd32(rdram, 0x62EEA8u + 81u * 0x370u + 0xB4u) == 0x7C9u)
                        wr32(rdram, 0x62EEA8u + 81u * 0x370u + 0xB4u, 0x22u);
                    // Agarrão (R2) da Mileena e da Jade, depois do nó de pegar item: um agarrão de outro lutador e o combo de
                    // 14 arremessos por tipo de inimigo (+0x2C), como o do Johnny. O último (chave 0x18AE, alvo agarrado) é o
                    // arremesso: func_16D710 só faz o arremesso em par se o registro da animação (+0x2A) aponta a do agarrado.
                    // Elas não têm par próprio: usa o primeiro par carregado (o outro monge, o parceiro do co-op...); sem
                    // nenhum, o agarrão sai da lista e o R2 só pega item.
                    // Ermac chefe: o R2 original vai para R1+quadrado (cópia logo depois do nó de pegar item; o hookMatch nega a raiz,
                    // o quadrado sozinho, quando a cópia casa, porque a raiz vem antes dela na lista).
                    // Fora do Versus: os outros três nós de pegar item do Versus (0x89, 0xA9, 0x9E: espada e outros; flags 0x100000,
                    // 0x200000, 0x1000000), logo depois do da cabeça. O Kintaro 81 já tem as listas "segurando" de cada tipo; nos
                    // outros, as vagas vazias recebem a cópia (mksm_itens.inc, tirada do 81) em 0xC0000 + j*0x8000.
                    uint32_t tail = head;
                    const bool foraVersus = block == 66 || block == 67 || block == 68 || block == 47 || block == 57 || block == 58 || block == 81 || block == 64;
                    if (foraVersus && root && rd32(rdram, root + 0x24u) == head)
                    {
                        const uint32_t area = 0xC0000u + j * 0x8000u;
                        if (rd32(rdram, entry + kItemSlots[0] * 4u) == 0u)
                        {
                            for (uint32_t k = 0; k < std::size(kItemNodes); ++k)
                                for (uint32_t q = 0; q < 14; ++q)
                                {
                                    uint32_t v = kItemNodes[k][q];
                                    if ((q == 8 || q == 9) && v) v = area + (v - 1u) * 0x38u;
                                    wr32(rdram, area + k * 0x38u + q * 4u, v);
                                }
                            for (uint32_t k = 0; k < std::size(kItemSlots); ++k)
                                if (rd32(rdram, entry + kItemSlots[k] * 4u) == 0u)
                                    wr32(rdram, entry + kItemSlots[k] * 4u, area + kItemHeads[k] * 0x38u);
                        }
                        static const uint32_t extra[3][2] = {{0x89u, 0x100000u}, {0xA9u, 0x200000u}, {0x9Eu, 0x1000000u}};
                        const uint32_t e0 = 0xBC000u + j * 0x800u + 0x578u;
                        if (rd32(rdram, head + 0x24u) != e0)
                        {
                            uint32_t nx = rd32(rdram, head + 0x24u);
                            for (int k = 2; k >= 0; --k)
                            {
                                const uint32_t at = e0 + 0x38u * k;
                                std::memset(rdram + at, 0, 0x38);
                                wr32(rdram, at, extra[k][0]); wr32(rdram, at + 0x8u, extra[k][1]); wr32(rdram, at + 0x10u, 0x141388u);
                                wr32(rdram, at + 0x14u, 0x2u); wr32(rdram, at + 0x1Cu, 0x40u); wr32(rdram, at + 0x24u, nx);
                                nx = at;
                            }
                            wr32(rdram, head + 0x24u, e0);
                        }
                        tail = e0 + 0x38u * 2u;
                    }
                    uint32_t pre = tail;
                    if ((block == 68 || block == 81) && root && rd32(rdram, root + 0x24u) == head)
                    {
                        const uint32_t r2c = head + 0x510u;
                        if (rd32(rdram, tail + 0x24u) != r2c)
                        {
                            uint32_t r2 = 0;
                            for (uint32_t n = root, k = 0; n && k < 200; n = rd32(rdram, n + 0x24u), ++k)
                                if (rd32(rdram, n + 0x14u) == 0x2u && rd32(rdram, n + 0x8u) == 0u && (n < 0xB9000u || n >= 0xBA000u)) { r2 = n; break; }
                            if (r2)
                            {
                                std::memcpy(rdram + r2c, rdram + r2, 0x38);
                                wr32(rdram, r2c + 0x8u, 0x10000u); wr32(rdram, r2c + 0x14u, block == 68 ? 0x80u : 0x2u); // Ermac: R1+quadrado; Kintaro 81: R1+R2 (já tem os R1+botão)
                                wr32(rdram, r2c + 0x24u, rd32(rdram, tail + 0x24u)); wr32(rdram, tail + 0x24u, r2c);
                            }
                        }
                        if (rd32(rdram, tail + 0x24u) == r2c) pre = r2c;
                    }
                    // Goro (47, 57), Kintaro (58, 81) e Shao Kahn (64) também: o R2 deles só pega item (o do 81 foi para R1+R2, como o do Ermac).
                    if ((block == 66 || block == 67 || block == 68 || block == 47 || block == 57 || block == 58 || block == 81 || block == 64) && ((block != 68 && block != 81) || pre != tail))
                    {
                        static const uint32_t pares[][2] = {{0x1C2u, 0x1C6u}, {0x10Au, 0x10Eu}, {0xADAu, 0xADEu}, {0xB58u, 0xB5Du}};
                        const uint32_t cnt = rd32(rdram, 0x6B3000u), tab = rd32(rdram, 0x6B3008u);
                        uint32_t agarra = 0, joga = 0;
                        for (const auto &pr : pares)
                        {
                            const uint32_t r = pr[1] < cnt && tab ? rd32(rdram, tab + pr[1] * 4u) : 0u;
                            if (r && r < PS2_RAM_SIZE && static_cast<int16_t>(rd32(rdram, r + 0x28u) >> 16) > 0) { agarra = pr[0]; joga = pr[1]; break; }
                        }
                        const uint32_t grab = head + 0x38u * 5u, throws = grab + 0x38u;
                        const bool ligado = root && rd32(rdram, root + 0x24u) == head && rd32(rdram, pre + 0x24u) == grab;
                        if (ligado && (!agarra || rd32(rdram, grab) != agarra))
                            wr32(rdram, pre + 0x24u, rd32(rdram, grab + 0x24u)); // tira (par sumiu ou mudou)
                        else if (agarra && root && rd32(rdram, root + 0x24u) == head && rd32(rdram, pre + 0x24u) != grab)
                        {
                            static const uint32_t keys[14] = {0x179a,0x17b3,0x17b6,0x17b7,0x17bc,0x17c5,0x17c6,0x17c7,0x17ca,0x17cb,0x17d1,0x17d2,0x17d8,0x18ae};
                            std::memset(rdram + grab, 0, 0x38 * 15);
                            wr32(rdram, grab, agarra); wr32(rdram, grab + 0x10u, 0x141388u); wr32(rdram, grab + 0x14u, 2u);
                            wr32(rdram, grab + 0x1Cu, 0x40u); wr32(rdram, grab + 0x20u, throws); wr32(rdram, grab + 0x24u, rd32(rdram, pre + 0x24u));
                            for (uint32_t i = 0; i < 14; ++i)
                            {
                                const uint32_t at = throws + 0x38u * i;
                                wr32(rdram, at, 0x01000000u | (i == 13 ? joga : 0x23u));
                                wr32(rdram, at + 0x4u, 0x1101u); wr32(rdram, at + 0x10u, 0x640012u); wr32(rdram, at + 0x1Cu, 0x860040u);
                                wr32(rdram, at + 0x24u, i == 13 ? 0u : at + 0x38u); wr32(rdram, at + 0x28u, 1u); wr32(rdram, at + 0x2Cu, keys[i]);
                            }
                            wr32(rdram, pre + 0x24u, grab);
                        }
                    }
                    // Finalizar o chefe tonto com L1, como no Versus: nó L1 (anim 0x29) com a sublista do Kung Lao (lido de um save:
                    // 23 entradas por tipo de inimigo e a última, chave 0x18AC = alvo pronto para morrer, com a finalização 0x1EF dele).
                    // A animação vem do Kung Lao, carregado na reserva quando não está na partida. Entra antes do primeiro L1 da lista.
                    if (block == 66 || block == 67 || block == 68 || block == 47 || block == 57 || block == 58 || block == 81 || block == 64)
                    {
                        const uint32_t fin = 0xBC000u + j * 0x800u, cntA = rd32(rdram, 0x6B3000u), tabA = rd32(rdram, 0x6B3008u);
                        const uint32_t rec = 0x1EFu < cntA && tabA ? rd32(rdram, tabA + 0x1EFu * 4u) : 0u;
                        const bool temAnim = rec && rec < PS2_RAM_SIZE && static_cast<int16_t>(rd32(rdram, rec + 0x28u) >> 16) > 0;
                        uint32_t prevFin = 0, prevL1 = 0, last = 0;
                        for (uint32_t pv = 0, n = root, k = 0; n && k < 300; pv = n, n = rd32(rdram, n + 0x24u), ++k)
                        {
                            if (n == fin) { prevFin = pv ? pv : 0xFFFFFFFFu; break; }
                            if (!prevL1 && pv && rd32(rdram, n + 0x14u) == 0x4u) prevL1 = pv;
                            last = n;
                        }
                        if (prevFin && !temAnim && prevFin != 0xFFFFFFFFu)
                            wr32(rdram, prevFin + 0x24u, rd32(rdram, fin + 0x24u)); // tira (Kung Lao saiu da memória)
                        else if (!prevFin && temAnim && root && (prevL1 || last))
                        {
                            static const uint16_t keys[24] = {0x17a0,0x17ab,0x17ae,0x17b3,0x17b6,0x17b7,0x17bc,0x17c2,0x17c3,0x17c5,0x17c6,0x17ca,
                                                              0x17d9,0x17dd,0x17de,0x17df,0x17e0,0x17e1,0x17e2,0x17e3,0x17e4,0x17e9,0x17ee,0x18ac};
                            const uint32_t at0 = prevL1 ? prevL1 : last;
                            std::memset(rdram + fin, 0, 0x38u * 25u);
                            wr32(rdram, fin, 0x29u); wr32(rdram, fin + 0xCu, 0x2000u); wr32(rdram, fin + 0x10u, 0x141388u);
                            wr32(rdram, fin + 0x14u, 0x4u); wr32(rdram, fin + 0x1Cu, 0x40u); wr32(rdram, fin + 0x20u, fin + 0x38u);
                            for (uint32_t i = 0; i < 24; ++i)
                            {
                                const uint32_t at = fin + 0x38u * (i + 1);
                                wr32(rdram, at, i == 23 ? 0x0A0501EFu : 0x03000023u); wr32(rdram, at + 0x4u, 0x1F01u);
                                wr32(rdram, at + 0x10u, 0x640020u); wr32(rdram, at + 0x1Cu, 0xE50040u);
                                wr32(rdram, at + 0x24u, i == 23 ? 0u : at + 0x38u); wr32(rdram, at + 0x28u, 1u); wr32(rdram, at + 0x2Cu, keys[i]);
                            }
                            wr32(rdram, fin + 0x24u, rd32(rdram, at0 + 0x24u)); wr32(rdram, at0 + 0x24u, fin);
                        }
                    }
                    // Mileena e Jade: R1 (+0x08 bit 0x10000, como os R1+botão do Versus) + triângulo = cópia do rolamento
                    // (triângulo+bola, que fica) e R1+bola = cópia do golpe do L1. A lista é a mesma das inimigas: o hookMatch
                    // nega as cópias a quem não é jogador e o L1 original ao jogador (L1 livre).
                    // Ermac chefe: R1+triângulo = cópia do R1 sozinho (proibido), R1+bola = cópia do L1. Jade: R1+triângulo = 0x70A
                    // (golpe dela que acerta e não tinha botão).
                    if ((block == 66 || block == 67 || block == 68) && root && rd32(rdram, head + 0x4A0u + 0x14u) != 0x10u)
                    {
                        const uint32_t roll = head + 0x4A0u;
                        uint32_t src = 0, alt = 0, r1 = 0, prevTri = 0, l1 = 0, prevCirc = 0;
                        for (uint32_t p = 0, n = root, k = 0; n && k < 200; p = n, n = rd32(rdram, n + 0x24u), ++k)
                        {
                            const uint32_t bt = rd32(rdram, n + 0x14u);
                            if (bt == 0x4u && !l1) l1 = n;
                            else if (bt == 0x20u && !prevCirc) prevCirc = p;
                            else if (bt == 0x30u && !src) src = n;
                            else if (bt == 0x90u && !alt) alt = n; // a Jade não tem triângulo+bola: usa o nó do quadrado+triângulo com outra animação
                            else if (bt == 0x8u && !r1) r1 = n;
                            else if (bt == 0x10u && !prevTri) prevTri = p; // os R1+botão vêm antes do botão sozinho, como no Versus
                        }
                        if (l1 && prevCirc && rd32(rdram, prevCirc + 0x24u) != head + 0x4D8u) // cópia do golpe do L1 em R1+bola, antes da bola sozinha (o original fica; hookMatch o nega ao jogador)
                        {
                            const uint32_t c = head + 0x4D8u;
                            std::memcpy(rdram + c, rdram + l1, 0x38);
                            wr32(rdram, c + 0x14u, 0x20u); wr32(rdram, c + 0x8u, rd32(rdram, l1 + 0x8u) | 0x10000u);
                            wr32(rdram, c + 0x24u, rd32(rdram, prevCirc + 0x24u)); wr32(rdram, prevCirc + 0x24u, c);
                        }
                        if (block == 68) src = r1;
                        else if (!src) src = alt;
                        if (src && prevTri && rd32(rdram, prevTri + 0x24u) != roll) // a Jade não tem triângulo+bola: sem esta checagem, a cópia do L1 era religada a cada quadro e apontava para si mesma (laço infinito na lista)
                        {
                            std::memcpy(rdram + roll, rdram + src, 0x38);
                            wr32(rdram, roll + 0x14u, 0x10u); wr32(rdram, roll + 0x8u, rd32(rdram, src + 0x8u) | 0x10000u);
                            if (block == 67) wr32(rdram, roll, 0x70Au);
                            wr32(rdram, roll + 0x24u, rd32(rdram, prevTri + 0x24u)); wr32(rdram, prevTri + 0x24u, roll);
                        }
                    }
                    const uint32_t held = 0xBA000u + j * 0x1000u;
                    const auto node = [&](uint32_t at, uint32_t anim, uint32_t flags, uint32_t button, uint32_t next) {
                        std::memset(rdram + at, 0, 0x38);
                        wr32(rdram, at, anim); wr32(rdram, at + 0x8u, flags); wr32(rdram, at + 0x10u, 0x141388u);
                        wr32(rdram, at + 0x14u, button); wr32(rdram, at + 0x1Cu, 0x40u); wr32(rdram, at + 0x24u, next); };
                    // Nó de pegar item depois da raiz. Também em quem já tem a lista "segurando item" própria (Kintaro 81),
                    // mas não tem o nó: sem ele o R2 não pegava nada.
                    if (root && rd32(rdram, root + 0x24u) != head && (rd32(rdram, entry + 0x44u) == 0u || block == 81))
                    {
                        node(head, 0x98u, 0x800000u, 0x2u, rd32(rdram, root + 0x24u));
                        wr32(rdram, root + 0x24u, head);
                    }
                    if (!root || (rd32(rdram, entry + 0x44u) != 0u && rd32(rdram, entry + 0x44u) != head + 0x38u)) continue; // já tem (ou já enxertado; a lista curta antiga é trocada)
                    // Lista "segurando item", cópia da do Liu Kang (57 nós): os 4 botões, e por direção do direcional (0x2000,
                    // 0x4000, 0x8000, 0x1000) os botões com e sem o inimigo naquela direção (bit alto, +0x08) e um nó 0xFFFF com
                    // a sublista que mira (+0x10 = 0x140010, +0x1C = alvo). Com só os 4 botões, o item ia reto para a frente.
                    static const uint32_t buttons[4] = {0x80u, 0x10u, 0x20u, 0x2u};
                    static const uint32_t dirs[4][2] = {{0x2000u, 0x0B30u}, {0x4000u, 0x0B3Du}, {0x8000u, 0x0B4Au}, {0x1000u, 0x0B57u}};
                    uint32_t at = held;
                    const auto add = [&](uint32_t anim, uint32_t f8, uint32_t f10, uint32_t button, uint32_t f1c, bool last) {
                        std::memset(rdram + at, 0, 0x38);
                        wr32(rdram, at, anim); wr32(rdram, at + 0x8u, f8); wr32(rdram, at + 0x10u, f10);
                        wr32(rdram, at + 0x14u, button); wr32(rdram, at + 0x1Cu, f1c); wr32(rdram, at + 0x24u, last ? 0u : at + 0x38u);
                        at += 0x38u; return at - 0x38u; };
                    for (uint32_t b : buttons) add(0x9Au, 0u, 0x141388u, b, 0x40u, false);
                    for (const auto &d : dirs)
                    {
                        const uint32_t hi = d[0] << 16;
                        for (uint32_t b : buttons) add(0x9Au, 0u, 0x141388u, d[0] | b, 0x40u, false);
                        for (uint32_t b : buttons) add(0x9Au, hi, 0x141388u, hi | b, 0x40u, false);
                        const uint32_t ff = add(0xFFFFu, 0u, 0x141388u, d[0], 0x40u, false);
                        const uint32_t next = at; // o próximo da lista principal vem depois da sublista
                        at += 0x38u * 4u;
                        uint32_t sub = ff + 0x38u;
                        for (uint32_t i = 0; i < 4; ++i)
                        {
                            const uint32_t sv = at; at = sub;
                            add(0x9Au, 0u, 0x140010u, hi | buttons[i], (d[1] << 16) | 0x40u, i == 3);
                            sub = at; at = sv;
                        }
                        wr32(rdram, ff + 0x20u, ff + 0x38u); wr32(rdram, ff + 0x24u, at);
                        (void)next;
                    }
                    add(0xFFFFu, 0x4u, 0x141388u, 0x100u, 0x40u, true);
                    wr32(rdram, entry + 0x44u, held);
                }
                g_state.coop = mode == 1u;
                const int32_t share = mode == 1u ? 2 : 1;
                if (g_state.active && save && mode <= 1u)
                    creditXpLocked(rdram, save, mode == 1u ? 2 : 1);
                if (g_state.active && save && objs[0] && mode <= 1u)
                {
                    const Config &c = cfg();
                    const uint32_t upgrade = std::min(rd32(rdram, save + 0xE0u), 4u);
                    const int32_t base = static_cast<int32_t>(rd32(rdram, 0x4EBC58u + 0x14u * upgrade + 4u * (mode + 1u)));
                    const int32_t units = static_cast<int32_t>(rd32(rdram, save + 0x90u));
                    int32_t want[2];
                    for (int j = 0; j < 2; ++j)
                        want[j] = static_cast<int32_t>(std::lround(base * (1.0 + effect(c, Vida, g_state.players[j].points[Vida]))));
                    const int32_t saveWant = mode == 1u ? (want[0] + want[1]) / 2 : want[0];
                    if (base > 0 && units > 0 && saveWant != static_cast<int32_t>(rd32(rdram, save + 0x94u)))
                        wr32(rdram, save + 0x94u, static_cast<uint32_t>(saveWant));
                    const bool regen = inGameplay(rdram);
                    for (int j = 0; j < 2; ++j)
                    {
                        const uint32_t obj = objs[j];
                        if (!obj || base <= 0 || units <= 0)
                            continue;
                        Player &p = g_state.players[j];
                        int32_t hp = static_cast<int32_t>(rd32(rdram, obj + 0x398u));
                        const int32_t oldMax = static_cast<int32_t>(rd32(rdram, obj + 0x3A0u)), max = units * want[j] / share;
                        if (max != oldMax) // vida a mais entra na vida atual; a menos (redistribuir) só limita
                        {
                            wr32(rdram, obj + 0x3A0u, static_cast<uint32_t>(max));
                            if (hp > 0)
                                hp = std::min(max, hp + std::max(0, max - oldMax));
                        }
                        if (hp > 0 && hp < max && regen)
                        {
                            const double r = max * effect(c, Regen, p.points[Regen]) / 60.0 + p.regenCarry; // 60 quadros/s
                            const int32_t add = static_cast<int32_t>(r);
                            p.regenCarry = r - add;
                            hp = std::min(max, hp + add);
                        }
                        if (hp > 0)
                            wr32(rdram, obj + 0x398u, static_cast<uint32_t>(hp));
                    }
                }
            }
            g_origFrame(rdram, ctx, runtime);
        }

        // Dificuldade: um jogo novo vira RPG se armado e no Hard; carregar um save lê o perfil do cartão.
        void hookDifficulty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t difficulty = getRegU32(ctx, 4), ra = getRegU32(ctx, 31);
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (ra == 0x130910u) // jogo novo (Single Player ou Ko-op -> personagem -> dificuldade)
                {
                    // Lutador escolhido com L1/R1 na tela de dificuldade: gravado no save quando a primeira sala é
                    // montada (antes disso o jogo ainda reescreve save+0xCC/0xD0 com a seleção).
                    g_fighterPending = g_fighter[0] >= 0 || g_fighter[1] >= 0;
                    const bool rpg = g_state.armed && difficulty == 2u;
                    g_state = State{};
                    g_state.active = rpg;
                    if (const char *xp = std::getenv("MKSM_RPG_TEST_XP"); rpg && xp) // roteiro de teste: começa com XP
                        for (Player &p : g_state.players)
                            p.xpGained = std::max(0LL, std::atoll(xp)), p.shownLevel = levelLocked(p);
                    if (rpg)
                        std::fprintf(stderr, "[rpg] jogo novo no modo RPG\n");
                }
                else if (ra == 0x1304F0u || ra == 0x141A08u) // carregar o save do cartão
                {
                    // Qual slot foi carregado: o que mais se parece com o bloco do save na RAM.
                    // Conta só onde algum dos dois tem dado: slots vazios (zeros) não ganham.
                    int slot = -1;
                    size_t best = 0;
                    const uint32_t save = saveBlock(rdram);
                    for (int k = 0; save && k < kSlots && kHeader + k * kSlotStride + kSlotSize <= g_card[0].size(); ++k)
                    {
                        const uint8_t *a = rdram + (save & 0x1FFFFFFu), *b = g_card[0].data() + kHeader + k * kSlotStride;
                        size_t same = 0, used = 0;
                        for (size_t i = 0; i < kSlotSize; ++i)
                            if (a[i] | b[i])
                                ++used, same += a[i] == b[i];
                        if (used > 0 && same * 2 > used && same > best)
                            best = same, slot = k;
                    }
                    State loaded;
                    const bool rpg = difficulty == 2u && slot >= 0 && readCard(0, slot, loaded);
                    g_state = rpg ? loaded : State{};
                    if (rpg)
                        std::fprintf(stderr, "[rpg] save RPG carregado (slot %d): nível %d\n", slot + 1, levelLocked(g_state.players[0]));
                }
            }
            g_origDifficulty(rdram, ctx, runtime);
        }

        // Portão da fatality na história: os comandos de roteiro isfatalityfull (0x128678), fatality (0x128630) e
        // fatalityover (0x1285C8) esperam a barra cheia e uma fatality que quem não é monge nem do Versus não tem.
        // Para eles, respondem "sim" sem mexer na barra (encher a barra trava a sala: PLANO_PERSONAGENS.md).
        // Só quando nenhum jogador ativo tem fatality: com um monge ou lutador do Versus no co-op, ele a faz.
        bool semFatality(const uint8_t *rdram)
        {
            const uint32_t save = saveBlock(rdram);
            if (!save || rd32(rdram, save + 0x88u) > 1u)
                return false;
            for (int j = 0; j < 2; ++j)
            {
                const int f = fighterId(rdram, save, j);
                if (f < 0)
                    continue;
                if (f == 0 || f == 1 || f == 82 || f == 83 || f == 84 || f == 88 || f == 89 || f == 90)
                    return false;
            }
            return true;
        }
        void hookFatFull(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            g_origFatFull(rdram, ctx, runtime);
            if (semFatality(rdram)) SET_GPR_U32(ctx, 2, 1u);
        }
        void hookFat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            g_origFat(rdram, ctx, runtime);
            if (semFatality(rdram)) SET_GPR_U32(ctx, 2, 1u);
        }
        // Multality e Brutality de quem não tem (Johnny Cage e outros do Versus): a tabela de golpes finais da fase (entradas em
        // 0x5BDD58 + i*0x3C, quantidade em 0x5BE334, categoria em +0x14) não tem entrada de categoria 2 (Multality)
        // nem 3 (Brutality). Na sala que pede uma delas (0x4AD58C), a primeira fatality (categoria 4) vira essa: aparecem as setas dela. O jogo
        // cancela a execução, mas, acertadas as 4 setas (contador 0x5BDD50), fatalityover(categoria) responde sim.
        // shortcut: o inimigo não morre; é só o portão.
        // Um gerente de golpes finais por personagem (8, em 0x5BB2B8 + k*0x610): entradas em gerente+0x30 + i*0x3C,
        // quantidade em gerente+0x30+0x5DC, contador de setas em gerente+0x28. No co-op, cada jogador usa o seu.
        constexpr uint32_t kFinisherBase = 0x5BB2B8u, kFinisherStride = 0x610u, kFinishers = 8u;
        int32_t g_multEntry[kFinishers] = {-1, -1, -1, -1, -1, -1, -1, -1};
        uint32_t g_multCat = 0;
        bool g_multDone = false;
        void multalityLocked(uint8_t *rdram)
        {
            const uint32_t cat = rd32(rdram, 0x4AD58Cu);
            const bool want = cat == 2u || cat == 3u;
            for (uint32_t k = 0; k < kFinishers; ++k)
            {
                const uint32_t m = kFinisherBase + k * kFinisherStride, table = m + 0x30u;
                const uint32_t n = std::min<uint32_t>(rd32(rdram, table + 0x5DCu), 32u);
                int32_t &entry = g_multEntry[k];
                if (!want || (entry >= 0 && cat != g_multCat))
                {
                    if (entry >= 0 && static_cast<uint32_t>(entry) < n && rd32(rdram, table + entry * 0x3Cu + 0x14u) == g_multCat)
                        wr32(rdram, table + entry * 0x3Cu + 0x14u, 4u);
                    entry = -1;
                    continue;
                }
                if (entry < 0)
                {
                    int32_t first = -1;
                    bool converted = false, real = false;
                    for (uint32_t i = 0; i < n && !real; ++i)
                    {
                        const uint32_t e = table + i * 0x3Cu, c = rd32(rdram, e + 0x14u);
                        // As fatalities têm +0xC = 0x29A (func_126900): uma entrada da categoria pedida com essa marca é
                        // uma fatality já convertida (por exemplo, antes de um save state), não uma Multality de verdade.
                        if (c == cat && rd32(rdram, e + 0xCu) != 0x29Au)
                            real = true;
                        else if (c == cat && first < 0)
                            first = static_cast<int32_t>(i), converted = true;
                        else if (c == 4u && first < 0)
                            first = static_cast<int32_t>(i);
                    }
                    if (real || first < 0)
                        continue;
                    entry = first;
                    if (!converted)
                        wr32(rdram, table + first * 0x3Cu + 0x14u, cat);
                }
                if (rd32(rdram, m + 0x28u) >= 4u) // as 4 setas; o botão final não passa por esse contador
                    g_multDone = true;
            }
            if (!want)
                g_multDone = false;
            else
                g_multCat = cat;
        }

        void hookFatOver(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t arg = getRegU32(ctx, 4);
            g_origFatOver(rdram, ctx, runtime);
            if (semFatality(rdram))
                SET_GPR_U32(ctx, 2, 1u);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_multDone && arg && rd32(rdram, arg) == g_multCat)
                SET_GPR_U32(ctx, 2, 1u);


            //iniciio codigo gerado

            //fim codigo gerado



        }

        // Chefe jogável: antes de abrir a fatality, func_127C08 varre os personagens da sala e, se o bloco de golpes
        // de algum for de chefe (func_127E30), a fatality fica travada para todos. O jogador escolhido como Shao
        // Kahn, Kintaro, Goro... caía nessa varredura. Nessa chamada, o bloco de um jogador não conta como chefe.
        // shortcut: um chefe inimigo com o mesmo bloco de um jogador também deixa de travar; raro (Shao contra Shao).
        void hookBossBlock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const int32_t block = static_cast<int16_t>(getRegU32(ctx, 5));
            g_origBossBlock(rdram, ctx, runtime);
            if (getRegU32(ctx, 31) != 0x127C8Cu || getRegU32(ctx, 2) == 0u)
                return;
            for (uint32_t j = 0; j < 2; ++j)
            {
                const uint32_t obj = 0x5E6B40u + j * 0x33C0u; // objeto de luta do jogador j (func_3025D0)
                if (rd32(rdram, 0x6826A8u + j * 4u) && static_cast<int16_t>(rd32(rdram, obj + 0x1E0Cu) >> 16) == block)
                    SET_GPR_U32(ctx, 2, 0u);
            }
        }

        void replace(PS2Runtime &runtime, uint32_t address, PS2Runtime::RecompiledFunction hook, PS2Runtime::RecompiledFunction &original)
        {
            original = runtime.lookupFunction(address);
            if (original)
                runtime.replaceFunction(address, hook);
            else
                std::fprintf(stderr, "[rpg] função 0x%X não encontrada: modo RPG incompleto\n", address);
        }
    }

    void install(PS2Runtime &runtime)
    {
        if (!selfTest())
            std::fprintf(stderr, "[rpg] autoteste da curva falhou (chaves RPG_* no mksm.ini?)\n");
        replace(runtime, 0x127E30u, hookBossBlock, g_origBossBlock);
        replace(runtime, 0x128678u, hookFatFull, g_origFatFull);
        replace(runtime, 0x128630u, hookFat, g_origFat);
        replace(runtime, 0x1285C8u, hookFatOver, g_origFatOver);
        replace(runtime, 0x284FE0u, hookDamage, g_origDamage);
        replace(runtime, 0x183978u, hookFrame, g_origFrame);
        replace(runtime, 0x16C968u, hookMatch, g_origMatch);
        replace(runtime, 0x1CE6E8u, hookLoadChar, g_origLoadChar);
        replace(runtime, 0x3195F8u, hookRequest, g_origRequest);
        replace(runtime, 0x3205E0u, hookRegAnims, g_origRegAnims);
        replace(runtime, 0x1DAF90u, hookDifficulty, g_origDifficulty);
        replace(runtime, 0x1CEE00u, hookRoom, g_origRoom);
    }

    Snapshot snapshot(int player)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Snapshot s;
        const Player &p = g_state.players[std::clamp(player, 0, 1)];
        s.active = g_state.active && g_inStory.load(std::memory_order_relaxed);
        s.coop = g_state.coop;
        s.maxLevel = cfg().maxLevel;
        s.cap = cfg().cap;
        for (int a = 0; a < AttrCount; ++a)
            s.caps[a] = capFor(cfg(), a);
        s.pointsPerLevel = cfg().pointsPerLevel;
        s.xpGained = p.xpGained;
        s.level = levelFor(cfg(), p.xpGained, &s.xpInLevel, &s.xpStep);
        s.freePoints = freeLocked(p);
        std::copy(std::begin(p.points), std::end(p.points), s.points);
        return s;
    }

    bool addPoint(int player, int attr)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Player *p = playerLocked(player);
        if (!g_state.active || !p || attr < 0 || attr >= AttrCount || freeLocked(*p) <= 0 || (capFor(cfg(), attr) && p->points[attr] >= capFor(cfg(), attr)))
            return false;
        ++p->points[attr];
        return true;
    }

    void resetPoints(int player)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (Player *p = playerLocked(player))
            std::fill(std::begin(p->points), std::end(p->points), 0);
    }

    const char *attrName(int attr)
    {
        static const char *names[AttrCount] = {"Força", "Defesa", "Vida", "Roubo de vida", "Regeneração", "Sabedoria"};
        return attr >= 0 && attr < AttrCount ? names[attr] : "?";
    }

    std::string attrEffect(int attr, int points)
    {
        const double pct = effect(cfg(), attr, points) * 100.0;
        char text[64];
        switch (attr)
        {
            case Forca: std::snprintf(text, sizeof(text), "+%.0f%% de dano causado", pct); break;
            case Defesa: std::snprintf(text, sizeof(text), "-%.0f%% de dano recebido", pct); break;
            case Vida: std::snprintf(text, sizeof(text), "+%.0f%% de vida máxima", pct); break;
            case Roubo: std::snprintf(text, sizeof(text), "cura %.0f%% do dano causado", pct); break;
            case Regen: std::snprintf(text, sizeof(text), "%.1f%% da vida por segundo", pct); break;
            case Sabedoria: std::snprintf(text, sizeof(text), "+%.0f%% de XP para o nível", pct); break;
            default: text[0] = '\0';
        }
        return text;
    }

    int takeLevelUps(int player, int &newLevel)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Player *p = playerLocked(player);
        if (!p)
            return 0;
        const int n = p->levelUps;
        p->levelUps = 0;
        newLevel = p->shownLevel;
        return n;
    }

    // Diálogo "Select Difficulty" aberto (RAM do menu, A7) e o cursor: 0x6FA678 é 0 no Hard, 1 no Normal.
    bool difficultyScreen(const uint8_t *rdram, bool &cursorHard)
    {
        // Diálogo "Sim/Não" do menu (0x5CC634 = 1, 0x5CC608 = 2) fora de uma partida (0x50BC14 = 0) e fora do Load Game
        // (0x4AFC1C = 0). 0x5BE708, usado antes, pisca e fazia o L3 falhar.
        const bool open = rd32(rdram, 0x5CC634u) == 1u && rd32(rdram, 0x5CC608u) == 2u && rd32(rdram, 0x4F4B68u) == 0u &&
        rd32(rdram, 0x50BC14u) == 0u && rd32(rdram, 0x4AFC1Cu) == 0u;
        cursorHard = open && rd32(rdram, 0x6FA678u) == 0u;
        return open;
    }

    void setInStory(bool on) { g_inStory.store(on, std::memory_order_relaxed); }
    bool inStory() { return g_inStory.load(std::memory_order_relaxed); }

    int fighterPick(int player)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return player >= 0 && player < 2 ? g_fighter[player] : -1;
    }

    void setFighterPick(int player, int id)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (player >= 0 && player < 2)
            g_fighter[player] = id;
    }

    bool armed()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_state.armed;
    }

    void setArmed(bool on)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.armed = on;
    }

    bool slotIsRpg(int port, int slot, int &level)
    {
        // A tela de load pergunta a cada quadro: os arquivos são relidos no máximo uma vez por segundo.
        static std::mutex cacheMutex;
        static double readAt[2] = {-10.0, -10.0};
        static int cached[2][kSlots] = {};
        if (port < 0 || port > 1 || slot < 0 || slot >= kSlots)
            return false;
        std::lock_guard<std::mutex> lock(cacheMutex);
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - readAt[port] >= 1.0)
        {
            for (int k = 0; k < kSlots; ++k)
            {
                State s;
                cached[port][k] = readCard(port, k, s) ? levelFor(cfg(), s.players[0].xpGained) : 0;
            }
            readAt[port] = now;
        }
        level = cached[port][slot];
        return level > 0;
    }

    // A lista da tela "Load Game" tem 9 itens; 0x5B9990 e 0x5D5448 valem 9 só nela (A6, comparando a RAM).
    bool loadScreen(const uint8_t *rdram)
    {
        const bool dialog = rd32(rdram, 0x5CC634u) == 1u && rd32(rdram, 0x5CC608u) == 2u; // "No/Yes" por cima da lista
        // A seleção do Versus também é uma lista de 9: 0x4AFC1C e 0x64D8B0 valem 1 só no Load Game.
        return rd32(rdram, 0x5B9990u) == 9u && rd32(rdram, 0x5D5448u) == 9u && rd32(rdram, 0x4AFC1Cu) == 1u &&
        rd32(rdram, 0x64D8B0u) == 1u && rd32(rdram, 0x4F4B68u) == 0u && !dialog;
    }

    void noteCardFile(int port, const std::string &path)
    {
        if (port < 0 || port > 1)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cardPath[port] = path;
        g_card[port] = readFile(path);
    }

    // O jogo gravou (um pedaço de) o save: os slots que mudaram recebem o perfil, ou perdem o RPG num jogo normal.
    void onCardWrite(int port, const std::string &path)
    {
        if (port < 0 || port > 1)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<uint8_t> now = readFile(path);
        const std::vector<uint8_t> &before = g_card[port];
        for (int k = 0; k < kSlots; ++k)
        {
            const size_t at = kHeader + k * kSlotStride;
            if (at + kSlotSize > now.size())
                break;
            const bool changed = before.size() < at + kSlotSize || !std::equal(now.begin() + at, now.begin() + at + kSlotSize, before.begin() + at);
            if (changed)
                writeCardLocked(port, k);
        }
        g_card[port] = std::move(now);
        g_cardPath[port] = path;
    }

    bool selfTest()
    {
        Config c; // os padrões do plano, independentes do mksm.ini
        c.xpFirst = 300, c.xpStep = 100, c.xpAccel = 50, c.maxLevel = 100, c.cap = 20, c.enemyDamage = c.enemyHealth = 30, c.enemyPerLevel = 0;
        c.capWisdom = 5;
        c.xpExtra[0] = c.xpExtra[1] = c.xpExtra[2] = 0; // a tabela abaixo é a curva base; os acréscimos são conferidos depois
        const double per[AttrCount] = {5, 3, 5, 1, 0.1, 3};
        std::copy(std::begin(per), std::end(per), c.perPoint);
        const int64_t xps[] = {0, 299, 300, 699, 700, 1250, 2000, 10500, 71250, 8357249, 8357250, 999999999};
        const int levels[] = {1, 1, 2, 2, 3, 4, 5, 10, 20, 99, 100, 100};
        for (size_t i = 0; i < std::size(xps); ++i)
            if (levelFor(c, xps[i]) != levels[i])
                return false;
        int64_t in = 0, step = 0;
        if (levelFor(c, 0, &in, &step) != 1 || in != 0 || step != 300 || (levelFor(c, 1000, &in, &step), in != 300 || step != 550))
            return false;
        Config e = c; // com os acréscimos por faixa: +100 até o 10, +200 do 10 ao 29, +300 do 30 em diante
        e.xpExtra[0] = 100, e.xpExtra[1] = 200, e.xpExtra[2] = 300;
        if (stepCost(e, 1) != 400 || stepCost(e, 4) != 850 || stepCost(e, 9) != 2600 || stepCost(e, 10) != 3200 ||
            stepCost(e, 29) != 22200 || stepCost(e, 30) != 23800 || levelFor(e, 399) != 1 || levelFor(e, 400) != 2)
            return false;
        if (capFor(c, Sabedoria) != 5 || capFor(c, Forca) != 20)
            return false;
        if (levelStartXp(c, 1) != 0 || levelStartXp(c, 3) != 700 || levelStartXp(e, 3) != 900)
            return false;
        const auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
        return near(effect(c, Forca, 20), 1.0) && near(effect(c, Defesa, 20), 0.6) && near(effect(c, Defesa, 40), 0.9) &&
        near(effect(c, Regen, 200), 0.1) && near(effect(c, Sabedoria, 1), 0.03) && dealtDamage(c, 100, 4, 1) == 92 &&
        dealtDamage(c, 100, 0, 1) == 77 && takenDamage(c, 100, 0, 1) == 130 && takenDamage(c, 1, 20, 1) == 1;
    }
}

namespace mksm_rpg
{
    // Debug save state: the RPG balance goes back with the game (XP must not count twice).
    void saveState(ps2x::savestate::Writer &w)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        w.pod(g_state), w.pod(g_fighter), w.pod(g_fighterPending);
    }
    void loadState(ps2x::savestate::Reader &r)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        r.pod(g_state), r.pod(g_fighter), r.pod(g_fighterPending);
    }
}
