#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 color;
    vec3 normal;
    vec3 albedo;
    float material;
    float distance;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.color = vec3(0.0);
    payload.albedo = vec3(0.0);
    payload.normal = vec3(0.0);
    payload.material = -1.0;
    payload.distance = 1e30;
}