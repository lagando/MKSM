#include "gs_hw_backend.h"

#include "runtime/gs/ps2_gs_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

GSHwBackend::GSHwBackend(GSRasterBackend &inner) : m_inner(inner) {}

std::unique_ptr<GSRasterBackend> GSHwBackend::WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner)
{
    const char *mode = std::getenv("PS2X_GS_HW");
    if (!mode || std::strcmp(mode, "count") != 0)
        return inner;
    auto wrapper = std::make_unique<GSHwBackend>(*inner);
    wrapper->m_owned = std::move(inner);
    return wrapper;
}

void GSHwBackend::Submit(const GSPrimitiveBatch &batch)
{
    const GSDrawState &state = batch.state;
    const GSContext &ctx = state.context;
    const uint32_t frameBlock = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const uint32_t depthBlock = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);

    // What a plain GPU draw cannot change inside one batch.
    Key key;
    key.target = (uint64_t(frameBlock) << 32) | (uint64_t(ctx.frame.fbw) << 24) | (uint64_t(ctx.frame.psm) << 16) |
                 (uint64_t(depthBlock & 0x3FFFu) << 2) | (ctx.zbuf.zmask ? 1u : 0u);
    key.texture = state.prim.tme ? (((uint64_t(ctx.tex0.tbp0) << 40) | (uint64_t(ctx.tex0.psm) << 32) | (uint64_t(ctx.tex0.cbp) << 16) |
                                     (uint64_t(ctx.tex0.tw) << 12) | (uint64_t(ctx.tex0.th) << 8) | (uint64_t(ctx.tex0.tfx) << 2) |
                                     ctx.tex0.tcc) ^
                                    (ctx.clamp * 0x9E3779B97F4A7C15ull) ^ (uint64_t(state.linearFilter) << 63))
                                 : 0u;
    key.blend = state.prim.abe ? ctx.alpha | (uint64_t(state.pabe) << 63) : 0u;
    key.test = ctx.test & 0x7FFFFull;
    key.other = (uint64_t(state.prim.type == GS_PRIM_SPRITE) << 0) | (uint64_t(state.prim.tme) << 1) | (uint64_t(state.prim.fge) << 2) |
                (uint64_t(ctx.fba & 1u) << 3) | (uint64_t(ctx.frame.fbmsk) << 8);

    ++m_stats.primitives;
    if (state.prim.tme && (ctx.tex0.tbp0 == frameBlock || ctx.tex0.tbp0 == depthBlock))
        ++m_stats.feedback;
    if (!m_open)
        ++m_stats.batches, m_open = true;
    else if (key.target != m_last.target)
        ++m_stats.batches, ++m_stats.breaksTarget;
    else if (key.texture != m_last.texture)
        ++m_stats.batches, ++m_stats.breaksTexture;
    else if (key.blend != m_last.blend)
        ++m_stats.batches, ++m_stats.breaksBlend;
    else if (key.test != m_last.test)
        ++m_stats.batches, ++m_stats.breaksTest;
    else if (key.other != m_last.other)
        ++m_stats.batches, ++m_stats.breaksOther;
    m_last = key;
    m_inner.Submit(batch);
}

void GSHwBackend::BeginTransfer(const GSTransferCommand &command)
{
    m_open = false; // a transfer can rewrite a texture or target: the next draw starts a batch
    m_inner.BeginTransfer(command);
}

PresentationFrame GSHwBackend::Present(const GSPresentationRequest &request)
{
    ++m_stats.frames;
    if (m_stats.frames % 120u == 0u)
        report();
    return m_inner.Present(request);
}

void GSHwBackend::report() const
{
    const Stats &s = m_stats;
    const double frames = s.frames ? static_cast<double>(s.frames) : 1.0;
    std::fprintf(stderr,
                 "[gs:hw] per frame: %.0f primitives in %.0f batches (breaks: target %.0f, texture %.0f, blend %.0f, test %.0f, other %.0f), feedback %.0f\n",
                 s.primitives / frames, s.batches / frames, s.breaksTarget / frames, s.breaksTexture / frames, s.breaksBlend / frames,
                 s.breaksTest / frames, s.breaksOther / frames, s.feedback / frames);
}
