#version 450
// Track F batching renderer: GS vertices already in target pixels, depth normalized.
layout(location = 0) in vec4 inPos;   // x, y (GS pixels), depth 0..1, q
layout(location = 1) in vec4 inColor; // r, g, b, a as 0..255
layout(location = 2) in vec4 inTex;   // s, t (or u, v in texels), fog 0..255, unused

layout(push_constant) uniform PC
{
    vec2 targetSize;
    vec2 texSize;
    uint flags;
    uint tfxTcc;
    uint alphaTest;
    uint fogColor;
    uint wrap;
    uint clampU;
    uint clampV;
    float scale;
    vec2 texOffset;
    float depthScale;
    uint texa; // ta0 | aem << 8 | 512 = 24-bit texture (alpha from TEXA)
} pc;

layout(location = 0) noperspective out vec4 vColor;
layout(location = 1) noperspective out vec3 vStq;
layout(location = 2) noperspective out float vFog;

void main()
{
    gl_Position = vec4(inPos.x / pc.targetSize.x * 2.0 - 1.0, inPos.y / pc.targetSize.y * 2.0 - 1.0, inPos.z, 1.0);
    vColor = inColor;
    vStq = vec3(inTex.xy, inPos.w);
    vFog = inTex.z;
}
