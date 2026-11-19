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
            std::memcpy(&v, rdram + (addr & 0x1FFFFFFu), 4);
            return v;
        }
        void wr32(uint8_t *rdram, uint32_t addr, uint32_t v) { std::memcpy(rdram + (addr & 0x1FFFFFFu), &v, 4); }
        uint32_t saveBlock(const uint8_t *rdram) { return rd32(rdram, 0x4D8B80u); }
        bool inGameplay(const uint8_t *rdram) // fora de pausa, menu e carregamento (N21)
        {
            return rd32(rdram, 0x4F4B68u) == 1u && rd32(rdram, 0x6BEE2Cu) == 0u && rd32(rdram, 0x4C49E8u) == 0u;
        }

        PS2Runtime::RecompiledFunction g_origFatFull, g_origFat, g_origFatOver, g_origDamage, g_origFrame, g_origDifficulty, g_origRoom;

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
                // eles causam pouco e apanham muito. Vale com ou sem RPG. shortcut: só o Shao Kahn (64); outros entram aqui.
                for (int j = 0; normalHit && j < 2; ++j)
                {
                    if (!objs[j] || static_cast<int32_t>(rd32(rdram, save + (j ? 0xD0u : 0xCCu))) != 64)
                        continue;
                    if (attacker == objs[j])
                        scaled = static_cast<int32_t>(std::lround(scaled * 2.5));
                    else if (target == objs[j])
                        scaled = std::max<int32_t>(1, static_cast<int32_t>(std::lround(scaled * 0.7)));
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

        // A cada quadro de jogo: vida máxima com o atributo Vida e Regeneração. Sozinho, o jogador tem toda a
        // vida (save+0x90 × save+0x94); no co-op (save+0x88 = 1) a vida é um total dividido: cada um tem metade,
        // aqui com a Vida de cada um, e save+0x94 fica na média (o jogo soma as duas metades no save).
        void multalityLocked(uint8_t *rdram);
        void hookFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                const uint32_t save = saveBlock(rdram), mode = save ? rd32(rdram, save + 0x88u) : 9u;
                if (save && mode <= 1u)
                    multalityLocked(rdram);
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
            for (uint32_t off : {0xCCu, 0xD0u})
            {
                const int32_t f = static_cast<int32_t>(rd32(rdram, save + off));
                if (off == 0xD0u && !rd32(rdram, 0x6826ACu))
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
        int32_t g_multEntry = -1;
        uint32_t g_multCat = 0;
        bool g_multDone = false;
        void multalityLocked(uint8_t *rdram)
        {
            const uint32_t n = std::min<uint32_t>(rd32(rdram, 0x5BE334u), 32u);
            const uint32_t cat = rd32(rdram, 0x4AD58Cu);
            const bool want = cat == 2u || cat == 3u;
            if (!want || (g_multEntry >= 0 && cat != g_multCat))
            {
                if (g_multEntry >= 0 && static_cast<uint32_t>(g_multEntry) < n &&
                    rd32(rdram, 0x5BDD58u + g_multEntry * 0x3Cu + 0x14u) == g_multCat)
                    wr32(rdram, 0x5BDD58u + g_multEntry * 0x3Cu + 0x14u, 4u);
                g_multEntry = -1;
                g_multDone = false;
                return;
            }
            if (g_multEntry < 0)
            {
                int32_t first = -1;
                for (uint32_t i = 0; i < n; ++i)
                {
                    const uint32_t c = rd32(rdram, 0x5BDD58u + i * 0x3Cu + 0x14u);
                    if (c == cat)
                        return; // tem a de verdade
                    if (c == 4u && first < 0)
                        first = static_cast<int32_t>(i);
                }
                if (first < 0)
                    return;
                g_multEntry = first;
                g_multCat = cat;
                wr32(rdram, 0x5BDD58u + first * 0x3Cu + 0x14u, cat);
            }
            if (rd32(rdram, 0x5BDD50u) >= 4u) // as 4 setas; o botão final não passa por esse contador
                g_multDone = true;
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
        replace(runtime, 0x128678u, hookFatFull, g_origFatFull);
        replace(runtime, 0x128630u, hookFat, g_origFat);
        replace(runtime, 0x1285C8u, hookFatOver, g_origFatOver);
        replace(runtime, 0x284FE0u, hookDamage, g_origDamage);
        replace(runtime, 0x183978u, hookFrame, g_origFrame);
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
