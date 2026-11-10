package com.fish.yaoguang.client;

final class SceneBuffers {
    static final int VERTEX_FLOATS = 20;
    static final int MAX_QUADS = 1_200_000;

    private float[] verts = new float[1 << 20];
    private int[] indices = new int[1 << 19];
    private int vertexCount;
    private int indexCount;
    private int quadCount;

    void reset() {
        vertexCount = 0;
        indexCount = 0;
        quadCount = 0;
    }

    boolean full() {
        return quadCount >= MAX_QUADS;
    }

    int quadCount() {
        return quadCount;
    }

    float[] vertices() {
        return verts;
    }

    int[] indices() {
        return indices;
    }

    int vertexFloatCount() {
        return vertexCount;
    }

    int indexCount() {
        return indexCount;
    }

    void quad(float x0, float y0, float z0,
              float x1, float y1, float z1,
              float x2, float y2, float z2,
              float x3, float y3, float z3,
              float nx, float ny, float nz,
              float r, float g, float b,
              float u0, float v0,
              float u1, float v1,
              float u2, float v2,
              float u3, float v3,
              int material) {
        quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3,
                nx, ny, nz, r, g, b, u0, v0, u1, v1, u2, v2, u3, v3,
                material, defaultRoughness(material), defaultMetalness(material),
                defaultEmission(material), defaultTranslucency(material));
    }

    void quad(float x0, float y0, float z0,
              float x1, float y1, float z1,
              float x2, float y2, float z2,
              float x3, float y3, float z3,
              float nx, float ny, float nz,
              float r, float g, float b,
              float u0, float v0,
              float u1, float v1,
              float u2, float v2,
              float u3, float v3,
              int material, float roughness, float metalness,
              float emission, float translucency) {
        quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3,
                nx, ny, nz, r, g, b, u0, v0, u1, v1, u2, v2, u3, v3,
                material, roughness, metalness, emission, translucency, 1.0f, 0.0f, 0.0f);
    }

    void quad(float x0, float y0, float z0,
              float x1, float y1, float z1,
              float x2, float y2, float z2,
              float x3, float y3, float z3,
              float nx, float ny, float nz,
              float r, float g, float b,
              float u0, float v0,
              float u1, float v1,
              float u2, float v2,
              float u3, float v3,
              int material, float roughness, float metalness,
              float emission, float translucency, float skyLight, float blockLight, float alphaTest) {
        if (full()) return;
        ensure(4, 6);
        int base = vertexCount / VERTEX_FLOATS;
        vertex(x0, y0, z0, nx, ny, nz, r, g, b, u0, v0, material, roughness, metalness, emission, translucency, skyLight, blockLight, alphaTest);
        vertex(x1, y1, z1, nx, ny, nz, r, g, b, u1, v1, material, roughness, metalness, emission, translucency, skyLight, blockLight, alphaTest);
        vertex(x2, y2, z2, nx, ny, nz, r, g, b, u2, v2, material, roughness, metalness, emission, translucency, skyLight, blockLight, alphaTest);
        vertex(x3, y3, z3, nx, ny, nz, r, g, b, u3, v3, material, roughness, metalness, emission, translucency, skyLight, blockLight, alphaTest);
        indices[indexCount++] = base;
        indices[indexCount++] = base + 1;
        indices[indexCount++] = base + 2;
        indices[indexCount++] = base;
        indices[indexCount++] = base + 2;
        indices[indexCount++] = base + 3;
        quadCount++;
    }

    private void vertex(float x, float y, float z,
                        float nx, float ny, float nz,
                        float r, float g, float b,
                        float u, float v,
                        int material, float roughness, float metalness,
                        float emission, float translucency, float skyLight, float blockLight, float alphaTest) {
        verts[vertexCount++] = x;
        verts[vertexCount++] = y;
        verts[vertexCount++] = z;
        verts[vertexCount++] = nx;
        verts[vertexCount++] = ny;
        verts[vertexCount++] = nz;
        verts[vertexCount++] = r;
        verts[vertexCount++] = g;
        verts[vertexCount++] = b;
        verts[vertexCount++] = u;
        verts[vertexCount++] = v;
        verts[vertexCount++] = material;
        verts[vertexCount++] = roughness;
        verts[vertexCount++] = metalness;
        verts[vertexCount++] = emission;
        verts[vertexCount++] = translucency;
        verts[vertexCount++] = skyLight;
        verts[vertexCount++] = blockLight;
        verts[vertexCount++] = alphaTest;
        verts[vertexCount++] = 0.0f;
    }

    static float defaultRoughness(int material) {
        return switch (material) {
            case 1 -> 0.42f;
            case 2 -> 0.07f;
            case 3 -> 0.01f;
            case 4 -> 0.7f;
            case 5 -> 0.9f;
            case 6 -> 0.35f;
            case 7 -> 0.8f;
            case 8 -> 0.55f;
            case 9 -> 0.72f;
            default -> 0.85f;
        };
    }

    static float defaultMetalness(int material) {
        return material == 1 ? 0.50f : 0.0f;
    }

    static float defaultEmission(int material) {
        return switch (material) {
            case 4 -> 1.6f;
            case 5 -> 1.2f;
            case 6 -> 2.6f;
            case 7 -> 0.35f;
            default -> 0.0f;
        };
    }

    static float defaultTranslucency(int material) {
        return switch (material) {
            case 2, 3 -> 1.0f;
            case 9 -> 0.25f;
            default -> 0.0f;
        };
    }

    private void ensure(int extraVerts, int extraIndices) {
        int needVerts = vertexCount + extraVerts * VERTEX_FLOATS;
        if (needVerts > verts.length) {
            int size = verts.length;
            while (size < needVerts) size += size / 2;
            float[] next = new float[size];
            System.arraycopy(verts, 0, next, 0, vertexCount);
            verts = next;
        }
        int needIndices = indexCount + extraIndices;
        if (needIndices > indices.length) {
            int size = indices.length;
            while (size < needIndices) size += size / 2;
            int[] next = new int[size];
            System.arraycopy(indices, 0, next, 0, indexCount);
            indices = next;
        }
    }
}
