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
    int op;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.material = -1.0;
    payload.distance = 1.0e30;
    payload.albedo = vec3(0.0);
    payload.normal = vec3(0.0, 1.0, 0.0);
    payload.emissive = vec3(0.0);
    payload.uv = vec2(0.0);
    payload.roughness = 0.5;
    payload.metalness = 0.0;
}
