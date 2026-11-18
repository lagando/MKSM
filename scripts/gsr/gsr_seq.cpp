// Sequência de desenho de um quadro de uma gravação .gsr (F9), agrupada por estado (alvo, tipo, textura, blend).
// uso: gsr_seq arquivo.gsr [quadro]   (compile: g++ -O1 -std=c++20 -I PS2Recomp/ps2xRuntime/include gsr_seq.cpp)
#include "runtime/gs/gs_types.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
struct LoadClutRecord { GSTex0Reg tex0; GSTexClutReg texclut; };
struct WriteVramRecord { uint32_t psm, base, bw, x, y, value; };
struct ClearRecord { GSContext context; uint32_t rgba; };
int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "uso: gsr_seq arquivo.gsr [quadro]\n"); return 1; }
    const int want = argc > 2 ? std::atoi(argv[2]) : 1;
    FILE *f = std::fopen(argv[1], "rb");
    if (!f) return 1;
    std::vector<uint8_t> b; uint8_t buf[65536]; size_t g;
    while ((g = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + g);
    std::fclose(f);
    size_t pos = 8 + 5 * 4; uint32_t sz;
    for (int i = 0; i < 2; ++i) { std::memcpy(&sz, &b[pos], 4); pos += 4 + sz; }
    struct G { uint32_t fbp, fpsm, zbp, tbp, tpsm, cpsm; int type, tme, abe, zmsk, fge; uint64_t alpha, test, fbmsk; long n; double r, gg, bb, a; float minx, miny, maxx, maxy; } cur{};
    bool have = false; int idx = 0, frame = 0;
    const auto flush = [&] {
        if (!have) return;
        std::printf("%4d fbp %03x fpsm %u t%d n=%-5ld tme%d tbp %x tpsm %u cpsm %u abe%d alpha=%llx test=%llx fbmsk=%08llx zmsk%d fge%d rgba=(%.0f %.0f %.0f %.0f) xy %.0f,%.0f-%.0f,%.0f\n",
                    idx++, cur.fbp, cur.fpsm, cur.type, cur.n, cur.tme, cur.tbp, cur.tpsm, cur.cpsm, cur.abe, (unsigned long long)cur.alpha, (unsigned long long)cur.test,
                    (unsigned long long)cur.fbmsk, cur.zmsk, cur.fge, cur.r / cur.n, cur.gg / cur.n, cur.bb / cur.n, cur.a / cur.n, cur.minx, cur.miny, cur.maxx, cur.maxy);
        have = false;
    };
    while (pos < b.size())
    {
        const uint8_t k = b[pos++];
        if (k == 0) break;
        if (k == 1)
        {
            GSPrimitiveBatch p; std::memcpy(&p, &b[pos], sizeof p); pos += sizeof p;
            if (frame != want) continue;
            const auto &c = p.state.context;
            G n{c.frame.fbp, c.frame.psm, c.zbuf.zbp, c.tex0.tbp0, c.tex0.psm, c.tex0.cpsm, p.state.prim.type, p.state.prim.tme, p.state.prim.abe, (int)c.zbuf.zmask, p.state.prim.fge, c.alpha, c.test, c.frame.fbmsk};
            if (!have || n.fbp != cur.fbp || n.type != cur.type || n.tme != cur.tme || n.tbp != cur.tbp || n.abe != cur.abe || n.alpha != cur.alpha || n.test != cur.test || n.fbmsk != cur.fbmsk)
            {
                flush(); cur = n; cur.minx = cur.miny = 1e9f; cur.maxx = cur.maxy = -1e9f; have = true;
            }
            for (int v = 0; v < p.vertexCount; ++v)
            {
                const auto &x = p.vertices[v];
                cur.r += x.r / (double)p.vertexCount, cur.gg += x.g / (double)p.vertexCount, cur.bb += x.b / (double)p.vertexCount, cur.a += x.a / (double)p.vertexCount;
                cur.minx = std::fmin(cur.minx, x.x), cur.miny = std::fmin(cur.miny, x.y), cur.maxx = std::fmax(cur.maxx, x.x), cur.maxy = std::fmax(cur.maxy, x.y);
            }
            cur.n++;
        }
        else if (k == 2) pos += sizeof(LoadClutRecord);
        else if (k == 3) pos += sizeof(GSTransferCommand);
        else if (k == 4) { std::memcpy(&sz, &b[pos], 4); pos += 4 + sz; }
        else if (k == 5) { flush(); ++frame; }
        else if (k == 6) pos += sizeof(WriteVramRecord);
        else if (k == 7) pos += sizeof(ClearRecord);
        else if (k == 9) pos += 4;
    }
    flush();
}
