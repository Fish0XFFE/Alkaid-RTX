#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 color;
    vec3 normal;
    vec3 albedo;
    vec2 uv;
    float material;
    float distance;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

layout(binding = 2, std430) readonly buffer VertexBuffer { float verts[]; };
layout(binding = 3, std430) readonly buffer IndexBuffer  { uint  idxs[]; };

void main() {
    uint pid = uint(gl_PrimitiveID);
    uint i0 = idxs[pid * 3u + 0u];
    uint i1 = idxs[pid * 3u + 1u];
    uint i2 = idxs[pid * 3u + 2u];

    vec3 p0 = vec3(verts[i0 * 12u + 0u], verts[i0 * 12u + 1u], verts[i0 * 12u + 2u]);
    vec3 p1 = vec3(verts[i1 * 12u + 0u], verts[i1 * 12u + 1u], verts[i1 * 12u + 2u]);
    vec3 p2 = vec3(verts[i2 * 12u + 0u], verts[i2 * 12u + 1u], verts[i2 * 12u + 2u]);

    vec3 hitP = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    vec3 v0 = p1 - p0;
    vec3 v1 = p2 - p0;
    vec3 v2 = hitP - p0;

    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);

    float denom = d00 * d11 - d01 * d01;
    if (abs(denom) < 1e-12) denom = 1.0;

    float bv = (d11 * d20 - d01 * d21) / denom;
    float bw = (d00 * d21 - d01 * d20) / denom;
    float bu = 1.0 - bv - bw;

    bv = clamp(bv, 0.0, 1.0);
    bw = clamp(bw, 0.0, 1.0);
    bu = clamp(bu, 0.0, 1.0);

    vec3 c0 = vec3(verts[i0 * 12u + 6u], verts[i0 * 12u + 7u], verts[i0 * 12u + 8u]);
    vec3 c1 = vec3(verts[i1 * 12u + 6u], verts[i1 * 12u + 7u], verts[i1 * 12u + 8u]);
    vec3 c2 = vec3(verts[i2 * 12u + 6u], verts[i2 * 12u + 7u], verts[i2 * 12u + 8u]);
    vec3 color = c0 * bu + c1 * bv + c2 * bw;

    vec3 n0 = vec3(verts[i0 * 12u + 3u], verts[i0 * 12u + 4u], verts[i0 * 12u + 5u]);
    vec3 n1 = vec3(verts[i1 * 12u + 3u], verts[i1 * 12u + 4u], verts[i1 * 12u + 5u]);
    vec3 n2 = vec3(verts[i2 * 12u + 3u], verts[i2 * 12u + 4u], verts[i2 * 12u + 5u]);
    vec3 n = normalize(n0 * bu + n1 * bv + n2 * bw);
    if (dot(n, gl_WorldRayDirectionEXT) > 0.0) n = -n;

    vec2 uv0 = vec2(verts[i0 * 12u + 9u], verts[i0 * 12u + 10u]);
    vec2 uv1 = vec2(verts[i1 * 12u + 9u], verts[i1 * 12u + 10u]);
    vec2 uv2 = vec2(verts[i2 * 12u + 9u], verts[i2 * 12u + 10u]);
    vec2 uv = uv0 * bu + uv1 * bv + uv2 * bw;

    float m = verts[i0 * 12u + 11u];

    payload.color = color;
    payload.albedo = color;
    payload.normal = n;
    payload.uv = uv;
    payload.material = m;
    payload.distance = gl_HitTEXT;
}