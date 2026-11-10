#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 albedo;
    vec3 normal;
    vec3 emissive;
    vec3 transmittance;
    vec2 uv;
    float material;
    float roughness;
    float metalness;
    float distance;
    float skyLight;
    float blockLight;
    int op;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

layout(binding = 2, std430) readonly buffer VertexBuffer { float verts[]; };
layout(binding = 3, std430) readonly buffer IndexBuffer { uint idxs[]; };
layout(binding = 11, std430) readonly buffer ChunkTable { uvec4 chunkTable[]; };
layout(binding = 4) uniform sampler2D atlasTex;

layout(push_constant) uniform PC {
    uint maxBounces;
    uint samplesPerPixel;
    uint frameIndex;
    uint flags;
    float exposure;
    float rayLength;
    float sunIntensity;
    float ambientIntensity;
    vec4 camPos;
    vec4 camDir;
    vec4 camUp;
    float fov;
    float aspect;
    float time;
    float wetness;
    uint dynamicCount;
    uint particleCount;
    uint pad0;
    uint pad1;
    vec4 sunDir;
    vec4 weather;
    vec4 prevCamPos;
    vec4 prevCamDir;
    vec4 prevCamUp;
    vec4 lens;
    vec4 misc;
    vec4 sunColor;
} pc;

const int STRIDE = 20;

float fetchComponent(uint index, int component) {
    return verts[index * uint(STRIDE) + uint(component)];
}

void main() {
    uvec4 chunk = chunkTable[gl_InstanceCustomIndexEXT];
    uint vbase = chunk.x;
    uint ibase = chunk.y;
    uint prim = ibase + uint(gl_PrimitiveID) * 3u;
    uint i0 = idxs[prim + 0] + vbase;
    uint i1 = idxs[prim + 1] + vbase;
    uint i2 = idxs[prim + 2] + vbase;

    float b0 = 1.0 - attribs.x - attribs.y;
    float b1 = attribs.x;
    float b2 = attribs.y;

    vec3 position = vec3(fetchComponent(i0, 0), fetchComponent(i0, 1), fetchComponent(i0, 2)) * b0 +
                    vec3(fetchComponent(i1, 0), fetchComponent(i1, 1), fetchComponent(i1, 2)) * b1 +
                    vec3(fetchComponent(i2, 0), fetchComponent(i2, 1), fetchComponent(i2, 2)) * b2;
    vec3 normal = normalize(vec3(fetchComponent(i0, 3), fetchComponent(i0, 4), fetchComponent(i0, 5)) * b0 +
                            vec3(fetchComponent(i1, 3), fetchComponent(i1, 4), fetchComponent(i1, 5)) * b1 +
                            vec3(fetchComponent(i2, 3), fetchComponent(i2, 4), fetchComponent(i2, 5)) * b2);
    vec3 color = vec3(fetchComponent(i0, 6), fetchComponent(i0, 7), fetchComponent(i0, 8)) * b0 +
                 vec3(fetchComponent(i1, 6), fetchComponent(i1, 7), fetchComponent(i1, 8)) * b1 +
                 vec3(fetchComponent(i2, 6), fetchComponent(i2, 7), fetchComponent(i2, 8)) * b2;
    vec2 uv = vec2(fetchComponent(i0, 9), fetchComponent(i0, 10)) * b0 +
              vec2(fetchComponent(i1, 9), fetchComponent(i1, 10)) * b1 +
              vec2(fetchComponent(i2, 9), fetchComponent(i2, 10)) * b2;

    float material = fetchComponent(i0, 11);
    float roughness = fetchComponent(i0, 12);
    float metalness = fetchComponent(i0, 13);
    float emission = fetchComponent(i0, 14);
    float skyLight = fetchComponent(i0, 16);
    float blockLight = fetchComponent(i0, 17);
    float alphaTest = fetchComponent(i0, 18);

    if (any(isnan(uv)) || any(isinf(uv))) uv = vec2(0.5);
    uv = clamp(uv, vec2(0.0), vec2(1.0));


    float worldPerPixel = 2.0 * gl_HitTEXT * tan(radians(pc.fov) * 0.5) / max(pc.misc.w, 1.0);
    float lod = clamp(log2(max(worldPerPixel * 2.0, 1.0)), 0.0, 2.0);
    vec4 texel;
    float soft = 1.0;
    if (alphaTest > 0.5) {
        lod = 0.0;
        ivec2 asize = textureSize(atlasTex, 0);
        texel = texelFetch(atlasTex, clamp(ivec2(uv * vec2(asize)), ivec2(0), asize - 1), 0);
        float a = max(texel.a, 1.0e-3);
        if (!(material > 3.5 && material < 7.5)) texel.rgb = texel.rgb / max(a, 0.35);
        soft = smoothstep(0.35, 0.80, texel.a);
        float cutThresh = (material > 8.5 && material < 9.5) ? 0.5 : ((material > 4.5 && material < 7.5) ? 0.06 : 0.22);

    } else {
        texel = textureLod(atlasTex, uv, lod);
    }

    float softA = soft;
    vec3 albedo = texel.rgb * color;
    if (material > 12.5 && material < 13.5) albedo = color;
    vec3 worldNormal = normal;
    if (dot(worldNormal, gl_WorldRayDirectionEXT) > 0.0) worldNormal = -worldNormal;

    if (payload.op == 1) {
        payload.distance = gl_HitTEXT;
        return;
    }

    payload.albedo = albedo;
    payload.normal = worldNormal;
    payload.emissive = albedo * emission * softA;
    payload.uv = uv;
    payload.material = material;
    payload.roughness = roughness;
    payload.metalness = metalness;
    payload.distance = gl_HitTEXT;
    payload.skyLight = skyLight;
    payload.blockLight = blockLight;
}
