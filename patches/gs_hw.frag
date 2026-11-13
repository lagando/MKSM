#version 450
// Track F batching renderer: GS texture function, alpha test and fog in byte arithmetic.
// Targets are RGBA16F holding r,g,b / 255 and a / 128, so blend factors As/Ad/FIX are exact.
layout(location = 0) noperspective in vec4 vColor;
layout(location = 1) noperspective in vec3 vStq;
layout(location = 2) noperspective in float vFog;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PC
{
    vec2 targetSize;
    vec2 texSize;
    uint flags;     // 1 tme, 2 fst, 4 linear, 8 fge, 16 texture is a surface, 32 fba, 64 invert alpha test, 128 depth surface
    uint tfxTcc;    // tfx | tcc << 2
    uint alphaTest; // atst | aref << 8
    uint fogColor;  // r | g << 8 | b << 16
    uint wrap;      // wms | wmt << 2
    uint clampU;    // minu | maxu << 16
    uint clampV;    // minv | maxv << 16
    float scale;
    vec2 texOffset;
    float depthScale;
    uint texa; // ta0 | aem << 8 | 512 = 24-bit texture (alpha from TEXA)
} pc;

layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outFactor; // .a = As / 128 for the blend

int wrapCoord(int c, int size, uint mode, uint minv, uint maxv)
{
    if (mode == 0u) return c & (size - 1);
    if (mode == 1u) return clamp(c, 0, size - 1);
    if (mode == 2u) return clamp(c, int(minv), int(maxv));
    return int((uint(c) & minv) | maxv);
}

vec4 fetchTexel(ivec2 c)
{
    ivec2 size = ivec2(pc.texSize);
    c.x = wrapCoord(c.x, size.x, pc.wrap & 3u, pc.clampU & 0xFFFFu, pc.clampU >> 16);
    c.y = wrapCoord(c.y, size.y, (pc.wrap >> 2) & 3u, pc.clampV & 0xFFFFu, pc.clampV >> 16);
    // Never fetch outside the image: an out-of-bounds fetch can fault the GPU channel.
    ivec2 limit = textureSize(tex, 0) - 1;
    if ((pc.flags & 16u) == 0u)
        return texelFetch(tex, clamp(c, ivec2(0), limit), 0) * 255.0;
    ivec2 p = clamp(ivec2((vec2(c) + pc.texOffset + 0.5) * pc.scale), ivec2(0), limit);
    vec4 v = texelFetch(tex, p, 0);
    if ((pc.flags & 128u) != 0u)
    {
        uint z = uint(clamp(v.r, 0.0, 1.0) * pc.depthScale);
        return vec4(float(z & 0xFFu), float((z >> 8) & 0xFFu), float((z >> 16) & 0xFFu), float(z >> 24));
    }
    vec4 t = vec4(clamp(v.rgb, 0.0, 1.0) * 255.0, clamp(v.a * 128.0, 0.0, 255.0));
    if ((pc.texa & 512u) != 0u) // 24-bit read of a surface: alpha from TEXA
        t.a = ((pc.texa & 256u) != 0u && all(lessThan(t.rgb, vec3(0.5)))) ? 0.0 : float(pc.texa & 0xFFu);
    return t;
}

vec4 sampleTexture()
{
    vec2 uv = (pc.flags & 2u) != 0u ? vStq.xy : vStq.xy / abs(vStq.z) * pc.texSize;
    if ((pc.flags & 4u) == 0u)
        return floor(fetchTexel(ivec2(floor(uv))));
    vec2 f = uv - 0.5;
    ivec2 c = ivec2(floor(f));
    vec2 w = floor((f - floor(f)) * 256.0) / 256.0;
    vec4 top = mix(floor(fetchTexel(c)), floor(fetchTexel(c + ivec2(1, 0))), w.x);
    vec4 bottom = mix(floor(fetchTexel(c + ivec2(0, 1))), floor(fetchTexel(c + ivec2(1, 1))), w.x);
    return floor(mix(top, bottom, w.y));
}

void main()
{
    vec4 c = floor(vColor + 0.5); // Gouraud rounds (PLANO_DESEMPENHO E1)
    if ((pc.flags & 1u) != 0u)
    {
        vec4 t = sampleTexture();
        uint tfx = pc.tfxTcc & 3u;
        bool tcc = (pc.tfxTcc & 4u) != 0u;
        vec4 r;
        if (tfx == 0u)      { r.rgb = floor(t.rgb * c.rgb / 128.0); r.a = tcc ? floor(t.a * c.a / 128.0) : c.a; }
        else if (tfx == 1u) { r.rgb = t.rgb; r.a = tcc ? t.a : c.a; }
        else if (tfx == 2u) { r.rgb = floor(t.rgb * c.rgb / 128.0) + c.a; r.a = tcc ? t.a + c.a : c.a; }
        else                { r.rgb = floor(t.rgb * c.rgb / 128.0) + c.a; r.a = tcc ? t.a : c.a; }
        c = clamp(r, 0.0, 255.0);
    }

    uint atst = pc.alphaTest & 7u;
    float aref = float((pc.alphaTest >> 8) & 0xFFu);
    bool pass = true;
    if (atst == 0u) pass = false;
    else if (atst == 2u) pass = c.a < aref;
    else if (atst == 3u) pass = c.a <= aref;
    else if (atst == 4u) pass = c.a == aref;
    else if (atst == 5u) pass = c.a >= aref;
    else if (atst == 6u) pass = c.a > aref;
    else if (atst == 7u) pass = c.a != aref;
    if ((pc.flags & 64u) != 0u) pass = !pass;
    if (!pass)
        discard;

    if ((pc.flags & 8u) != 0u)
    {
        float fog = floor(vFog + 0.5);
        vec3 fc = vec3(float(pc.fogColor & 0xFFu), float((pc.fogColor >> 8) & 0xFFu), float((pc.fogColor >> 16) & 0xFFu));
        c.rgb = floor(fog * c.rgb / 256.0) + floor((255.0 - fog) * fc / 256.0);
    }

    outFactor = vec4(0.0, 0.0, 0.0, c.a / 128.0);
    float stored = c.a;
    if ((pc.flags & 32u) != 0u && stored < 128.0)
        stored += 128.0;
    outColor = vec4(c.rgb / 255.0, stored / 128.0);
}
