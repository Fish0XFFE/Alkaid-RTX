package com.fish.yaoguang.client;

import com.fish.yaoguang.nativebridge.YaoguangNative;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.block.BlockRenderDispatcher;
import net.minecraft.client.renderer.block.model.BakedQuad;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.resources.model.BakedModel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.tags.FluidTags;
import net.minecraft.util.RandomSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.inventory.InventoryMenu;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.Vec3;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL30;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class RayTracingRenderer {
    public static final RayTracingRenderer INSTANCE = new RayTracingRenderer();

    private static final long MIN_REBUILD_INTERVAL_MS = 5000;
    private static final long REBUILD_COOLDOWN_MS = 200;
    private static final double REBUILD_DISTANCE_SQ = 225.0;

    private static final int MAT_DIFFUSE  = 0;
    private static final int MAT_METAL    = 1;
    private static final int MAT_GLASS    = 2;
    private static final int MAT_WATER    = 3;
    private static final int MAT_EMISSIVE = 4;

    private static final int VERTEX_FLOATS = 12;
    private static final int MAX_ENTITIES  = 256;
    private static final int MAX_PARTICLES = 1024;

    private long lastRebuildMs = 0;
    private BlockPos lastPlayerPos = null;
    private volatile boolean sceneDirty = true;
    private volatile boolean sceneValid = false;
    private int rebuildCount = 0;
    private int frameCount = 0;

    private boolean blockAtlasUploaded = false;
    private int lastBlockAtlasId = -1;
    private boolean particleAtlasUploaded = false;
    private int lastParticleAtlasId = -1;

    private int entityCaptureLog = 0;
    private int particleCaptureLog = 0;

    private final ExecutorService rebuildExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "Yaoguang-SceneRebuild");
        t.setDaemon(true);
        t.setPriority(Thread.MIN_PRIORITY);
        return t;
    });

    private final AtomicBoolean rebuildInFlight = new AtomicBoolean(false);

    private volatile float[] pendingVerts = null;
    private volatile int[] pendingIndices = null;
    private volatile boolean pendingUpload = false;

    private final FloatBuffer entityBuffer = BufferUtils.createFloatBuffer(MAX_ENTITIES * 8);
    private final FloatBuffer particleBuffer = BufferUtils.createFloatBuffer(MAX_PARTICLES * 12);

    private final ConcurrentLinkedQueue<EntitySnapshot> entityQueue = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<ParticleSnapshot> particleQueue = new ConcurrentLinkedQueue<>();

    private int cachedRenderDistance = 32;
    private long lastRenderDistanceCheck = 0;

    private RayTracingRenderer() {}

    public void markSceneDirty() { sceneDirty = true; }

    private static class EntitySnapshot {
        final double x, y, z;
        final float width, height, colorR, colorG, colorB;
        final int material;

        EntitySnapshot(double x, double y, double z, float w, float h,
                       float r, float g, float b, int mat) {
            this.x = x; this.y = y; this.z = z;
            this.width = w; this.height = h;
            this.colorR = r; this.colorG = g; this.colorB = b;
            this.material = mat;
        }
    }

    private static class ParticleSnapshot {
        final double x, y, z;
        final float size, colorR, colorG, colorB, alpha, age, life;

        ParticleSnapshot(double x, double y, double z, float size,
                         float r, float g, float b, float a,
                         float age, float life) {
            this.x = x; this.y = y; this.z = z;
            this.size = size;
            this.colorR = r; this.colorG = g; this.colorB = b; this.alpha = a;
            this.age = age; this.life = life;
        }
    }

    public void onEntityRendered(Entity entity,
                                 com.mojang.blaze3d.vertex.PoseStack poseStack) {
        if (entityQueue.size() > 2048) return;

        try {
            if (entityCaptureLog < 5) {
                System.out.println("[Yaoguang] onEntityRendered called: " + entity.getType());
                entityCaptureLog++;
            }

            var pose = poseStack.last().pose();
            org.joml.Vector3f pos = pose.transformPosition(new org.joml.Vector3f(0, 0, 0));

            var bb = entity.getBoundingBox();
            float w = (float) Math.max(bb.getXsize(), bb.getZsize());
            float h = (float) bb.getYsize();

            int c = entityColor(entity);
            float r = ((c >> 16) & 0xFF) / 255f;
            float g = ((c >> 8) & 0xFF) / 255f;
            float b = (c & 0xFF) / 255f;

            entityQueue.add(new EntitySnapshot(pos.x, pos.y, pos.z, w, h,
                    r, g, b, entityMaterial(entity)));
        } catch (Throwable ignored) {}
    }

    public void onParticleRendered(double x, double y, double z, float size,
                                   float r, float g, float b, float a,
                                   float age, float life) {
        if (particleQueue.size() > 8192) return;
        if (particleCaptureLog < 5) {
            System.out.println("[Yaoguang] onParticleRendered called");
            particleCaptureLog++;
        }
        particleQueue.add(new ParticleSnapshot(x, y, z, size, r, g, b, a, age, life));
    }

    private int getRenderDistance() {
        try {
            long now = System.currentTimeMillis();
            if (now - lastRenderDistanceCheck > 500) {
                lastRenderDistanceCheck = now;
                cachedRenderDistance = 32;
            }
        } catch (Throwable ignored) {}
        return cachedRenderDistance;
    }

    private void uploadAtlas(int texId, String name, boolean blockAtlas) {
        int prevFbo = GL11.glGetInteger(GL30.GL_FRAMEBUFFER_BINDING);
        int prevTex = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, texId);
        int w = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);
        int h = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_HEIGHT);
        if (w <= 0 || h <= 0) {
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTex);
            return;
        }

        int fbo = GL30.glGenFramebuffers();
        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, fbo);
        GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                GL11.GL_TEXTURE_2D, texId, 0);

        if (GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER) != GL30.GL_FRAMEBUFFER_COMPLETE) {
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, prevFbo);
            GL30.glDeleteFramebuffers(fbo);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTex);
            return;
        }

        ByteBuffer buf = BufferUtils.createByteBuffer(w * h * 4);
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 4);
        GL11.glReadPixels(0, 0, w, h, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, buf);

        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, prevFbo);
        GL30.glDeleteFramebuffers(fbo);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTex);

        if (blockAtlas) {
            YaoguangNative.uploadBlockAtlas(w, h, buf);
        } else {
            YaoguangNative.uploadParticleAtlas(w, h, buf);
        }
        System.out.println("[Yaoguang] " + name + " uploaded: " + w + "x" + h);
    }

    private void ensureBlockAtlasUploaded() {
        try {
            var tex = Minecraft.getInstance().getTextureManager()
                    .getTexture(InventoryMenu.BLOCK_ATLAS);
            if (tex == null) return;
            int id = tex.getId();
            if (id == lastBlockAtlasId && blockAtlasUploaded) return;
            uploadAtlas(id, "Block atlas", true);
            blockAtlasUploaded = true;
            lastBlockAtlasId = id;
        } catch (Throwable t) {
            System.out.println("[Yaoguang] Block atlas upload failed: " + t);
        }
    }

    private void ensureParticleAtlasUploaded() {
    }

    private int entityColor(Entity e) {
        try {
            String id = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE
                    .getKey(e.getType()).getPath();
            if (id.contains("zombie") || id.contains("husk") || id.contains("drowned")) return 0x4A7C3A;
            if (id.contains("skeleton") || id.contains("stray")) return 0xD0D0D0;
            if (id.contains("creeper")) return 0x50B050;
            if (id.contains("cow") || id.contains("mooshroom")) return 0x5A3A20;
            if (id.contains("sheep")) return 0xE0E0D0;
            if (id.contains("pig")) return 0xE8A0A0;
            if (id.contains("chicken")) return 0xE8E8E0;
            if (id.contains("player")) return 0x4A4A8A;
            if (id.contains("villager") || id.contains("wandering")) return 0xA08060;
            if (id.contains("wolf") || id.contains("cat") || id.contains("fox")) return 0xC0A080;
            if (id.contains("horse") || id.contains("donkey")) return 0x8A5A30;
            if (id.contains("spider")) return 0x2A1A1A;
            if (id.contains("enderman")) return 0x101018;
            if (id.contains("slime") || id.contains("magma")) return 0x70C070;
            if (id.contains("ghast") || id.contains("blaze")) return 0xF0E0C0;
            if (id.contains("witch")) return 0x504050;
            if (id.contains("arrow") || id.contains("trident")) return 0xA08050;
            if (id.contains("item")) return 0xC0C0C0;
        } catch (Throwable ignored) {}
        return 0xA0A0A0;
    }

    private int entityMaterial(Entity e) {
        try {
            String id = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE
                    .getKey(e.getType()).getPath();
            if (id.contains("blaze") || id.contains("magma") || id.contains("fire")) return MAT_EMISSIVE;
            if (id.contains("slime")) return MAT_GLASS;
        } catch (Throwable ignored) {}
        return MAT_DIFFUSE;
    }

    private void uploadEntities(int count) {
        try {
            YaoguangNative.uploadDynamic(entityBuffer, count);
        } catch (Throwable ignored) {}
    }

    private void uploadParticles(int count) {
        try {
            if (count <= 0) { YaoguangNative.clearParticles(); return; }
            YaoguangNative.uploadParticles(particleBuffer, count);
        } catch (Throwable ignored) {}
    }

    public void beginFrame() {
        CompositeRenderer.tickShutdown();

        Minecraft mc = Minecraft.getInstance();
        if (mc == null) return;
        if (mc.level == null || mc.player == null || mc.gameRenderer == null) return;

        frameCount++;

        try {
            long handle = mc.getWindow().getWindow();
            int winW = mc.getWindow().getWidth();
            int winH = mc.getWindow().getHeight();
            if (handle != 0L && winW > 0 && winH > 0) {
                YaoguangNative.onFrameBegin(handle, winW, winH);
            }
        } catch (Throwable t) {
            if (frameCount < 5) System.out.println("[Yaoguang] onFrameBegin failed: " + t);
        }

        if (!blockAtlasUploaded) ensureBlockAtlasUploaded();
        if (!particleAtlasUploaded) ensureParticleAtlasUploaded();

        if (pendingUpload) {
            float[] v = pendingVerts;
            int[] i = pendingIndices;
            pendingVerts = null;
            pendingIndices = null;
            pendingUpload = false;
            if (v != null && i != null) {
                try {
                    YaoguangNative.uploadSceneGeometry(v, i);
                    sceneValid = true;
                    sceneDirty = false;
                    rebuildCount++;
                    CompositeRenderer.resetAccum();
                    System.out.println("[Yaoguang] Scene rebuilt #" + rebuildCount
                            + " verts=" + (v.length / VERTEX_FLOATS) + " indices=" + i.length);
                } catch (Throwable t) {
                    System.out.println("[Yaoguang] uploadSceneGeometry failed: " + t);
                }
            }
        }

        BlockPos playerPos = mc.player.blockPosition();
        long now = System.currentTimeMillis();

        boolean needRebuild = sceneDirty || !sceneValid;
        if (!needRebuild && now - lastRebuildMs >= MIN_REBUILD_INTERVAL_MS) {
            if (lastPlayerPos == null || lastPlayerPos.distSqr(playerPos) > REBUILD_DISTANCE_SQ) {
                needRebuild = true;
            }
        }

        if (needRebuild && now - lastRebuildMs < REBUILD_COOLDOWN_MS) {
            needRebuild = false;
        }

        if (needRebuild && !rebuildInFlight.get() && !pendingUpload) {
            rebuildInFlight.set(true);
            lastRebuildMs = now;
            lastPlayerPos = playerPos.immutable();
            sceneDirty = false;
            final BlockPos center = playerPos.immutable();
            final int radius = getRenderDistance();
            rebuildExecutor.submit(() -> {
                try {
                    rebuildSceneAsync(center, radius);
                } catch (Throwable t) {
                    System.out.println("[Yaoguang] rebuildSceneAsync failed: " + t);
                } finally {
                    rebuildInFlight.set(false);
                }
            });
        }

        if (!sceneValid) return;

        Vec3 camPos = mc.gameRenderer.getMainCamera().getPosition();
        float yaw = mc.gameRenderer.getMainCamera().getYRot();
        float pitch = mc.gameRenderer.getMainCamera().getXRot();

        try {
            CompositeRenderer.notifyCamera(camPos.x, camPos.y, camPos.z, yaw, pitch);
        } catch (Throwable ignored) {}

        entityBuffer.clear();
        int entCount = 0;
        double maxDist = 64.0 * 64.0;
        EntitySnapshot snap;
        while ((snap = entityQueue.poll()) != null && entCount < MAX_ENTITIES) {
            double dx = snap.x - camPos.x;
            double dy = snap.y - camPos.y;
            double dz = snap.z - camPos.z;
            if (dx * dx + dy * dy + dz * dz > maxDist) continue;
            entityBuffer.put((float) snap.x);
            entityBuffer.put((float) snap.y);
            entityBuffer.put((float) snap.z);
            entityBuffer.put(Math.max(snap.width, snap.height) * 0.5f);
            entityBuffer.put(snap.colorR);
            entityBuffer.put(snap.colorG);
            entityBuffer.put(snap.colorB);
            entityBuffer.put((float) snap.material);
            entCount++;
        }
        entityBuffer.flip();
        uploadEntities(entCount);

        if (frameCount <= 60 && entCount > 0)
            System.out.println("[Yaoguang] entities sent: " + entCount);

        particleBuffer.clear();
        int partCount = 0;
        ParticleSnapshot psnap;
        while ((psnap = particleQueue.poll()) != null && partCount < MAX_PARTICLES) {
            double dx = psnap.x - camPos.x;
            double dy = psnap.y - camPos.y;
            double dz = psnap.z - camPos.z;
            if (dx * dx + dy * dy + dz * dz > maxDist) continue;

            float lifeFrac = psnap.life > 0.001f ? (psnap.age / psnap.life) : 0.0f;
            if (lifeFrac < 0.0f) lifeFrac = 0.0f;
            if (lifeFrac > 1.0f) lifeFrac = 1.0f;
            float alphaFade = 1.0f - lifeFrac;
            float a = psnap.alpha * alphaFade;

            particleBuffer.put((float) psnap.x);
            particleBuffer.put((float) psnap.y);
            particleBuffer.put((float) psnap.z);
            particleBuffer.put(Math.max(psnap.size, 0.02f) * 0.5f);
            particleBuffer.put(psnap.colorR);
            particleBuffer.put(psnap.colorG);
            particleBuffer.put(psnap.colorB);
            particleBuffer.put(a);
            particleBuffer.put(0.0f);
            particleBuffer.put(0.0f);
            particleBuffer.put(0.0f);
            particleBuffer.put((float) MAT_EMISSIVE);
            partCount++;
        }
        particleBuffer.flip();
        uploadParticles(partCount);

        if (frameCount <= 60 && partCount > 0)
            System.out.println("[Yaoguang] particles sent: " + partCount);

        try {
            long dayTime = mc.level.getDayTime();
            double phase = ((dayTime % 24000L) / 24000.0) * (2.0 * Math.PI);
            float sx = (float) Math.cos(phase);
            float sy = (float) Math.sin(phase);
            float sz = 0.18f;
            float len = (float) Math.sqrt(sx * sx + sy * sy + sz * sz);
            if (len < 1e-6f) len = 1.0f;
            sx /= len; sy /= len; sz /= len;

            float sunIntensity = (sy > 0.0f)
                    ? (2.5f + 4.5f * (float) Math.sin(phase))
                    : 0.8f;

            float rainLevel = 0f;
            try { rainLevel = mc.level.getRainLevel(1.0f); } catch (Throwable ignored) {}
            float thunderLevel = 0f;
            try { thunderLevel = mc.level.getThunderLevel(1.0f); } catch (Throwable ignored) {}

            sunIntensity *= (1.0f - rainLevel * 0.65f - thunderLevel * 0.30f);
            if (sunIntensity < 0.05f) sunIntensity = 0.05f;

            YaoguangNative.setSun(sx, sy, sz, sunIntensity);
            YaoguangNative.setWeather(rainLevel, thunderLevel);

            int rdChunks = mc.options.renderDistance().get();
            float rayLen = Math.max(64.0f, rdChunks * 16.0f);
            YaoguangNative.setRayLength(rayLen);

            if (frameCount <= 30)
                System.out.println("[Yaoguang] chunks=" + rdChunks + " rayLen=" + rayLen);
        } catch (Throwable ignored) {}

        double yawRad = Math.toRadians(yaw);
        double pitchRad = Math.toRadians(pitch);
        float dirX = (float)(-Math.sin(yawRad) * Math.cos(pitchRad));
        float dirY = (float)(-Math.sin(pitchRad));
        float dirZ = (float)( Math.cos(yawRad) * Math.cos(pitchRad));

        float fov = mc.options.fov().get();
        int w = mc.getWindow().getWidth();
        int h = mc.getWindow().getHeight();
        if (w <= 0 || h <= 0) return;
        float aspect = (float) w / (float) h;

        try {
            YaoguangNative.setCamera(
                    (float) camPos.x, (float) camPos.y, (float) camPos.z,
                    dirX, dirY, dirZ,
                    0.0f, 1.0f, 0.0f,
                    fov, aspect);
        } catch (Throwable t) {
            if (frameCount < 5) System.out.println("[Yaoguang] setCamera failed: " + t);
        }

        try {
            YaoguangNative.renderFrame();
        } catch (Throwable t) {
            System.out.println("[Yaoguang] renderFrame failed: " + t);
            t.printStackTrace();
        }
    }

    public void endFrame() {
        if (!sceneValid) return;
        try {
            long imageHandle = YaoguangNative.getSharedHandle();
            int w = YaoguangNative.getOutputWidth();
            int h = YaoguangNative.getOutputHeight();
            if (imageHandle != 0L && w > 0 && h > 0) {
                CompositeRenderer.importSharedResources(imageHandle, w, h);
                CompositeRenderer.render(w, h);
            }
        } catch (Throwable t) {
            System.out.println("[Yaoguang] endFrame error: " + t);
            t.printStackTrace();
        }
    }

    private int detectMaterial(BlockState state) {
        var block = state.getBlock();
        if (block == Blocks.WATER) return MAT_WATER;

        ResourceLocation rl = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block);
        if (rl == null) return MAT_DIFFUSE;
        String path = rl.getPath();

        if (path.contains("glass") || path.contains("pane") || path.contains("ice")
                || path.contains("amethyst") || path.contains("beacon")) return MAT_GLASS;

        if (path.contains("diamond_block") || path.contains("emerald_block")
                || path.contains("gold_block") || path.contains("iron_block")
                || path.contains("copper_block") || path.contains("netherite_block")
                || path.contains("anvil") || path.contains("cauldron")
                || path.contains("iron_bars") || path.contains("chain")
                || path.contains("lodestone") || path.contains("raw_")
                || path.contains("exposed_copper") || path.contains("weathered_copper")
                || path.contains("oxidized_copper") || path.contains("waxed_")) return MAT_METAL;

        try {
            if (state.getLightEmission() > 0) return MAT_EMISSIVE;
        } catch (Throwable ignored) {}
        return MAT_DIFFUSE;
    }

    private boolean isWaterState(BlockState s) {
        if (s == null) return false;
        if (s.getBlock() == Blocks.WATER) return true;
        try {
            FluidState fs = s.getFluidState();
            return fs != null && !fs.isEmpty() && fs.is(FluidTags.WATER);
        } catch (Throwable t) {
            return false;
        }
    }

    private void rebuildSceneAsync(BlockPos center, int radius) {
        Minecraft mc = Minecraft.getInstance();
        ClientLevel level = mc == null ? null : mc.level;
        if (level == null) return;

        BlockRenderDispatcher dispatcher = mc.getBlockRenderer();
        RandomSource rand = RandomSource.create(42L);

        List<Float> verts = new ArrayList<>(1 << 22);
        List<Integer> indices = new ArrayList<>(1 << 22);

        int cx = center.getX();
        int cy = center.getY();
        int cz = center.getZ();

        int vr = radius;
        int minY = Math.max(level.getMinBuildHeight(), cy - vr);
        int maxY = Math.min(level.getMaxBuildHeight() - 1, cy + vr);

        BlockPos.MutableBlockPos mpos = new BlockPos.MutableBlockPos();
        BlockPos.MutableBlockPos npos = new BlockPos.MutableBlockPos();
        int solidCount = 0;
        int quadCount = 0;

        float waterU0 = 0f, waterV0 = 0f, waterU1 = 1f, waterV1 = 1f;
        try {
            var tex = mc.getTextureManager().getTexture(InventoryMenu.BLOCK_ATLAS);
            if (tex instanceof TextureAtlas atlas) {
                ResourceLocation waterRl = ResourceLocation.tryParse("minecraft:block/water_still");
                if (waterRl != null) {
                    var sprite = atlas.getSprite(waterRl);
                    if (sprite != null) {
                        waterU0 = sprite.getU0();
                        waterV0 = sprite.getV0();
                        waterU1 = sprite.getU1();
                        waterV1 = sprite.getV1();
                    }
                }
            }
        } catch (Throwable ignored) {}

        for (int x = cx - radius; x <= cx + radius; x++) {
            for (int z = cz - radius; z <= cz + radius; z++) {
                for (int y = minY; y <= maxY; y++) {
                    mpos.set(x, y, z);
                    BlockState state;
                    try {
                        state = level.getBlockState(mpos);
                    } catch (Throwable t) {
                        continue;
                    }
                    if (state.isAir()) continue;

                    var block = state.getBlock();
                    boolean isWaterBlock = block == Blocks.WATER;

                    boolean hasWaterFluid = false;
                    try {
                        FluidState fs = state.getFluidState();
                        hasWaterFluid = fs != null && !fs.isEmpty() && fs.is(FluidTags.WATER);
                    } catch (Throwable ignored) {}

                    solidCount++;

                    if (isWaterBlock || hasWaterFluid) {
                        for (Direction dir : Direction.values()) {
                            npos.set(mpos).move(dir);
                            BlockState ns;
                            try { ns = level.getBlockState(npos); } catch (Throwable t) { continue; }
                            if (isWaterState(ns)) continue;

                            boolean neighborOccludes;
                            try {
                                neighborOccludes = ns.canOcclude() && ns.isSolidRender(level, npos);
                            } catch (Throwable t) {
                                neighborOccludes = false;
                            }
                            if (!neighborOccludes) {
                                addWaterFace(verts, indices, x, y, z, dir,
                                        waterU0, waterV0, waterU1, waterV1);
                                quadCount++;
                            }
                        }
                    }

                    if (isWaterBlock) continue;

                    int material = detectMaterial(state);

                    try {
                        BakedModel model = dispatcher.getBlockModelShaper().getBlockModel(state);
                        if (model == null) continue;

                        for (Direction dir : Direction.values()) {
                            List<BakedQuad> quads = model.getQuads(state, dir, rand);
                            for (BakedQuad q : quads) {
                                appendQuad(verts, indices, x, y, z, q, material, state, level, mpos);
                                quadCount++;
                            }
                        }
                        List<BakedQuad> unDirQuads = model.getQuads(state, null, rand);
                        for (BakedQuad q : unDirQuads) {
                            appendQuad(verts, indices, x, y, z, q, material, state, level, mpos);
                            quadCount++;
                        }
                    } catch (Throwable ignored) {}
                }
            }
        }

        if (verts.isEmpty()) return;

        float[] vArr = new float[verts.size()];
        for (int i = 0; i < vArr.length; i++) vArr[i] = verts.get(i);

        int[] iArr = new int[indices.size()];
        for (int i = 0; i < iArr.length; i++) iArr[i] = indices.get(i);

        pendingVerts = vArr;
        pendingIndices = iArr;
        pendingUpload = true;

        System.out.println("[Yaoguang] Scene collected: " + solidCount + " blocks, "
                + quadCount + " quads, verts=" + (vArr.length / VERTEX_FLOATS)
                + " radius=" + radius);
    }

    private void addWaterFace(List<Float> verts, List<Integer> indices,
                              int x, int y, int z, Direction dir,
                              float u0, float v0, float u1, float v1) {
        float x0 = x,       y0 = y,       z0 = z;
        float x1 = x + 1f,  y1 = y + 1f,  z1 = z + 1f;

        float[] pA, pB, pC, pD;
        float nx = 0f, ny = 0f, nz = 0f;
        switch (dir) {
            case DOWN:  pA = new float[]{x0, y0, z0}; pB = new float[]{x1, y0, z0}; pC = new float[]{x1, y0, z1}; pD = new float[]{x0, y0, z1}; ny = -1f; break;
            case UP:    pA = new float[]{x0, y1, z0}; pB = new float[]{x1, y1, z0}; pC = new float[]{x1, y1, z1}; pD = new float[]{x0, y1, z1}; ny =  1f; break;
            case NORTH: pA = new float[]{x0, y0, z0}; pB = new float[]{x1, y0, z0}; pC = new float[]{x1, y1, z0}; pD = new float[]{x0, y1, z0}; nz = -1f; break;
            case SOUTH: pA = new float[]{x0, y0, z1}; pB = new float[]{x1, y0, z1}; pC = new float[]{x1, y1, z1}; pD = new float[]{x0, y1, z1}; nz =  1f; break;
            case WEST:  pA = new float[]{x0, y0, z0}; pB = new float[]{x0, y0, z1}; pC = new float[]{x0, y1, z1}; pD = new float[]{x0, y1, z0}; nx = -1f; break;
            case EAST:  pA = new float[]{x1, y0, z0}; pB = new float[]{x1, y0, z1}; pC = new float[]{x1, y1, z1}; pD = new float[]{x1, y1, z0}; nx =  1f; break;
            default: return;
        }

        float[][] positions = {pA, pB, pC, pD};
        float[][] uvs = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};

        float mf = (float) MAT_WATER;
        int faceBase = verts.size() / VERTEX_FLOATS;

        for (int v = 0; v < 4; v++) {
            verts.add(positions[v][0]);
            verts.add(positions[v][1]);
            verts.add(positions[v][2]);
            verts.add(nx);
            verts.add(ny);
            verts.add(nz);
            verts.add(1f);
            verts.add(1f);
            verts.add(1f);
            verts.add(uvs[v][0]);
            verts.add(uvs[v][1]);
            verts.add(mf);
        }

        indices.add(faceBase + 0);
        indices.add(faceBase + 1);
        indices.add(faceBase + 2);
        indices.add(faceBase + 0);
        indices.add(faceBase + 2);
        indices.add(faceBase + 3);
    }

    private void appendQuad(List<Float> verts, List<Integer> indices,
                            int bx, int by, int bz, BakedQuad q, int material,
                            BlockState state, ClientLevel level, BlockPos pos) {
        int[] data = q.getVertices();
        if (data == null) return;
        int stride = 8;
        if (data.length < stride * 4) return;

        float tr = 1f, tg = 1f, tb = 1f;
        int tintIdx = q.getTintIndex();
        if (tintIdx >= 0) {
            try {
                int tint = Minecraft.getInstance().getBlockColors().getColor(state, level, pos, tintIdx);
                if (tint != -1) {
                    tr = ((tint >> 16) & 0xFF) / 255f;
                    tg = ((tint >> 8) & 0xFF) / 255f;
                    tb = (tint & 0xFF) / 255f;
                }
            } catch (Throwable ignored) {}
        }

        float[] px = new float[4];
        float[] py = new float[4];
        float[] pz = new float[4];
        float[] uu = new float[4];
        float[] vv = new float[4];

        for (int i = 0; i < 4; i++) {
            int base = i * stride;
            px[i] = Float.intBitsToFloat(data[base + 0]) + bx;
            py[i] = Float.intBitsToFloat(data[base + 1]) + by;
            pz[i] = Float.intBitsToFloat(data[base + 2]) + bz;
            uu[i] = Float.intBitsToFloat(data[base + 4]);
            vv[i] = Float.intBitsToFloat(data[base + 5]);
        }

        float ax = px[1] - px[0], ay = py[1] - py[0], az = pz[1] - pz[0];
        float qx = px[2] - px[0], qy = py[2] - py[0], qz = pz[2] - pz[0];
        float nx = ay * qz - az * qy;
        float ny = az * qx - ax * qz;
        float nz = ax * qy - ay * qx;
        float nl = (float) Math.sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-6f) nl = 1f;
        nx /= nl; ny /= nl; nz /= nl;

        float mf = (float) material;
        int faceBase = verts.size() / VERTEX_FLOATS;

        for (int i = 0; i < 4; i++) {
            verts.add(px[i]);
            verts.add(py[i]);
            verts.add(pz[i]);
            verts.add(nx);
            verts.add(ny);
            verts.add(nz);
            verts.add(tr);
            verts.add(tg);
            verts.add(tb);
            verts.add(uu[i]);
            verts.add(vv[i]);
            verts.add(mf);
        }

        indices.add(faceBase + 0);
        indices.add(faceBase + 1);
        indices.add(faceBase + 2);
        indices.add(faceBase + 0);
        indices.add(faceBase + 2);
        indices.add(faceBase + 3);
    }
}