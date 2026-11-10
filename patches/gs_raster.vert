#version 450
// Rasterizer path: three vertices per GS record, read from the record itself. Triangles use
// (words 60,61), (62,63), (30,31) (offset-adjusted, pixel units). A general sprite is one
// triangle twice its scissored rectangle (words 1-4), cut back by the fragment's box test:
// two triangles would both cover the diagonal under conservative rasterization.
layout(std430, binding = 4) restrict readonly buffer Records { uint rec[]; };
layout(push_constant) uniform Batch
{
    uint tileBase, recordBase;
    uint upBlock, upWidth, upPsm, upX, upY, upRowWidth, upFirstPixel, upCount, upData;
    uint srcBlock, srcWidth, srcPsm, srcX, srcY;
} pc;
layout(location = 0) flat out uint vRecord;

void main()
{
    const uint record = pc.recordBase + uint(gl_VertexIndex) / 3u;
    const uint corner = uint(gl_VertexIndex) % 3u;
    const uint base = record * 64u;
    vec2 p;
    if (rec[base] == 0u)
    {
        // Normal rasterization of a dilated triangle. The fragment's edge test accepts
        // barycentrics >= -1e-4: that region is the triangle scaled by 1 + 3e-4 about its
        // center (2.5e-4 used here for float slack); each edge then moves out by kMargin
        // pixels for the rasterizer's subpixel snapping, the corner miter capped at 4 px.
        const vec2 v[3] = vec2[3](vec2(uintBitsToFloat(rec[base + 60u]), uintBitsToFloat(rec[base + 61u])),
                                  vec2(uintBitsToFloat(rec[base + 62u]), uintBitsToFloat(rec[base + 63u])),
                                  vec2(uintBitsToFloat(rec[base + 30u]), uintBitsToFloat(rec[base + 31u])));
        const vec2 center = (v[0] + v[1] + v[2]) / 3.0;
        const vec2 self = v[corner], next = v[(corner + 1u) % 3u], prev = v[(corner + 2u) % 3u];
        const float kMargin = 1.0 / 32.0;
        const float orientation = sign((next.x - self.x) * (prev.y - self.y) - (next.y - self.y) * (prev.x - self.x));
        // Outward unit normals of the two edges meeting here.
        const vec2 a = normalize(next - self), b = normalize(self - prev);
        const vec2 na = -orientation * vec2(-a.y, a.x), nb = -orientation * vec2(-b.y, b.x);
        vec2 miter = kMargin * (na + nb) / max(1.0 + dot(na, nb), 1e-6);
        if (length(miter) > 4.0)
            miter = normalize(miter) * 4.0;
        p = self + 7.5e-4 * (self - center) + miter;
    }
    else
    {
        const float x0 = float(int(rec[base + 1u])), x1 = float(int(rec[base + 2u]) + 1);
        const float y0 = float(int(rec[base + 3u])), y1 = float(int(rec[base + 4u]) + 1);
        p = corner == 0u ? vec2(x0, y0) : corner == 1u ? vec2(2.0 * x1 - x0, y0) : vec2(x0, 2.0 * y1 - y0);
    }
    gl_Position = vec4(p / 1024.0 - 1.0, 0.0, 1.0);
    vRecord = record;
}
