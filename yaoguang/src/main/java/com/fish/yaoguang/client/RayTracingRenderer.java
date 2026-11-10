package com.fish.yaoguang.client;

import com.fish.yaoguang.YaoguangConfig;
import com.fish.yaoguang.client.mixin.ParticleAccessor;
import com.fish.yaoguang.client.mixin.ParticleEngineAccessor;
import com.fish.yaoguang.nativebridge.YaoguangNative;
import com.fish.yaoguang.util.VramExtender;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.math.Axis;
import net.minecraft.client.Minecraft;
import net.minecraft.client.model.HumanoidModel;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleRenderType;
import net.minecraft.client.renderer.block.BlockRenderDispatcher;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.core.BlockPos;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.inventory.InventoryMenu;
import net.minecraft.world.level.block.entity.BeaconBlockEntity;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Vector4f;
import org.joml.Vector3f;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL30;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public final class RayTracingRenderer {
    private static final org.slf4j.Logger LOG =
            org.slf4j.LoggerFactory.getLogger("yaoguang");

    private static void log(String message) {
        System.out.println(message);
        LOG.info(message);
    }

    private int lastCfgBounces = -1;
    private int lastCfgSpp = -1;
    private int lastCfgDlss = -1;
    private int lastCfgRenderScale = -1;

    private static final java.util.Set<String> WARNED = java.util.concurrent.ConcurrentHashMap.newKeySet();

    private static void warnOnce(String name, Throwable e) {
        if (WARNED.add(name)) log("[Yaoguang] native call failed: " + name + " -> " + e);
    }

    private int proxyLogCounter = 0;

    public static final RayTracingRenderer INSTANCE = new RayTracingRenderer();

    private static final int BONE_FLOATS = 20;
    private static final int MAX_BONES = 4096;
    private static final int PARTICLE_FLOATS = 8;
    private static final int MAX_PARTICLES = 1024;
    private static final int MAT_DIFFUSE = 0;
    private static final int MAT_METAL = 1;
    private static final int MAT_GLASS = 2;
    private static final int MAT_WATER = 3;
    private static final int MAT_EMISSIVE = 4;
    private static final int MAT_ENTITY_PROXY = 8;
    private static final Map<Class<?>, Boolean> PARTICLE_PROXY_CACHE = new java.util.concurrent.ConcurrentHashMap<>();

    private static final long MIN_REBUILD_INTERVAL_MS = 2500L;
    private static final long URGENT_REBUILD_MS = 150L;

    private final java.util.concurrent.ConcurrentHashMap<Long, SceneMesher.ChunkMesh> chunkCache =
            new java.util.concurrent.ConcurrentHashMap<>();
    private final java.util.Set<Long> dirtyChunks = java.util.concurrent.ConcurrentHashMap.newKeySet();

    private volatile int lastCacheCenterX = Integer.MIN_VALUE;
    private volatile int lastCacheCenterZ = Integer.MIN_VALUE;

    private volatile boolean sceneDirty = true;
    private volatile boolean urgentDirty = false;
    private volatile boolean sceneValid = false;
    private long lastRebuildMs;
    private static final boolean FORCE_RAIN = System.getenv("YAOGUANG_RAIN_FORCE") != null;
    private int lastRadiusChunks = -1;
    private int lastCenterChunkX = Integer.MIN_VALUE;
    private int lastCenterChunkZ = Integer.MIN_VALUE;
    private int rebuildCount;
    private int frameCount;

    private final AtomicBoolean rebuildInFlight = new AtomicBoolean(false);
    private final AtomicInteger rebuildGeneration = new AtomicInteger();
    private volatile SceneMesher.Mesh pendingMesh;
    private volatile java.util.List<PendingChunk> pendingChunks;
    private final java.util.Map<Long, Integer> chunkSlots = new java.util.HashMap<>();
    private volatile boolean rtDeviceLost = false;
    private int lastDimensionId = Integer.MIN_VALUE;
    private volatile boolean missingChunks = false;
    private long lastBuildLogMs = 0L;
    private int emptyRebuilds = 0;
    private boolean pendingDimReset = false;
    private int nextChunkSlot = 0;

    static final class PendingChunk {
        final int cx;
        final int cz;
        final SceneMesher.ChunkMesh mesh;

        PendingChunk(int cx, int cz, SceneMesher.ChunkMesh mesh) {
            this.cx = cx;
            this.cz = cz;
            this.mesh = mesh;
        }
    }

    private boolean blockAtlasUploaded;
    private int lastBlockAtlasId = -1;
    private int atlasCheckTimer;

    private final ExecutorService rebuildExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "Yaoguang-SceneMesher");
        thread.setDaemon(true);
        thread.setPriority(Thread.MIN_PRIORITY);
        return thread;
    });

    private final FloatBuffer boneBuffer = BufferUtils.createFloatBuffer(MAX_BONES * BONE_FLOATS);
    private final FloatBuffer particleBuffer = BufferUtils.createFloatBuffer(MAX_PARTICLES * PARTICLE_FLOATS);
    private float[] boneData = new float[MAX_BONES * BONE_FLOATS];
    private int boneCount;

    private final ConcurrentLinkedQueue<EntitySnapshot> entityQueue = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<ParticleSnapshot> particleQueue = new ConcurrentLinkedQueue<>();

    private int entityCaptureLog;
    private int particleCaptureLog;

    private float lastSkyR;
    private float lastSkyG;
    private float lastSkyB;
    private float lastCloudR;
    private float lastCloudG;
    private float lastCloudB;
    private float lastFlash;
    private long lastWeatherSample;

    private static final class EntitySnapshot {
        final double x;
        final double y;
        final double z;
        final float halfX;
        final float halfY;
        final float halfZ;
        final float red;
        final float green;
        final float blue;
        final int material;

        EntitySnapshot(double x, double y, double z, float halfX, float halfY, float halfZ,
                       float red, float green, float blue, int material) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.halfX = halfX;
            this.halfY = halfY;
            this.halfZ = halfZ;
            this.red = red;
            this.green = green;
            this.blue = blue;
            this.material = material;
        }
    }

    private static final class ParticleSnapshot {
        final double x;
        final double y;
        final double z;
        final float size;
        final float red;
        final float green;
        final float blue;
        final float alpha;

        ParticleSnapshot(double x, double y, double z, float size,
                         float red, float green, float blue, float alpha) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.size = size;
            this.red = red;
            this.green = green;
            this.blue = blue;
            this.alpha = alpha;
        }
    }

    private RayTracingRenderer() {
    }

    public void markSceneDirty() {
        sceneDirty = true;
    }

    public void markChunkDirty(int cx, int cz) {
        dirtyChunks.add(((long) cx << 32) ^ (cz & 0xffffffffL));
        sceneDirty = true;
        urgentDirty = true;
    }

    public void markAllChunksDirty() {
        chunkCache.clear();
        sceneDirty = true;
        urgentDirty = true;
    }

    public void markSceneDirtyUrgent() {
        urgentDirty = true;
        sceneDirty = true;
    }

    public boolean isRendering() {
        return sceneValid && YaoguangConfig.isRayTracingEnabled();
    }

    public boolean addBoneBox(float cx, float cy, float cz, int material,
                              float ax, float ay, float az, float hx,
                              float bx, float by, float bz, float hy,
                              float tx, float ty, float tz, float hz,
                              float red, float green, float blue, float alpha) {
        if (boneCount >= MAX_BONES) return false;
        int i = boneCount * BONE_FLOATS;
        boneData[i] = cx;
        boneData[i + 1] = cy;
        boneData[i + 2] = cz;
        boneData[i + 3] = material;
        boneData[i + 4] = ax;
        boneData[i + 5] = ay;
        boneData[i + 6] = az;
        boneData[i + 7] = hx;
        boneData[i + 8] = bx;
        boneData[i + 9] = by;
        boneData[i + 10] = bz;
        boneData[i + 11] = hy;
        boneData[i + 12] = tx;
        boneData[i + 13] = ty;
        boneData[i + 14] = tz;
        boneData[i + 15] = hz;
        boneData[i + 16] = red;
        boneData[i + 17] = green;
        boneData[i + 18] = blue;
        boneData[i + 19] = alpha;
        boneCount++;
        return true;
    }

    public void onEntityRendered(Entity entity, com.mojang.blaze3d.vertex.PoseStack poseStack) {
        if (entity == null) return;
        if (entity instanceof LivingEntity) return;
        addEntityBox(entity);
    }

    public void addEntityBox(Entity entity) {
        if (entity == null) return;
        if (entityQueue.size() > 4096) return;

        try {
            AABB box = entity.getBoundingBox();
            Vec3 center = box.getCenter();
            double halfX = box.getXsize() * 0.5;
            double halfY = box.getYsize() * 0.5;
            double halfZ = box.getZsize() * 0.5;

            int color = entityColor(entity);
            float red = ((color >> 16) & 0xFF) / 255.0f;
            float green = ((color >> 8) & 0xFF) / 255.0f;
            float blue = (color & 0xFF) / 255.0f;

            if (entityCaptureLog < 3) {
                log("[Yaoguang] entity box: " + entity.getType());
                entityCaptureLog++;
            }

            entityQueue.add(new EntitySnapshot(center.x, center.y, center.z,
                    (float) halfX, (float) halfY, (float) halfZ,
                    red, green, blue, entityMaterial(entity)));
        } catch (Throwable ignored) {
        }
    }

    public void onParticleRendered(double x, double y, double z, float size,
                                   float red, float green, float blue, float alpha) {
        if (particleQueue.size() > 16384) return;
        if (particleCaptureLog < 3) {
            log("[Yaoguang] particle captured");
            particleCaptureLog++;
        }
        particleQueue.add(new ParticleSnapshot(x, y, z, size, red, green, blue, alpha));
    }

    public void beginFrame() {
        CompositeRenderer.tickShutdown();

        Minecraft mc = Minecraft.getInstance();
        if (mc == null || mc.level == null || mc.player == null || mc.gameRenderer == null) return;

        frameCount++;
        int renderScale = YaoguangConfig.getRenderScale();
        int cfgBounces = YaoguangConfig.getMaxBounces();
        int cfgSpp = YaoguangConfig.getSamplesPerPixel();
        int cfgDlss = YaoguangConfig.getDlssQuality();
        if (cfgBounces != lastCfgBounces || cfgSpp != lastCfgSpp
                || cfgDlss != lastCfgDlss || renderScale != lastCfgRenderScale) {
            lastCfgBounces = cfgBounces;
            lastCfgSpp = cfgSpp;
            lastCfgDlss = cfgDlss;
            lastCfgRenderScale = renderScale;
            log("[Yaoguang] config changed: preset=" + YaoguangConfig.getCurrentPreset().name()
                    + " bounces=" + cfgBounces + " spp=" + cfgSpp
                    + " dlss=" + cfgDlss + " renderScale=" + renderScale
                    + " sceneValid=" + sceneValid);
        }
        int windowWidth = mc.getWindow().getWidth();
        int windowHeight = mc.getWindow().getHeight();

        try {
            long handle = mc.getWindow().getWindow();
            if (handle != 0L) {
                YaoguangNative.setRenderScale(renderScale / 100.0f);
                YaoguangNative.onFrameBegin(handle, windowWidth, windowHeight);
            }
        } catch (Throwable t) {
            if (frameCount < 5) log("[Yaoguang] onFrameBegin failed: " + t);
        }

        updateBeacons(mc);

        atlasCheckTimer++;
        if (!blockAtlasUploaded || atlasCheckTimer >= 6) {
            atlasCheckTimer = 0;
            ensureBlockAtlasUploaded(mc);
        }

        int dimensionId = mc.level.dimension() == net.minecraft.world.level.Level.OVERWORLD ? 0
                : (mc.level.dimension() == net.minecraft.world.level.Level.NETHER ? 1 : 2);
        if (dimensionId != lastDimensionId) {
            lastDimensionId = dimensionId;
            pendingDimReset = true;
            pendingChunks = null;
            lastCacheCenterX = Integer.MIN_VALUE;
            lastCacheCenterZ = Integer.MIN_VALUE;
            emptyRebuilds = 0;
            dirtyChunks.clear();
            sceneDirty = true;
            log("[Yaoguang] dimension changed -> holding old scene until new chunks (dim=" + dimensionId + ")");
        }

        if (pendingChunks != null) {
            java.util.List<PendingChunk> chunks = pendingChunks;
            pendingChunks = null;
            if (pendingDimReset && !chunks.isEmpty()) {
                sceneValid = false;
                try {
                    YaoguangNative.flushGpu();
                } catch (Throwable ignored) {
                }
                for (Integer slot : chunkSlots.values()) {
                    try {
                        YaoguangNative.removeChunkGeometry(slot);
                    } catch (Throwable ignored) {
                    }
                }
                chunkSlots.clear();
                chunkCache.clear();
                nextChunkSlot = 0;
                pendingDimReset = false;
                sceneValid = false;
                rebuildGeneration.incrementAndGet();
                log("[Yaoguang] dimension reset applied with " + chunks.size() + " chunks");
            }
            try {
                int verts = 0;
                int idxs = 0;
                if (!chunks.isEmpty()) {
                    YaoguangNative.beginChunkBatch();
                    for (PendingChunk pc : chunks) {
                        long key = ((long) pc.cx << 32) ^ (pc.cz & 0xffffffffL);
                        Integer slot = chunkSlots.get(key);
                        if (slot == null) {
                            slot = nextChunkSlot++;
                            chunkSlots.put(key, slot);
                        }
                        YaoguangNative.uploadChunkGeometry(slot, pc.cx, pc.cz, pc.mesh.verts, pc.mesh.idx);
                        verts += pc.mesh.verts.length / SceneBuffers.VERTEX_FLOATS;
                        idxs += pc.mesh.idx.length;
                    }
                    YaoguangNative.commitChunkBatch();
                    sceneValid = true;
                    sceneDirty = false;
                    emptyRebuilds = 0;
                } else {
                    emptyRebuilds++;
                    sceneDirty = missingChunks;
                }
                log("[Yaoguang] Scene rebuilt: " + verts + " verts, " + idxs
                        + " indices in " + chunks.size() + " chunks"
                        + (missingChunks ? " (waiting for chunks)" : ""));
            } catch (Throwable t) {
                log("[Yaoguang] chunk upload failed: " + t);
                t.printStackTrace();
            }
        }

        Vec3 playerPos = mc.player.position();
        BlockPos playerBlock = BlockPos.containing(playerPos);
        int centerChunkX = ((int) Math.floor(playerPos.x)) >> 4;
        int centerChunkZ = ((int) Math.floor(playerPos.z)) >> 4;
        int radiusChunks = Math.min(8, mc.options.renderDistance().get());
        boolean moved = centerChunkX != lastCenterChunkX || centerChunkZ != lastCenterChunkZ
                || radiusChunks != lastRadiusChunks;
        boolean needRebuild = !sceneValid || sceneDirty || urgentDirty || moved;
        long interval = !sceneValid ? 0L
                : urgentDirty ? URGENT_REBUILD_MS
                : moved ? 5000L
                : MIN_REBUILD_INTERVAL_MS;
        if (emptyRebuilds > 0 && interval < 2500L) {
            interval = 2500L;
        }
        if (needRebuild && !rebuildInFlight.get() && pendingChunks == null
                && System.currentTimeMillis() - lastRebuildMs >= interval) {
            startRebuild(mc, playerBlock, radiusChunks, centerChunkX, centerChunkZ, System.currentTimeMillis());
        }

        if (!sceneValid) return;

        Vec3 cameraPos = mc.gameRenderer.getMainCamera().getPosition();
        float yaw = mc.gameRenderer.getMainCamera().getYRot();
        float pitch = mc.gameRenderer.getMainCamera().getXRot();
        String cameraOverride = System.getenv("YAOGUANG_CAM");
        if (cameraOverride != null) {
            String[] parts = cameraOverride.split(",");
            if (parts.length >= 5) {
                try {
                    cameraPos = new Vec3(Double.parseDouble(parts[0]), Double.parseDouble(parts[1]), Double.parseDouble(parts[2]));
                    yaw = Float.parseFloat(parts[3]);
                    pitch = Float.parseFloat(parts[4]);
                } catch (NumberFormatException ignored) {
                }
            }
        }
        long now = System.currentTimeMillis();
        float partialTick = mc.getFrameTime();

        try {
            CompositeRenderer.notifyCamera(cameraPos.x, cameraPos.y, cameraPos.z, yaw, pitch);
            CompositeRenderer.setTemporalAccumulation(!YaoguangConfig.isDenoiserEnabled());
        } catch (Throwable ignored) {
        }

        updateEnvironment(mc, cameraPos, partialTick, now);

        float fov = mc.options.fov().get();
        float aspect = (float) windowWidth / (float) Math.max(1, windowHeight);

        double yawRad = Math.toRadians(yaw);
        double pitchRad = Math.toRadians(pitch);
        float dirX = (float) (-Math.sin(yawRad) * Math.cos(pitchRad));
        float dirY = (float) (-Math.sin(pitchRad));
        float dirZ = (float) (Math.cos(yawRad) * Math.cos(pitchRad));

        try {
            YaoguangNative.setCamera(
                    (float) cameraPos.x, (float) cameraPos.y, (float) cameraPos.z,
                    dirX, dirY, dirZ,
                    0.0f, 1.0f, 0.0f,
                    fov, aspect);
            org.lwjgl.opengl.GL11.glFinish();
        if (sceneValid && !rtDeviceLost) {
            YaoguangNative.renderFrame();
            if (YaoguangNative.isDeviceLost()) {
                rtDeviceLost = true;
                sceneValid = false;
                log("[Yaoguang] GPU device lost - ray tracing stopped, restart the game to re-enable");
            }
        }
        } catch (Throwable t) {
            log("[Yaoguang] renderFrame failed: " + t);
            t.printStackTrace();
        }
    }

    private void startRebuild(Minecraft mc, BlockPos playerPos, int radiusChunks,
                              int centerChunkX, int centerChunkZ, long now) {
        rebuildInFlight.set(true);
        lastRebuildMs = now;
        lastRadiusChunks = radiusChunks;
        lastCenterChunkX = centerChunkX;
        lastCenterChunkZ = centerChunkZ;
        sceneDirty = false;
        urgentDirty = false;

        ClientLevel level = mc.level;
        BlockRenderDispatcher dispatcher = mc.getBlockRenderer();
        TextureAtlas atlas = null;
        try {
            var texture = mc.getTextureManager().getTexture(InventoryMenu.BLOCK_ATLAS);
            if (texture instanceof TextureAtlas blockAtlas) atlas = blockAtlas;
        } catch (Throwable ignored) {
        }

        final TextureAtlas finalAtlas = atlas;
        final int generation = rebuildGeneration.incrementAndGet();
        final BlockPos center = playerPos.immutable();

        final boolean fullRebuild = chunkCache.isEmpty()
                || Math.abs(centerChunkX - lastCacheCenterX) >= 3
                || Math.abs(centerChunkZ - lastCacheCenterZ) >= 3;
        if (fullRebuild) {
            chunkCache.clear();
            dirtyChunks.clear();
        }
        lastCacheCenterX = centerChunkX;
        lastCacheCenterZ = centerChunkZ;

        rebuildExecutor.submit(() -> {
            try {
                java.util.List<PendingChunk> list = buildChunkList(level, center, radiusChunks, dispatcher, finalAtlas, fullRebuild);
                if (generation == rebuildGeneration.get()) {
                    pendingChunks = list;
                }
            } catch (Throwable t) {
                log("[Yaoguang] scene build failed: " + t);
                t.printStackTrace();
                sceneDirty = true;
            } finally {
                rebuildInFlight.set(false);
            }
        });
    }

    private java.util.List<PendingChunk> buildChunkList(ClientLevel level, BlockPos center, int radiusChunks,
                                                        BlockRenderDispatcher dispatcher, TextureAtlas atlas,
                                                        boolean fullRebuild) {
        int cx0 = center.getX() >> 4;
        int cz0 = center.getZ() >> 4;
        if (fullRebuild) {
            chunkCache.clear();
            dirtyChunks.clear();
        }
        java.util.List<PendingChunk> list = new java.util.ArrayList<>();
        boolean missing = false;
        int emptyChunkCount = 0;
        java.util.Set<Long> dirty = new java.util.HashSet<>(dirtyChunks);
        dirtyChunks.removeAll(dirty);
        for (int ring = 0; ring <= radiusChunks; ring++) {
            for (int dz = -ring; dz <= ring; dz++) {
                for (int dx = -ring; dx <= ring; dx++) {
                    if (Math.max(Math.abs(dx), Math.abs(dz)) != ring) continue;
                    int cx = cx0 + dx;
                    int cz = cz0 + dz;
                    LevelChunk chunk = level.getChunkSource().getChunk(cx, cz,
                            net.minecraft.world.level.chunk.ChunkStatus.FULL, false);
                    if (chunk == null) {
                        missing = true;
                        continue;
                    }
                    if (chunk.isEmpty()) {
                        emptyChunkCount++;
                        continue;
                    }
                    long key = ((long) cx << 32) ^ (cz & 0xffffffffL);
                    SceneMesher.ChunkMesh mesh = chunkCache.get(key);
                    if (mesh == null || dirty.contains(key)) {
                        mesh = SceneMesher.buildSingleChunk(level, chunk, dispatcher, atlas);
                        chunkCache.put(key, mesh);
                        list.add(new PendingChunk(cx, cz, mesh));
                    }
                }
            }
        }
        missingChunks = missing;
        long nowMs = System.currentTimeMillis();
        if (nowMs - lastBuildLogMs > 3000L) {
            lastBuildLogMs = nowMs;
            log("[Yaoguang] build center=" + cx0 + "," + cz0 + " r=" + radiusChunks
                    + " meshed=" + list.size() + " air=" + emptyChunkCount + " missing=" + missing);
        }
        return list;
    }

    private SceneMesher.Mesh buildIncremental(ClientLevel level, BlockPos center, int radiusChunks,
                                              BlockRenderDispatcher dispatcher, TextureAtlas atlas) {
        int cx0 = center.getX() >> 4;
        int cz0 = center.getZ() >> 4;
        java.util.List<SceneMesher.ChunkMesh> list = new java.util.ArrayList<>();
        java.util.Set<Long> dirty = new java.util.HashSet<>(dirtyChunks);
        dirtyChunks.removeAll(dirty);
        int chunks = 0;
        for (int ring = 0; ring <= radiusChunks; ring++) {
            for (int dz = -ring; dz <= ring; dz++) {
                for (int dx = -ring; dx <= ring; dx++) {
                    if (Math.max(Math.abs(dx), Math.abs(dz)) != ring) continue;
                    int cx = cx0 + dx;
                    int cz = cz0 + dz;
                    LevelChunk chunk = level.getChunkSource().getChunk(cx, cz,
                            net.minecraft.world.level.chunk.ChunkStatus.FULL, false);
                    if (chunk == null || chunk.isEmpty()) continue;
                    chunks++;
                    long key = ((long) cx << 32) ^ (cz & 0xffffffffL);
                    SceneMesher.ChunkMesh mesh = chunkCache.get(key);
                    if (mesh == null || dirty.contains(key)) {
                        mesh = SceneMesher.buildSingleChunk(level, chunk, dispatcher, atlas);
                        chunkCache.put(key, mesh);
                    }
                    list.add(mesh);
                }
            }
        }
        return SceneMesher.assemble(list, chunks, radiusChunks);
    }

    private void updateBeacons(Minecraft mc) {
        boneBuffer.clear();
        int count = writeBeaconBeams(mc, 0);
        boneBuffer.flip();
        try {
            if (count > 0) {
                YaoguangNative.uploadDynamic(boneBuffer, count);
            } else {
                YaoguangNative.clearDynamic();
            }
        } catch (Throwable ignored) {
        }
    }

    private int writeBeaconBeams(Minecraft mc, int start) {
        if (!(mc.level instanceof ClientLevel)) return start;
        if (System.getenv("YAOGUANG_BEAMTEST") != null) {
            var cam = mc.gameRenderer.getMainCamera();
            var look = cam.getLookVector();
            Vec3 base = cam.getPosition().add(look.x * 10.0, look.y * 10.0 + 6.0, look.z * 10.0);
            writeProxyBox(base.x, base.y, base.z, 0.16f, 10.0f, 0.16f, 1.0f, 0.97f, 0.88f, 12);
            start++;
        }
        ClientLevel level = mc.level;
        BlockPos center = BlockPos.containing(mc.gameRenderer.getMainCamera().getPosition());
        int radius = 6;
        int count = start;
        for (int cx = -radius; cx <= radius; cx++) {
            for (int cz = -radius; cz <= radius; cz++) {
                LevelChunk chunk;
                try {
                    chunk = level.getChunk((center.getX() >> 4) + cx, (center.getZ() >> 4) + cz);
                } catch (Throwable ignored) {
                    continue;
                }
                if (chunk == null) continue;
                for (BlockEntity be : chunk.getBlockEntities().values()) {
                    if (!(be instanceof BeaconBlockEntity beacon)) continue;
                    float height = 0.0f;
                    try {
                        var sections = beacon.getBeamSections();
                        if (sections != null && !sections.isEmpty()) height = 384.0f;
                    } catch (Throwable ignored) {
                    }
                    if (height <= 0.0f) continue;
                    if (count >= MAX_BONES) break;
                    BlockPos pos = beacon.getBlockPos();
                    writeProxyBox(pos.getX() + 0.5, pos.getY() + 1.0 + height * 0.5, pos.getZ() + 0.5,
                            0.16f, height * 0.5f, 0.16f, 1.0f, 0.97f, 0.88f, 12);
                    count++;
                }
            }
        }
        return count;
    }

    private void captureEntityProxies(Minecraft mc) {
        boneBuffer.clear();
        int count = 0;
        Vec3 camera = mc.gameRenderer.getMainCamera().getPosition();
        boolean firstPerson = mc.options.getCameraType().isFirstPerson();
        double maxDistanceSq = 96.0 * 96.0;
        float partialTick = mc.getFrameTime();

        boolean playerSeen = false;
        boolean entityProxies = System.getenv("YAOGUANG_PROXY") != null;
        try {
            for (Entity entity : entityProxies ? mc.level.entitiesForRendering() : java.util.List.<Entity>of()) {
                if (entity == null || entity.isInvisible()) continue;
                if (entity == mc.player) {
                    playerSeen = true;
                    if (firstPerson) continue;
                }
                if (entity.distanceToSqr(camera.x, camera.y, camera.z) > maxDistanceSq) continue;
                int boxes = writeEntityProxy(entity, partialTick);
                if (boxes <= 0) continue;
                count += boxes;
                if (count >= MAX_BONES) break;
            }
            if (entityProxies && !firstPerson && !playerSeen && mc.player != null && count < MAX_BONES) {
                count += writeEntityProxy(mc.player, partialTick);
            }
        } catch (Throwable ignored) {
        }

        count = writeBeaconBeams(mc, count);

        boneBuffer.flip();
        if (frameCount % 600 == 0) {
            log("[Yaoguang] dynamic proxies=" + count);
        }
        try {
            if (count > 0) {
                YaoguangNative.uploadDynamic(boneBuffer, count);
            } else {
                YaoguangNative.clearDynamic();
            }
        } catch (Throwable ignored) {
        }
    }

    private int writeEntityProxy(Entity entity, float partialTick) {
        AABB box = entity.getBoundingBox();
        double x = Mth.lerp(partialTick, entity.xOld, entity.getX());
        double y = Mth.lerp(partialTick, entity.yOld, entity.getY());
        double z = Mth.lerp(partialTick, entity.zOld, entity.getZ());
        int color = EntityTextureColors.averageFor(entity);
        float cr = ((color >> 16) & 0xFF) / 255.0f;
        float cg = ((color >> 8) & 0xFF) / 255.0f;
        float cb = (color & 0xFF) / 255.0f;
        float lum = Math.max(cr, Math.max(cg, cb));
        if (lum < 0.16f) {
            float scale = 0.28f / Math.max(lum, 0.02f);
            cr = Math.min(1.0f, cr * scale);
            cg = Math.min(1.0f, cg * scale);
            cb = Math.min(1.0f, cb * scale);
        }
        float width = (float) Math.max(0.12, box.getXsize());
        float depth = (float) Math.max(0.12, box.getZsize());
        float height = (float) Math.max(0.24, box.getYsize());
        if (entity instanceof LivingEntity living) {
            int cubes = writeHumanoidCubes(living, partialTick, x, y, z, cr, cg, cb);
            if (cubes > 0) return cubes;
            writeProxyBox(x, y + height * 0.54, z, width * 0.45f, height * 0.30f, depth * 0.34f, cr, cg, cb);
            writeProxyBox(x, y + height * 0.88, z, width * 0.32f, height * 0.16f, depth * 0.32f, cr, cg, cb);
            writeProxyBox(x, y + height * 0.20, z, width * 0.38f, height * 0.20f, depth * 0.32f, cr, cg, cb);
            return 3;
        }
        writeProxyBox(x, y + height * 0.5, z, width * 0.5f, height * 0.5f, depth * 0.5f, cr, cg, cb);
        return 1;
    }

    private int writeHumanoidCubes(LivingEntity entity, float partialTick, double x, double y, double z,
                                   float cr, float cg, float cb) {
        try {
            EntityRenderer<?> renderer = Minecraft.getInstance().getEntityRenderDispatcher().getRenderer(entity);
            if (!(renderer instanceof LivingEntityRenderer<?, ?> livingRenderer)) return 0;
            if (!(livingRenderer.getModel() instanceof HumanoidModel<?> model)) return 0;
            PoseStack pose = new PoseStack();
            pose.translate(x, y, z);
            float bodyYaw = Mth.rotLerp(partialTick, entity.yBodyRotO, entity.yBodyRot);
            pose.mulPose(Axis.YP.rotationDegrees(180.0f - bodyYaw));
            pose.scale(-1.0f, -1.0f, 1.0f);
            pose.translate(0.0f, -1.501f, 0.0f);
            ModelPart[] parts = {model.head, model.body, model.rightArm, model.leftArm, model.rightLeg, model.leftLeg};
            int[] emitted = {0};
            for (ModelPart part : parts) {
                part.visit(pose, (p, path, index, cube) -> {
                    if (emitted[0] >= 128) return;
                    Matrix4f matrix = p.pose();
                    float minX = Float.POSITIVE_INFINITY;
                    float minY = Float.POSITIVE_INFINITY;
                    float minZ = Float.POSITIVE_INFINITY;
                    float maxX = Float.NEGATIVE_INFINITY;
                    float maxY = Float.NEGATIVE_INFINITY;
                    float maxZ = Float.NEGATIVE_INFINITY;
                    Vector4f vertex = new Vector4f();
                    for (int i = 0; i < 8; i++) {
                        float px = ((i & 1) == 0 ? cube.minX : cube.maxX) / 16.0f;
                        float py = ((i & 2) == 0 ? cube.minY : cube.maxY) / 16.0f;
                        float pz = ((i & 4) == 0 ? cube.minZ : cube.maxZ) / 16.0f;
                        vertex.set(px, py, pz, 1.0f).mul(matrix);
                        minX = Math.min(minX, vertex.x);
                        minY = Math.min(minY, vertex.y);
                        minZ = Math.min(minZ, vertex.z);
                        maxX = Math.max(maxX, vertex.x);
                        maxY = Math.max(maxY, vertex.y);
                        maxZ = Math.max(maxZ, vertex.z);
                    }
                    if (!Float.isFinite(minX) || !Float.isFinite(maxY)) return;
                    float hx = Math.max(0.02f, (maxX - minX) * 0.5f);
                    float hy = Math.max(0.02f, (maxY - minY) * 0.5f);
                    float hz = Math.max(0.02f, (maxZ - minZ) * 0.5f);
                    writeProxyBox((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5,
                            hx, hy, hz, cr, cg, cb);
                    emitted[0]++;
                });
            }
            return emitted[0];
        } catch (Throwable ignored) {
            return 0;
        }
    }

    private void writeProxyBox(double x, double y, double z, float hx, float hy, float hz,
                               float cr, float cg, float cb) {
        writeProxyBox(x, y, z, hx, hy, hz, cr, cg, cb, MAT_ENTITY_PROXY);
    }

    private void writeProxyBox(double x, double y, double z, float hx, float hy, float hz,
                               float cr, float cg, float cb, int material) {
        boneBuffer.put((float) x);
        boneBuffer.put((float) y);
        boneBuffer.put((float) z);
        boneBuffer.put((float) material);
        boneBuffer.put(1.0f).put(0.0f).put(0.0f).put(hx);
        boneBuffer.put(0.0f).put(1.0f).put(0.0f).put(hy);
        boneBuffer.put(0.0f).put(0.0f).put(1.0f).put(hz);
        boneBuffer.put(cr);
        boneBuffer.put(cg);
        boneBuffer.put(cb);
        boneBuffer.put(1.0f);
    }

    @SuppressWarnings("unchecked")
    private void captureParticles(Minecraft mc) {
        if (mc.particleEngine == null) return;
        Map<ParticleRenderType, Queue<Particle>> map;
        try {
            map = ((ParticleEngineAccessor) (Object) mc.particleEngine).yaoguang$getParticles();
        } catch (Throwable t) {
            return;
        }
        if (map == null) return;

        float partialTick = mc.getFrameTime();
        int captured = 0;
        for (Map.Entry<ParticleRenderType, Queue<Particle>> entry : map.entrySet()) {
            Queue<Particle> queue = entry.getValue();
            if (queue == null) continue;
            for (Particle particle : queue) {
                if (particle == null) continue;
                ParticleAccessor accessor = (ParticleAccessor) particle;
                double px = Mth.lerp(partialTick, accessor.yaoguang$getXo(), accessor.yaoguang$getX());
                double py = Mth.lerp(partialTick, accessor.yaoguang$getYo(), accessor.yaoguang$getY());
                double pz = Mth.lerp(partialTick, accessor.yaoguang$getZo(), accessor.yaoguang$getZ());
                int age = accessor.yaoguang$getAge();
                int life = accessor.yaoguang$getLifetime();
                float fade = life > 0 ? Math.max(0.0f, 1.0f - (float) age / (float) life) : 1.0f;
                float size = Math.max(accessor.yaoguang$getBbWidth(), accessor.yaoguang$getBbHeight());
                onParticleRendered(px, py, pz,
                        Math.max(0.015f, Math.min(0.18f, size * 0.22f)),
                        accessor.yaoguang$getRCol(),
                        accessor.yaoguang$getGCol(),
                        accessor.yaoguang$getBCol(),
                        accessor.yaoguang$getAlpha() * fade);
                captured++;
                if (captured >= MAX_PARTICLES) return;
            }
        }
        particleBuffer.clear();
        int uploaded = 0;
        Vec3 camera = mc.gameRenderer.getMainCamera().getPosition();
        ParticleSnapshot snapshot;
        while ((snapshot = particleQueue.poll()) != null) {
            if (uploaded >= MAX_PARTICLES) continue;
            double dx = snapshot.x - camera.x;
            double dy = snapshot.y - camera.y;
            double dz = snapshot.z - camera.z;
            if (dx * dx + dy * dy + dz * dz > 128.0 * 128.0) continue;
            particleBuffer.put((float) snapshot.x);
            particleBuffer.put((float) snapshot.y);
            particleBuffer.put((float) snapshot.z);
            particleBuffer.put(snapshot.size);
            particleBuffer.put(snapshot.red);
            particleBuffer.put(snapshot.green);
            particleBuffer.put(snapshot.blue);
            particleBuffer.put(snapshot.alpha);
            uploaded++;
        }
        particleBuffer.flip();
        try {
            if (uploaded > 0) {
                YaoguangNative.uploadParticles(particleBuffer, uploaded);
            } else {
                YaoguangNative.clearParticles();
            }
        } catch (Throwable ignored) {
        }

        if (particleCaptureLog < 3 && captured > 0) {
            particleCaptureLog++;
            log("[Yaoguang] particles captured: " + captured);
        }
    }

    private void drainEntityQueues(Minecraft mc) {
        Vec3 camera = mc.gameRenderer.getMainCamera().getPosition();
        double maxDistanceSq = 128.0 * 128.0;

        boneBuffer.clear();
        int boxes = 0;
        if (boneCount > 0) {
            for (int i = 0; i < boneCount && boxes < MAX_BONES; i++) {
                int base = i * BONE_FLOATS;
                float x = boneData[base];
                float y = boneData[base + 1];
                float z = boneData[base + 2];
                double dx = x - camera.x;
                double dy = y - camera.y;
                double dz = z - camera.z;
                if (dx * dx + dy * dy + dz * dz > maxDistanceSq) continue;
                boneBuffer.put(boneData, base, BONE_FLOATS);
                boxes++;
            }
        }
        boneCount = 0;

        EntitySnapshot snapshot;
        while ((snapshot = entityQueue.poll()) != null) {
            if (boxes >= MAX_BONES) continue;
            double dx = snapshot.x - camera.x;
            double dy = snapshot.y - camera.y;
            double dz = snapshot.z - camera.z;
            if (dx * dx + dy * dy + dz * dz > maxDistanceSq) continue;

            boneBuffer.put((float) snapshot.x);
            boneBuffer.put((float) snapshot.y);
            boneBuffer.put((float) snapshot.z);
            boneBuffer.put((float) snapshot.material);
            boneBuffer.put(1.0f).put(0.0f).put(0.0f).put(snapshot.halfX);
            boneBuffer.put(0.0f).put(1.0f).put(0.0f).put(snapshot.halfY);
            boneBuffer.put(0.0f).put(0.0f).put(1.0f).put(snapshot.halfZ);
            boneBuffer.put(snapshot.red);
            boneBuffer.put(snapshot.green);
            boneBuffer.put(snapshot.blue);
            boneBuffer.put(1.0f);
            boxes++;
        }
        boneBuffer.flip();
        if (boxes > 0) {
            proxyLogCounter++;
            if ((proxyLogCounter % 600) == 0) {
                float maxAbs = 0.0f;
                for (int i = 0; i < boxes * BONE_FLOATS; i++) {
                    float v = Math.abs(boneBuffer.get(i));
                    if (v > maxAbs) maxAbs = v;
                }
                boneBuffer.position(boxes * BONE_FLOATS);
                log("[Yaoguang] proxy boxes=" + boxes + " maxAbs=" + maxAbs);
            }
            try {
                YaoguangNative.uploadDynamic(boneBuffer, boxes);
            } catch (Throwable ignored) {
            }
        } else {
            try {
                YaoguangNative.clearDynamic();
            } catch (Throwable ignored) {
            }
        }

        particleBuffer.clear();
        int particles = 0;
        ParticleSnapshot particle;
        while ((particle = particleQueue.poll()) != null) {
            if (particles >= MAX_PARTICLES) continue;
            double dx = particle.x - camera.x;
            double dy = particle.y - camera.y;
            double dz = particle.z - camera.z;
            if (dx * dx + dy * dy + dz * dz > maxDistanceSq) continue;

            particleBuffer.put((float) particle.x);
            particleBuffer.put((float) particle.y);
            particleBuffer.put((float) particle.z);
            particleBuffer.put(Math.max(0.02f, particle.size));
            particleBuffer.put(particle.red);
            particleBuffer.put(particle.green);
            particleBuffer.put(particle.blue);
            particleBuffer.put(particle.alpha);
            particles++;
        }
        particleBuffer.flip();
        if (particles > 0) {
            try {
                YaoguangNative.uploadParticles(particleBuffer, particles);
            } catch (Throwable ignored) {
            }
        } else {
            try {
                YaoguangNative.clearParticles();
            } catch (Throwable ignored) {
            }
        }

        if (frameCount <= 30 && boxes > 0) {
            log("[Yaoguang] bones sent: " + boxes);
        }
    }

    private void updateEnvironment(Minecraft mc, Vec3 cameraPos, float partialTick, long now) {
        ClientLevel level = mc.level;
        if (level == null) return;

        float skyR = 0.55f;
        float skyG = 0.75f;
        float skyB = 1.0f;
        float cloudR = 1.0f;
        float cloudG = 1.0f;
        float cloudB = 1.0f;
        float flash = 0.0f;

        try {
            Vec3 sky = level.getSkyColor(cameraPos, partialTick);
            skyR = (float) sky.x;
            skyG = (float) sky.y;
            skyB = (float) sky.z;
            Vec3 cloud = level.getCloudColor(partialTick);
            cloudR = (float) cloud.x;
            cloudG = (float) cloud.y;
            cloudB = (float) cloud.z;
            int flashTime = level.getSkyFlashTime();
            if (flashTime > 0) {
                float f = Math.min(1.0f, flashTime - partialTick);
                flash = f * 0.85f;
            }
        } catch (Throwable ignored) {
        }

        lastSkyR = skyR;
        lastSkyG = skyG;
        lastSkyB = skyB;
        lastCloudR = cloudR;
        lastCloudG = cloudG;
        lastCloudB = cloudB;
        lastFlash = flash;

        long dayTime = level.getDayTime();
        String timeOverride = System.getenv("YAOGUANG_TIME");
        if (timeOverride != null) {
            try {
                dayTime = Long.parseLong(timeOverride);
            } catch (NumberFormatException ignored) {
            }
        }
        double phase = ((dayTime % 24000L) / 24000.0) * (Math.PI * 2.0);
        float sx = (float) Math.cos(phase);
        float sy = (float) Math.sin(phase);
        float sz = 0.18f;
        float length = (float) Math.sqrt(sx * sx + sy * sy + sz * sz);
        if (length < 1.0e-6f) length = 1.0f;
        sx /= length;
        sy /= length;
        sz /= length;

        float rain = 0.0f;
        float thunder = 0.0f;
        float snow = 0.0f;
        try {
            rain = FORCE_RAIN ? 1.0f : level.getRainLevel(partialTick);
            thunder = level.getThunderLevel(partialTick);
            if (rain > 0.02f && mc.player != null) {
                net.minecraft.core.BlockPos probe = mc.player.blockPosition();
                net.minecraft.world.level.biome.Biome biome = level.getBiome(probe).value();
                if (biome.getBaseTemperature() < 0.15f || biome.coldEnoughToSnow(probe)) {
                    snow = rain;
                    rain = 0.0f;
                }
            }
        } catch (Throwable ignored) {
        }
        if (System.getenv("YAOGUANG_CLEAR") != null) {
            rain = 0.0f;
            thunder = 0.0f;
        }
        boolean overworld = level.dimension() == net.minecraft.world.level.Level.OVERWORLD;
        if (!overworld) {
            rain = 0.0f;
            thunder = 0.0f;
        }

        float sun = Math.max(0.0f, sy) * 5.5f + 0.85f;
        sun *= (1.0f - rain * 0.65f - thunder * 0.30f);
        if (sun < 0.03f) sun = 0.03f;
        VramExtender.setNight(sy < 0.02f);

        if (now - lastWeatherSample > 250L) {
            lastWeatherSample = now;
            int chunks = Math.max(2, Math.min(32, mc.options.renderDistance().get()));
            float sceneLength = Math.max(80.0f, chunks * 16.0f + 48.0f);
            float rayLength = Math.max(YaoguangConfig.getRayLength(), sceneLength);
            float nightFactor = Math.max(0.0f, Math.min(1.0f, -sy * 2.0f));
            float animationTime = (float) ((System.nanoTime() % 3_600_000_000_000L) / 1.0e9);
            int dimId = level.dimension() == net.minecraft.world.level.Level.OVERWORLD ? 0
                    : (level.dimension() == net.minecraft.world.level.Level.NETHER ? 1 : 2);
            try { YaoguangNative.setEnvironment(skyR, skyG, skyB, cloudR, cloudG, cloudB, flash, animationTime); }
            catch (Throwable e) { warnOnce("setEnvironment", e); }
            try { YaoguangNative.setSun(sx, sy, sz, sun); }
            catch (Throwable e) { warnOnce("setSun", e); }
            try { YaoguangNative.setExposure(YaoguangConfig.getExposure() * (1.0f + 2.1f * nightFactor)); }
            catch (Throwable e) { warnOnce("setExposure", e); }
            try { YaoguangNative.setDimension(dimId); }
            catch (Throwable e) { warnOnce("setDimension", e); }
            try { YaoguangNative.setRayLength(rayLength); }
            catch (Throwable e) { warnOnce("setRayLength", e); }
            try { YaoguangNative.setWeather(rain, thunder, snow); }
            catch (Throwable e) { warnOnce("setWeather", e); }
        }

        if (frameCount <= 3 || frameCount % 120 == 0) {
            float animNow = (float) ((System.nanoTime() % 3_600_000_000_000L) / 1.0e9);
            log("[Yaoguang] env anim=" + animNow + " dayTime=" + (dayTime % 24000L)
                    + " rain=" + rain + " sunDir=" + sx + "," + sy + "," + sz
                    + " sun=" + sun + " sky=" + skyR + "," + skyG + "," + skyB);
        }
    }

    public void endFrame() {
        if (!sceneValid) return;
        try {
            long imageHandle = YaoguangNative.getSharedHandle();
            int width = YaoguangNative.getOutputWidth();
            int height = YaoguangNative.getOutputHeight();
            if (imageHandle != 0L && width > 0 && height > 0) {
                CompositeRenderer.importSharedResources(imageHandle, width, height);
                CompositeRenderer.render(width, height);
            }
        } catch (Throwable t) {
            log("[Yaoguang] endFrame error: " + t);
            t.printStackTrace();
        }
    }

    private void ensureBlockAtlasUploaded(Minecraft mc) {
        try {
            var texture = mc.getTextureManager().getTexture(InventoryMenu.BLOCK_ATLAS);
            if (texture == null) return;
            int id = texture.getId();
            uploadAtlas(id, "Block atlas");
            blockAtlasUploaded = true;
            lastBlockAtlasId = id;
        } catch (Throwable t) {
            log("[Yaoguang] block atlas upload failed: " + t);
        }
    }

    private void uploadAtlas(int textureId, String name) {
        int previousFbo = GL11.glGetInteger(GL30.GL_FRAMEBUFFER_BINDING);
        int previousTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, textureId);
        int width = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);
        int height = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_HEIGHT);
        if (width <= 0 || height <= 0) {
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, previousTexture);
            return;
        }

        int fbo = GL30.glGenFramebuffers();
        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, fbo);
        GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                GL11.GL_TEXTURE_2D, textureId, 0);

        if (GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER) != GL30.GL_FRAMEBUFFER_COMPLETE) {
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, previousFbo);
            GL30.glDeleteFramebuffers(fbo);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, previousTexture);
            return;
        }

        ByteBuffer raw = BufferUtils.createByteBuffer(width * height * 4);
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 4);
        GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, raw);
        raw.position(0);
        raw.limit(width * height * 4);

        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, previousFbo);
        GL30.glDeleteFramebuffers(fbo);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, previousTexture);

        boolean firstUpload = !blockAtlasUploaded;
        if (firstUpload) {
            YaoguangNative.uploadBlockAtlas(width, height, raw);
            log("[Yaoguang] " + name + " uploaded " + width + "x" + height);
        } else {
            YaoguangNative.uploadBlockAtlasLevel0(width, height, raw);
        }
    }

    public static int entityColor(Entity entity) {
        try {
            String id = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE
                    .getKey(entity.getType()).getPath();
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
        } catch (Throwable ignored) {
        }
        return 0xA0A0A0;
    }

    public static int entityMaterial(Entity entity) {
        try {
            String id = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE
                    .getKey(entity.getType()).getPath();
            if (id.contains("blaze") || id.contains("magma") || id.contains("fire")) return MAT_EMISSIVE;
            if (id.contains("slime")) return MAT_GLASS;
        } catch (Throwable ignored) {
        }
        return MAT_DIFFUSE;
    }

    public static int materialDiffuse() {
        return MAT_DIFFUSE;
    }

    public static int materialMetal() {
        return MAT_METAL;
    }

    public static int materialGlass() {
        return MAT_GLASS;
    }

    public static int materialWater() {
        return MAT_WATER;
    }

    public static int materialEmissive() {
        return MAT_EMISSIVE;
    }
}
