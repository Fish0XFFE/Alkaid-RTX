package com.fish.yaoguang.client;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.block.BlockRenderDispatcher;
import net.minecraft.client.renderer.block.model.BakedQuad;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.world.inventory.InventoryMenu;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.BakedModel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.tags.FluidTags;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.material.FluidState;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class SceneMesher {
    public static final int MAT_DIFFUSE = 0;
    public static final int MAT_METAL = 1;
    public static final int MAT_GLASS = 2;
    public static final int MAT_WATER = 3;
    public static final int MAT_EMISSIVE = 4;
    public static final int MAT_FIRE = 5;
    private static int UV_DEBUG_COUNT = 0;
    public static final int MAT_LAVA = 6;
    public static final int MAT_CAMPFIRE = 7;
    public static final int MAT_FOLIAGE = 9;
    public static final int MAT_GEM = 10;
    public static final int MAT_PORTAL = 14;

    private static final int NEAR_FULL_CHUNKS = 3;
    private static final int MID_LOD_CHUNKS = 10;
    private static final int MAX_LOD_STEP = 2;
    private static final Map<Block, Boolean> CULL_SAME_CACHE = new ConcurrentHashMap<>();
    private static final Map<Block, Boolean> FOLIAGE_CACHE = new ConcurrentHashMap<>();
    private static final float[] TOP_CORNERS = {0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1};
    private static final float[] NORTH_CORNERS = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    private static final float[] SOUTH_CORNERS = {0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};
    private static final float[] WEST_CORNERS = {0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0};
    private static final float[] EAST_CORNERS = {1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0};

    public static final class Mesh {
        public final float[] vertices;
        public final int[] indices;
        public final int quads;
        public final int chunks;
        public final int radiusChunks;

        Mesh(float[] vertices, int[] indices, int quads, int chunks, int radiusChunks) {
            this.vertices = vertices;
            this.indices = indices;
            this.quads = quads;
            this.chunks = chunks;
            this.radiusChunks = radiusChunks;
        }
    }

    public static final class ChunkMesh {
        public final float[] verts;
        public final int[] idx;

        ChunkMesh(float[] verts, int[] idx) {
            this.verts = verts;
            this.idx = idx;
        }
    }

    public static float[] waterUv(TextureAtlas atlas) {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (atlas != null) {
            ResourceLocation waterId = ResourceLocation.tryParse("minecraft:block/water_still");
            if (waterId != null) {
                var sprite = atlas.getSprite(waterId);
                if (sprite != null) {
                    u0 = sprite.getU0();
                    v0 = sprite.getV0();
                    u1 = sprite.getU1();
                    v1 = sprite.getV1();
                }
            }
        }
        return new float[]{u0, v0, u1, v1};
    }

    public static ChunkMesh buildSingleChunk(ClientLevel level, LevelChunk chunk, BlockRenderDispatcher dispatcher,
                                             TextureAtlas atlas) {
        float[] uv = waterUv(atlas);
        SceneBuffers out = new SceneBuffers();
        buildChunkFull(level, chunk, out, dispatcher, uv[0], uv[1], uv[2], uv[3]);
        float[] verts = new float[out.vertexFloatCount()];
        System.arraycopy(out.vertices(), 0, verts, 0, verts.length);
        int[] idx = new int[out.indexCount()];
        System.arraycopy(out.indices(), 0, idx, 0, idx.length);
        return new ChunkMesh(verts, idx);
    }

    public static Mesh assemble(java.util.List<ChunkMesh> meshes, int chunks, int radiusChunks) {
        int vf = 0;
        int ic = 0;
        for (ChunkMesh m : meshes) {
            vf += m.verts.length;
            ic += m.idx.length;
        }
        float[] verts = new float[vf];
        int[] idx = new int[ic];
        int vo = 0;
        int io = 0;
        int base = 0;
        for (ChunkMesh m : meshes) {
            System.arraycopy(m.verts, 0, verts, vo, m.verts.length);
            for (int k = 0; k < m.idx.length; k++) idx[io + k] = m.idx[k] + base;
            int vcount = m.verts.length / SceneBuffers.VERTEX_FLOATS;
            base += vcount;
            vo += m.verts.length;
            io += m.idx.length;
        }
        return new Mesh(verts, idx, base, chunks, radiusChunks);
    }

    public static Mesh build(ClientLevel level, BlockPos center, int radiusChunks,
                             TextureAtlas atlas, BlockRenderDispatcher dispatcher) {
        int radius = Math.max(2, Math.min(32, radiusChunks));
        SceneBuffers out = new SceneBuffers();
        Map<BlockState, LodFace> lodCache = new HashMap<>();
        RandomSource lodRandom = RandomSource.create(42L);
        int centerChunkX = center.getX() >> 4;
        int centerChunkZ = center.getZ() >> 4;
        int chunks = 0;

        float waterU0 = 0.0f;
        float waterV0 = 0.0f;
        float waterU1 = 1.0f;
        float waterV1 = 1.0f;

        if (atlas != null) {
            ResourceLocation waterId = ResourceLocation.tryParse("minecraft:block/water_still");
            if (waterId != null) {
                var sprite = atlas.getSprite(waterId);
                if (sprite != null) {
                    waterU0 = sprite.getU0();
                    waterV0 = sprite.getV0();
                    waterU1 = sprite.getU1();
                    waterV1 = sprite.getV1();
                }
            }
        }

        for (int ring = 0; ring <= radius && !out.full(); ring++) {
            for (int dz = -ring; dz <= ring && !out.full(); dz++) {
                for (int dx = -ring; dx <= ring && !out.full(); dx++) {
                    if (Math.max(Math.abs(dx), Math.abs(dz)) != ring) continue;
                    int cx = centerChunkX + dx;
                    int cz = centerChunkZ + dz;
                    LevelChunk chunk = level.getChunkSource().getChunk(cx, cz, net.minecraft.world.level.chunk.ChunkStatus.FULL, false);
                    if (chunk == null || chunk.isEmpty()) continue;
                    chunks++;

                    if (true) {
                        buildChunkFull(level, chunk, out, dispatcher, waterU0, waterV0, waterU1, waterV1);
                    } else {
                        int step = 1;
                        buildChunkLod(level, chunk, out, step,
                                waterU0, waterV0, waterU1, waterV1,
                                dispatcher, lodCache, lodRandom);
                    }
                }
            }
        }

        float[] verts = new float[out.vertexFloatCount()];
        System.arraycopy(out.vertices(), 0, verts, 0, verts.length);
        int[] idx = new int[out.indexCount()];
        System.arraycopy(out.indices(), 0, idx, 0, idx.length);
        return new Mesh(verts, idx, out.quadCount(), chunks, radius);
    }

    private static void buildChunkFull(ClientLevel level, LevelChunk chunk, SceneBuffers out,
                                       BlockRenderDispatcher dispatcher,
                                       float waterU0, float waterV0, float waterU1, float waterV1) {
        LevelChunkSection[] sections = chunk.getSections();
        int minSection = level.getMinSection();
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        BlockPos.MutableBlockPos neighbor = new BlockPos.MutableBlockPos();
        int baseX = chunk.getPos().getMinBlockX();
        int baseZ = chunk.getPos().getMinBlockZ();
        RandomSource random = RandomSource.create(42L);

        for (int si = 0; si < sections.length && !out.full(); si++) {
            LevelChunkSection section = sections[si];
            if (section == null || section.hasOnlyAir()) continue;
            int baseY = (minSection + si) << 4;

            for (int y = 0; y < 16 && !out.full(); y++) {
                for (int z = 0; z < 16 && !out.full(); z++) {
                    for (int x = 0; x < 16 && !out.full(); x++) {
                        BlockState state = section.getBlockState(x, y, z);
                        if (state.isAir()) continue;

                        int wx = baseX + x;
                        int wy = baseY + y;
                        int wz = baseZ + z;
                        pos.set(wx, wy, wz);

                        FluidState fluid = state.getFluidState();
                        boolean pureWater = state.getBlock() == Blocks.WATER;
                        boolean waterlogged = !fluid.isEmpty() && fluid.is(FluidTags.WATER);
                        if (pureWater) {
                            addWaterFaces(level, pos, neighbor, out, waterU0, waterV0, waterU1, waterV1);
                            continue;
                        }
                        if (waterlogged) {
                            addWaterSurface(level, pos, neighbor, out, waterU0, waterV0, waterU1, waterV1);
                        }

                        Block portalBlock = state.getBlock();
                        if (portalBlock == Blocks.END_PORTAL || portalBlock == Blocks.END_GATEWAY) {
                            float py = wy + 0.75f;
                            out.quad(wx, py, wz, wx, py, wz + 1.0f, wx + 1.0f, py, wz + 1.0f, wx + 1.0f, py, wz,
                                    0, 1, 0, 0.05f, 0.02f, 0.12f,
                                    0, 0, 0, 0, 0, 0, 0, 0, 13, 1.0f, 0.0f, 1.0f, 1.0f);
                            continue;
                        }
                        if (portalBlock == Blocks.NETHER_PORTAL) {
                            float px0 = wx;
                            float py0 = wy;
                            float pz0 = wz;
                            float px1 = wx + 1.0f;
                            float py1 = wy + 1.0f;
                            float pz1 = wz + 1.0f;
                            out.quad(px0, py0, pz0, px1, py0, pz0, px1, py1, pz0, px0, py1, pz0,
                                    0, 0, -1, 0.45f, 0.08f, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0, 14, 1.0f, 0.0f, 1.0f, 1.0f);
                            out.quad(px0, py0, pz1, px1, py0, pz1, px1, py1, pz1, px0, py1, pz1,
                                    0, 0, 1, 0.45f, 0.08f, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0, 14, 1.0f, 0.0f, 1.0f, 1.0f);
                            out.quad(px0, py0, pz0, px0, py0, pz1, px0, py1, pz1, px0, py1, pz0,
                                    -1, 0, 0, 0.45f, 0.08f, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0, 14, 1.0f, 0.0f, 1.0f, 1.0f);
                            out.quad(px1, py0, pz0, px1, py0, pz1, px1, py1, pz1, px1, py1, pz0,
                                    1, 0, 0, 0.45f, 0.08f, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0, 14, 1.0f, 0.0f, 1.0f, 1.0f);
                            continue;
                        }
                        if (state.getBlock() == Blocks.LAVA) {
                            addLavaFaces(level, pos, neighbor, out, waterU0, waterV0, waterU1, waterV1);
                            continue;
                        }

                        int material = detectMaterial(state);
                        BakedModel model;
                        try {
                            model = dispatcher.getBlockModelShaper().getBlockModel(state);
                        } catch (Throwable t) {
                            continue;
                        }
                        if (model == null) continue;

                        boolean cullSame = isCullableSameBlock(state);
                        for (Direction dir : Direction.values()) {
                            neighbor.set(pos).move(dir);
                            if (cullSame) {
                                try {
                                    if (level.getBlockState(neighbor).getBlock() == state.getBlock()) continue;
                                } catch (Throwable ignored) {
                                }
                            }
                            if (isHidden(level, pos, neighbor, dir)) continue;
                            java.util.List<BakedQuad> quads;
                            try {
                                quads = model.getQuads(state, dir, random);
                            } catch (Throwable t) {
                                continue;
                            }
                            for (BakedQuad q : quads) {
                                appendQuad(out, wx, wy, wz, q, material, state, level, pos);
                            }
                        }

                        java.util.List<BakedQuad> general;
                        try {
                            general = model.getQuads(state, null, random);
                        } catch (Throwable t) {
                            continue;
                        }
                        for (BakedQuad q : general) {
                            appendQuad(out, wx, wy, wz, q, material, state, level, pos);
                        }
                    }
                }
            }
        }
    }

    private static void buildChunkLod(ClientLevel level, LevelChunk chunk, SceneBuffers out,
                                      int step,
                                      float waterU0, float waterV0, float waterU1, float waterV1,
                                      BlockRenderDispatcher dispatcher,
                                      Map<BlockState, LodFace> cache,
                                      RandomSource random) {
        int baseX = chunk.getPos().getMinBlockX();
        int baseZ = chunk.getPos().getMinBlockZ();
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();

        for (int z = 0; z < 16; z += step) {
            for (int x = 0; x < 16; x += step) {
                int wx = baseX + x;
                int wz = baseZ + z;
                int height = heightAt(level, wx, wz);
                if (height <= level.getMinBuildHeight()) continue;
                pos.set(wx, height - 1, wz);
                BlockState state;
                try {
                    state = level.getBlockState(pos);
                } catch (Throwable t) {
                    continue;
                }
                if (state.isAir()) continue;

                boolean water = state.getBlock() == Blocks.WATER;
                boolean leaf = state.is(net.minecraft.tags.BlockTags.LEAVES);
                LodFace face = water ? null : cache.computeIfAbsent(
                        state, s -> lodFace(s, dispatcher, random));
                float r = 1.0f;
                float g = 1.0f;
                float b = 1.0f;
                float[] uv;
                int material;

                if (water) {
                    uv = new float[]{waterU0, waterV0, waterU1, waterV0, waterU1, waterV1, waterU0, waterV1};
                    r = 0.25f;
                    g = 0.55f;
                    b = 0.95f;
                    material = MAT_WATER;
                } else {
                    if (face == null) continue;
                    uv = face.uv;
                    material = leaf ? MAT_FOLIAGE : detectMaterial(state);
                    int tintIndex = face.tintIndex >= 0 ? face.tintIndex : (leaf ? 0 : -1);
                    if (tintIndex >= 0) {
                        int tint = -1;
                        try {
                            tint = Minecraft.getInstance().getBlockColors().getColor(state, level, pos, tintIndex);
                        } catch (Throwable ignored) {
                        }
                        if (tint != -1) {
                            r = ((tint >> 16) & 0xFF) / 255.0f;
                            g = ((tint >> 8) & 0xFF) / 255.0f;
                            b = (tint & 0xFF) / 255.0f;
                        }
                    }
                }

                float[] northUv = face != null ? face.sideUv(Direction.NORTH) : uv;
                float[] southUv = face != null ? face.sideUv(Direction.SOUTH) : uv;
                float[] westUv = face != null ? face.sideUv(Direction.WEST) : uv;
                float[] eastUv = face != null ? face.sideUv(Direction.EAST) : uv;
                float y = height;
                float x1 = wx + step;
                float z1 = wz + step;
                out.quad(wx, y, wz, x1, y, wz, x1, y, z1, wx, y, z1,
                        0.0f, 1.0f, 0.0f, r, g, b,
                        uv[0], uv[1], uv[2], uv[3], uv[4], uv[5], uv[6], uv[7],
                        material);

                if (!water) {
                    int north = heightAt(level, wx, wz - step);
                    int south = heightAt(level, wx, wz + step);
                    int west = heightAt(level, wx - step, wz);
                    int east = heightAt(level, wx + step, wz);
                    float sr = r * 0.72f;
                    float sg = g * 0.72f;
                    float sb = b * 0.72f;

                    if (height > north) {
                        out.quad(wx, north, wz, x1, north, wz, x1, y, wz, wx, y, wz,
                                0.0f, 0.0f, -1.0f, sr, sg, sb,
                                northUv[0], northUv[1], northUv[2], northUv[3],
                                northUv[4], northUv[5], northUv[6], northUv[7],
                                material);
                    }
                    if (height > south) {
                        out.quad(wx, south, z1, x1, south, z1, x1, y, z1, wx, y, z1,
                                0.0f, 0.0f, 1.0f, sr, sg, sb,
                                southUv[0], southUv[1], southUv[2], southUv[3],
                                southUv[4], southUv[5], southUv[6], southUv[7],
                                material);
                    }
                    if (height > west) {
                        out.quad(wx, west, wz, wx, west, z1, wx, y, z1, wx, y, wz,
                                -1.0f, 0.0f, 0.0f, sr, sg, sb,
                                westUv[0], westUv[1], westUv[2], westUv[3],
                                westUv[4], westUv[5], westUv[6], westUv[7],
                                material);
                    }
                    if (height > east) {
                        out.quad(x1, east, wz, x1, east, z1, x1, y, z1, x1, y, wz,
                                1.0f, 0.0f, 0.0f, sr, sg, sb,
                                eastUv[0], eastUv[1], eastUv[2], eastUv[3],
                                eastUv[4], eastUv[5], eastUv[6], eastUv[7],
                                material);
                    }
                }
            }
        }
    }

    private static LodFace lodFace(BlockState state, BlockRenderDispatcher dispatcher, RandomSource random) {
        try {
            BakedModel model = dispatcher.getBlockModelShaper().getBlockModel(state);
            if (model == null) return null;
            float[] top = null;
            int tintIndex = -1;
            java.util.List<BakedQuad> up = model.getQuads(state, Direction.UP, random);
            if (!up.isEmpty()) {
                BakedQuad q = up.get(0);
                top = mappedUv(q, TOP_CORNERS);
                tintIndex = q.getTintIndex();
            }
            if (top == null) {
                java.util.List<BakedQuad> general = model.getQuads(state, null, random);
                if (!general.isEmpty()) {
                    BakedQuad q = general.get(0);
                    top = quadUv(q);
                    tintIndex = q.getTintIndex();
                }
            }
            if (top == null) {
                TextureAtlasSprite icon = model.getParticleIcon();
                if (icon == null) return null;
                top = new float[]{
                        icon.getU0(), icon.getV1(), icon.getU1(), icon.getV1(),
                        icon.getU1(), icon.getV0(), icon.getU0(), icon.getV0()
                };
            }
            float[] north = sideUv(state, model, random, Direction.NORTH, top);
            float[] south = sideUv(state, model, random, Direction.SOUTH, top);
            float[] west = sideUv(state, model, random, Direction.WEST, top);
            float[] east = sideUv(state, model, random, Direction.EAST, top);
            return new LodFace(top, north, south, west, east, tintIndex);
        } catch (Throwable t) {
            return null;
        }
    }

    private static float[] sideUv(BlockState state, BakedModel model, RandomSource random,
                                  Direction direction, float[] fallback) {
        java.util.List<BakedQuad> quads = model.getQuads(state, direction, random);
        if (quads.isEmpty()) return fallback;
        float[] mapped = mappedUv(quads.get(0), sideCorners(direction));
        return mapped != null ? mapped : fallback;
    }

    private static float[] sideCorners(Direction direction) {
        return switch (direction) {
            case NORTH -> NORTH_CORNERS;
            case SOUTH -> SOUTH_CORNERS;
            case WEST -> WEST_CORNERS;
            case EAST -> EAST_CORNERS;
            default -> TOP_CORNERS;
        };
    }


    private static float[] mappedUv(BakedQuad quad, float[] target) {
        int[] data = quad.getVertices();
        if (data == null || data.length < 32) return null;
        float[] uv = new float[8];
        for (int t = 0; t < 4; t++) {
            float tx = target[t * 3];
            float ty = target[t * 3 + 1];
            float tz = target[t * 3 + 2];
            int best = -1;
            float bestDistance = Float.MAX_VALUE;
            for (int i = 0; i < 4; i++) {
                float dx = Float.intBitsToFloat(data[i * 8]) - tx;
                float dy = Float.intBitsToFloat(data[i * 8 + 1]) - ty;
                float dz = Float.intBitsToFloat(data[i * 8 + 2]) - tz;
                float distance = dx * dx + dy * dy + dz * dz;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }
            if (best < 0) return null;
            uv[t * 2] = Float.intBitsToFloat(data[best * 8 + 4]);
            uv[t * 2 + 1] = Float.intBitsToFloat(data[best * 8 + 5]);
        }
        return uv;
    }

    private static float[] quadUv(BakedQuad quad) {
        int[] data = quad.getVertices();
        if (data == null || data.length < 32) return null;
        float[] uv = new float[8];
        for (int i = 0; i < 4; i++) {
            uv[i * 2] = Float.intBitsToFloat(data[i * 8 + 4]);
            uv[i * 2 + 1] = Float.intBitsToFloat(data[i * 8 + 5]);
        }
        return uv;
    }

    private static final class LodFace {
        final float[] uv;
        final float[] northUv;
        final float[] southUv;
        final float[] westUv;
        final float[] eastUv;
        final int tintIndex;

        LodFace(float[] uv, float[] northUv, float[] southUv, float[] westUv, float[] eastUv, int tintIndex) {
            this.uv = uv;
            this.northUv = northUv;
            this.southUv = southUv;
            this.westUv = westUv;
            this.eastUv = eastUv;
            this.tintIndex = tintIndex;
        }

        float[] sideUv(Direction direction) {
            return switch (direction) {
                case NORTH -> northUv;
                case SOUTH -> southUv;
                case WEST -> westUv;
                case EAST -> eastUv;
                default -> uv;
            };
        }
    }

    private static int heightAt(ClientLevel level, int x, int z) {
        try {
            return level.getHeight(Heightmap.Types.MOTION_BLOCKING, x, z);
        } catch (Throwable t) {
            return level.getMinBuildHeight();
        }
    }

    private static boolean isFoliage(BlockState state) {
        Block block = state.getBlock();
        return FOLIAGE_CACHE.computeIfAbsent(block, b -> {
            try {
                String path = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(b).getPath();
                return path.contains("leaves") || path.contains("grass") && !path.contains("grass_block")
                        || path.contains("fern") || path.contains("flower") || path.contains("vine")
                        || path.contains("lily") || path.contains("bush") || path.contains("azalea");
            } catch (Throwable t) {
                return false;
            }
        });
    }

    private static boolean isCullableSameBlock(BlockState state) {
        Block block = state.getBlock();
        return CULL_SAME_CACHE.computeIfAbsent(block, b -> {
            try {
                String path = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(b).getPath();
                return path.contains("leaves") || path.contains("glass") || path.contains("pane")
                        || path.contains("ice");
            } catch (Throwable t) {
                return false;
            }
        });
    }

    private static boolean isHidden(ClientLevel level, BlockPos pos, BlockPos.MutableBlockPos neighbor,
                                    Direction dir) {
        neighbor.set(pos).move(dir);
        try {
            BlockState ns = level.getBlockState(neighbor);
            return ns.canOcclude() && ns.isSolidRender(level, neighbor);
        } catch (Throwable t) {
            return false;
        }
    }

    private static boolean isWater(ClientLevel level, BlockPos pos) {
        BlockState state = level.getBlockState(pos);
        if (state.getBlock() == Blocks.WATER) return true;
        FluidState fluid = state.getFluidState();
        return !fluid.isEmpty() && fluid.is(FluidTags.WATER);
    }

    private static void addWaterSurface(ClientLevel level, BlockPos pos, BlockPos.MutableBlockPos neighbor,
                                        SceneBuffers out,
                                        float u0, float v0, float u1, float v1) {
        neighbor.set(pos.getX(), pos.getY() + 1, pos.getZ());
        if (isWater(level, neighbor)) return;
        BlockState above = level.getBlockState(neighbor);
        if (above.canOcclude() && above.isSolidRender(level, neighbor)) return;

        int x = pos.getX();
        int y = pos.getY();
        int z = pos.getZ();
        out.quad(x, y + 1.0f, z, x + 1.0f, y + 1.0f, z,
                x + 1.0f, y + 1.0f, z + 1.0f, x, y + 1.0f, z + 1.0f,
                0, 1, 0, 0.25f, 0.55f, 0.95f,
                u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
    }

    private static void addLavaFaces(ClientLevel level, BlockPos pos, BlockPos.MutableBlockPos neighbor,
                                     SceneBuffers out, float u0, float v0, float u1, float v1) {
        int x = pos.getX();
        int y = pos.getY();
        int z = pos.getZ();
        for (Direction dir : Direction.values()) {
            neighbor.set(pos).move(dir);
            BlockState ns = level.getBlockState(neighbor);
            if (ns.getBlock() == Blocks.LAVA) continue;
            if (ns.canOcclude() && ns.isSolidRender(level, neighbor)) continue;
            float x0 = x;
            float y0 = y;
            float z0 = z;
            float x1 = x + 1.0f;
            float y1 = y + 1.0f;
            float z1 = z + 1.0f;
            switch (dir) {
                case DOWN -> out.quad(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1,
                        0, -1, 0, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
                case UP -> out.quad(x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1,
                        0, 1, 0, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
                case NORTH -> out.quad(x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0,
                        0, 0, -1, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
                case SOUTH -> out.quad(x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1,
                        0, 0, 1, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
                case WEST -> out.quad(x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0,
                        -1, 0, 0, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
                case EAST -> out.quad(x1, y0, z0, x1, y0, z1, x1, y1, z1, x1, y1, z0,
                        1, 0, 0, 0.95f, 0.42f, 0.08f, u0, v0, u1, v0, u1, v1, u0, v1, MAT_LAVA, 0.85f, 0.0f, 1.0f, 0.0f);
            }
        }
    }

    private static void addWaterFaces(ClientLevel level, BlockPos pos, BlockPos.MutableBlockPos neighbor,
                                      SceneBuffers out,
                                      float u0, float v0, float u1, float v1) {
        int x = pos.getX();
        int y = pos.getY();
        int z = pos.getZ();

        for (Direction dir : Direction.values()) {
            neighbor.set(pos).move(dir);
            if (isWater(level, neighbor)) continue;
            BlockState ns = level.getBlockState(neighbor);
            if (ns.canOcclude() && ns.isSolidRender(level, neighbor)) continue;

            float x0 = x;
            float y0 = y;
            float z0 = z;
            float x1 = x + 1.0f;
            float y1 = y + 1.0f;
            float z1 = z + 1.0f;

            switch (dir) {
                case DOWN -> out.quad(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1,
                        0, -1, 0, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
                case UP -> out.quad(x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1,
                        0, 1, 0, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
                case NORTH -> out.quad(x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0,
                        0, 0, -1, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
                case SOUTH -> out.quad(x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1,
                        0, 0, 1, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
                case WEST -> out.quad(x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0,
                        -1, 0, 0, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
                case EAST -> out.quad(x1, y0, z0, x1, y0, z1, x1, y1, z1, x1, y1, z0,
                        1, 0, 0, 0.25f, 0.55f, 0.95f,
                        u0, v0, u1, v0, u1, v1, u0, v1, MAT_WATER);
            }
        }
    }

    public static int detectMaterial(BlockState state) {
        var block = state.getBlock();
        if (block == Blocks.WATER) return MAT_WATER;

        ResourceLocation id = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block);
        if (id == null) return MAT_DIFFUSE;
        String path = id.getPath();

        if (path.equals("nether_portal") || path.equals("end_portal")) return MAT_PORTAL;
        if (path.equals("fire") || path.equals("soul_fire")) return MAT_FIRE;
        if (path.contains("campfire") || path.contains("torch")) return MAT_DIFFUSE;
        if (path.equals("lava") || path.contains("magma")) return MAT_LAVA;

        if (path.contains("amethyst") || path.contains("diamond_block") || path.contains("emerald_block")) return MAT_GEM;

        if (path.contains("beacon")) return MAT_EMISSIVE;
        if (path.contains("glass") || path.contains("pane") || path.contains("ice")) return MAT_GLASS;

        if (path.contains("gold_block") || path.contains("iron_block")
                || path.contains("copper_block") || path.contains("netherite_block")
                || path.contains("anvil") || path.contains("cauldron")
                || path.contains("iron_bars") || path.contains("chain")
                || path.contains("lodestone") || path.contains("raw_")
                || path.contains("exposed_copper") || path.contains("weathered_copper")
                || path.contains("oxidized_copper") || path.contains("waxed_")) return MAT_METAL;

        try {
            if (state.getLightEmission() > 0) return MAT_EMISSIVE;
        } catch (Throwable ignored) {
        }
        return MAT_DIFFUSE;
    }

    private static void appendQuad(SceneBuffers out, int bx, int by, int bz, BakedQuad q,
                                   int material, BlockState state, ClientLevel level, BlockPos pos) {
        int[] data = q.getVertices();
        if (data == null || data.length < 32) return;

        float tr = 1.0f;
        float tg = 1.0f;
        float tb = 1.0f;
        int tintIdx = q.getTintIndex();
        if (tintIdx >= 0) {
            try {
                int tint = Minecraft.getInstance().getBlockColors().getColor(state, level, pos, tintIdx);
                if (tint != -1) {
                    tr = ((tint >> 16) & 0xFF) / 255.0f;
                    tg = ((tint >> 8) & 0xFF) / 255.0f;
                    tb = (tint & 0xFF) / 255.0f;
                }
            } catch (Throwable ignored) {
            }
        }

        float[] px = new float[4];
        float[] py = new float[4];
        float[] pz = new float[4];
        float[] uu = new float[4];
        float[] vv = new float[4];

        for (int i = 0; i < 4; i++) {
            int base = i * 8;
            px[i] = Float.intBitsToFloat(data[base]) + bx;
            py[i] = Float.intBitsToFloat(data[base + 1]) + by;
            pz[i] = Float.intBitsToFloat(data[base + 2]) + bz;
            uu[i] = Float.intBitsToFloat(data[base + 4]);
            vv[i] = Float.intBitsToFloat(data[base + 5]);
        }

        net.minecraft.client.renderer.texture.TextureAtlasSprite quadSprite = q.getSprite();
        if (material == MAT_CAMPFIRE && quadSprite != null) {
            String firePath = "";
            try {
                firePath = quadSprite.contents().name().getPath();
            } catch (Throwable ignored) {
            }
            if (firePath.contains("fire") || firePath.contains("flame")) material = MAT_FIRE;
        }
        if (material == 7 && UV_DEBUG_COUNT < 40) {
            UV_DEBUG_COUNT++;
            String sname = "null";
            try {
                sname = quadSprite.contents().name().toString();
            } catch (Throwable ignored) {
            }
            System.out.println("[UVD] " + net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(state.getBlock())
                    + " sprite=" + sname
                    + " u=" + (quadSprite == null ? -1 : quadSprite.getU0()) + ".."
                    + (quadSprite == null ? -1 : quadSprite.getU1())
                    + " v=" + (quadSprite == null ? -1 : quadSprite.getV0()) + ".."
                    + (quadSprite == null ? -1 : quadSprite.getV1())
                    + " raw u=" + uu[0] + " v=" + vv[0]);
        }

        float ax = px[1] - px[0];
        float ay = py[1] - py[0];
        float az = pz[1] - pz[0];
        float qx = px[2] - px[0];
        float qy = py[2] - py[0];
        float qz = pz[2] - pz[0];
        float nx = ay * qz - az * qy;
        float ny = az * qx - ax * qz;
        float nz = ax * qy - ay * qx;
        float len = (float) Math.sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1.0e-6f) len = 1.0f;
        nx /= len;
        ny /= len;
        nz /= len;

        float[] pbr = pbrFor(state, material);
        float skyLight = 1.0f;
        float blockLight = 0.0f;
        float alphaTest = alphaTestFor(state);
            if (alphaTest > 0.5f && alphaTest < 1.5f && material != MAT_FOLIAGE) alphaTest = 2.0f;
        if (material >= MAT_EMISSIVE && material <= MAT_CAMPFIRE) alphaTest = alphaTestFor(state) > 0.5f ? 1.0f : 0.0f;
        if (alphaTest > 0.5f && material != MAT_FOLIAGE) {
            nx = 0.0f;
            ny = 1.0f;
            nz = 0.0f;
        }
        try {
            if (level != null && pos != null) {
                skyLight = level.getBrightness(LightLayer.SKY, pos) / 15.0f;
                blockLight = level.getBrightness(LightLayer.BLOCK, pos) / 15.0f;
                if (!level.dimensionType().hasSkyLight()) skyLight = 0.0f;
            }
        } catch (Throwable ignored) {
        }
        out.quad(px[0], py[0], pz[0], px[1], py[1], pz[1],
                px[2], py[2], pz[2], px[3], py[3], pz[3],
                nx, ny, nz, tr, tg, tb,
                uu[0], vv[0], uu[1], vv[1], uu[2], vv[2], uu[3], vv[3],
                material, pbr[0], pbr[1], pbr[2], pbr[3], skyLight, blockLight, alphaTest);
    }

    private static final Map<BlockState, Float> ALPHA_TEST_CACHE = new ConcurrentHashMap<>();


    private static float[] FIRE_RECT;

    private static float alphaTestFor(BlockState state) {
        return ALPHA_TEST_CACHE.computeIfAbsent(state, s -> {
            try {
                net.minecraft.client.renderer.RenderType type =
                        net.minecraft.client.renderer.ItemBlockRenderTypes.getChunkRenderType(s);
                if (type == net.minecraft.client.renderer.RenderType.cutoutMipped()) {
                    return 1.0f;
                }
                if (type == net.minecraft.client.renderer.RenderType.cutout()) {
                    return 2.0f;
                }
            } catch (Throwable ignored) {
            }
            return 0.0f;
        });
    }

    private static float[] pbrFor(BlockState state, int material) {
        float roughness = SceneBuffers.defaultRoughness(material);
        float metalness = SceneBuffers.defaultMetalness(material);
        float emission = SceneBuffers.defaultEmission(material);
        float translucency = SceneBuffers.defaultTranslucency(material);
        ResourceLocation id = BuiltInRegistries.BLOCK.getKey(state.getBlock());
        String path = id == null ? "" : id.getPath();
        if (material == MAT_GEM) {
            roughness = path.contains("amethyst") ? 0.10f : 0.07f;
            metalness = 0.0f;
            translucency = 0.9f;
        } else if (path.contains("ice")) {
            roughness = 0.05f;
            metalness = 0.0f;
            translucency = 0.7f;
        } else if (path.contains("obsidian")) {
            roughness = 0.40f;
        } else if (path.contains("amethyst")) {
            roughness = 0.04f;
            translucency = 0.8f;
        } else if (path.contains("snow")) {
            roughness = 0.62f;
        } else if (path.contains("glass")) {
            roughness = 0.07f;
            translucency = 1.0f;
        } else if (path.contains("polished") || path.contains("smooth_")
                || path.contains("quartz") || path.contains("tiles")) {
            roughness = 0.38f;
        } else if (path.contains("gold_block")) {
            roughness = 0.42f;
            metalness = 0.60f;
        } else if (path.contains("copper") || path.contains("iron_block")
                || path.contains("netherite")) {
            roughness = 0.46f;
            metalness = 0.62f;
        } else if (path.contains("diamond") || path.contains("emerald")) {
            roughness = 0.15f;
        } else if (path.contains("water")) {
            roughness = 0.01f;
            translucency = 1.0f;
        } else if (path.contains("leaves") || path.contains("vine")
                || path.contains("moss") || path.contains("grass")) {
            roughness = 0.72f;
            translucency = 0.28f;
        } else if (path.contains("glowstone") || path.contains("shroomlight")
                || path.contains("lamp") || path.contains("sea_lantern")) {
            emission = 2.2f;
            roughness = 0.55f;
        } else if (path.contains("deepslate") || path.contains("basalt")
                || path.contains("cobble")) {
            roughness = 0.92f;
        }
        return new float[]{roughness, metalness, emission, translucency};
    }

    private SceneMesher() {
    }
}
