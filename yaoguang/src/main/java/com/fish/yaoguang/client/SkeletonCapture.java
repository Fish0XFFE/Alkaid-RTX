package com.fish.yaoguang.client;

import com.fish.yaoguang.client.mixin.ModelPartAccessor;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.Minecraft;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.LivingEntity;
import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector3f;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;

public final class SkeletonCapture {
    private static final Deque<Context> STACK = new ArrayDeque<>();
    private static final double MAX_DISTANCE_SQ = 128.0 * 128.0;
    private static int debugCount = 0;

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static ResourceLocation textureOf(LivingEntity entity) {
        EntityRenderer renderer = Minecraft.getInstance().getEntityRenderDispatcher().getRenderer(entity);
        if (renderer instanceof LivingEntityRenderer living) {
            return living.getTextureLocation(entity);
        }
        return null;
    }

    private static final class Context {
        final LivingEntity entity;
        final PoseStack pose;
        final float red;
        final float green;
        final float blue;
        final int material;
        final boolean active;
        final float offsetX;
        final float offsetY;
        final float offsetZ;
        int boxes;

        Context(LivingEntity entity, PoseStack pose, float red, float green, float blue,
                int material, boolean active, float offsetX, float offsetY, float offsetZ) {
            this.entity = entity;
            this.pose = pose;
            this.red = red;
            this.green = green;
            this.blue = blue;
            this.material = material;
            this.active = active;
            this.offsetX = offsetX;
            this.offsetY = offsetY;
            this.offsetZ = offsetZ;
        }
    }

    public static void begin(LivingEntity entity, PoseStack pose) {
        var camera = Minecraft.getInstance().gameRenderer.getMainCamera().getPosition();
        if (entity == null || pose == null) {
            STACK.push(new Context(null, pose, 1.0f, 1.0f, 1.0f, 0, false, 0, 0, 0));
            return;
        }
        if (entity.isInvisible()) {
            STACK.push(new Context(entity, pose, 1.0f, 1.0f, 1.0f, 0, false, 0, 0, 0));
            return;
        }
        if (entity == Minecraft.getInstance().player
                && Minecraft.getInstance().options.getCameraType().isFirstPerson()) {
            STACK.push(new Context(entity, pose, 1.0f, 1.0f, 1.0f, 0, false, 0, 0, 0));
            return;
        }

        if (entity.distanceToSqr(camera.x, camera.y, camera.z) > MAX_DISTANCE_SQ) {
            STACK.push(new Context(entity, pose, 1.0f, 1.0f, 1.0f, 0, false, 0, 0, 0));
            return;
        }

        Vector3f root = pose.last().pose().transformPosition(new Vector3f());
        double rx = root.x;
        double ry = root.y;
        double rz = root.z;
        double toRelative = (rx - (entity.getX() - camera.x)) * (rx - (entity.getX() - camera.x))
                + (ry - (entity.getY() - camera.y)) * (ry - (entity.getY() - camera.y))
                + (rz - (entity.getZ() - camera.z)) * (rz - (entity.getZ() - camera.z));
        double toWorld = (rx - entity.getX()) * (rx - entity.getX())
                + (ry - entity.getY()) * (ry - entity.getY())
                + (rz - entity.getZ()) * (rz - entity.getZ());
        boolean cameraRelative = toRelative <= toWorld;
        if (debugCount < 3) {
            debugCount++;
            System.out.println("[Yaoguang] skeleton space " + entity.getType()
                    + " root=" + rx + "," + ry + "," + rz
                    + " relative=" + cameraRelative
                    + " entity=" + entity.getX() + "," + entity.getY() + "," + entity.getZ());
        }

        int color = RayTracingRenderer.entityColor(entity);
        try {
            ResourceLocation texture = textureOf(entity);
            if (texture != null) color = EntityTextureColors.average(texture);
        } catch (Throwable ignored) {
        }
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        STACK.push(new Context(entity, pose, r, g, b,
                RayTracingRenderer.entityMaterial(entity), true,
                cameraRelative ? (float) camera.x : 0.0f,
                cameraRelative ? (float) camera.y : 0.0f,
                cameraRelative ? (float) camera.z : 0.0f));
    }

    public static void end() {
        if (STACK.isEmpty()) return;
        Context context = STACK.pop();
        if (context.active && context.boxes < 4 && context.entity != null) {
            RayTracingRenderer.INSTANCE.addEntityBox(context.entity);
        }
    }

    public static void capture(ModelPart part, PoseStack pose, float alpha) {
        Context context = STACK.peek();
        if (context == null || !context.active || context.entity == null
                || part == null || pose == null) return;
        if (!part.visible || part.skipDraw) return;

        List<ModelPart.Cube> cubes = ((ModelPartAccessor) (Object) part).yaoguang$getCubes();
        if (cubes == null || cubes.isEmpty()) return;

        PoseStack transformed = new PoseStack();
        Matrix4f base = transformed.last().pose();
        base.set(pose.last().pose());
        Matrix3f normal = transformed.last().normal();
        normal.set(pose.last().normal());
        part.translateAndRotate(transformed);

        Matrix4f matrix = transformed.last().pose();
        float a = Math.max(0.15f, Math.min(1.0f, alpha));

        for (ModelPart.Cube cube : cubes) {
            float cx = (cube.minX + cube.maxX) * 0.5f / 16.0f;
            float cy = (cube.minY + cube.maxY) * 0.5f / 16.0f;
            float cz = (cube.minZ + cube.maxZ) * 0.5f / 16.0f;
            float hx = (cube.maxX - cube.minX) * 0.5f / 16.0f;
            float hy = (cube.maxY - cube.minY) * 0.5f / 16.0f;
            float hz = (cube.maxZ - cube.minZ) * 0.5f / 16.0f;
            if (hx <= 0.0f || hy <= 0.0f || hz <= 0.0f) continue;

            Vector3f center = matrix.transformPosition(new Vector3f(cx, cy, cz));
            Vector3f px = matrix.transformPosition(new Vector3f(cx + hx, cy, cz));
            Vector3f py = matrix.transformPosition(new Vector3f(cx, cy + hy, cz));
            Vector3f pz = matrix.transformPosition(new Vector3f(cx, cy, cz + hz));

            Vector3f ax = px.sub(center, new Vector3f());
            Vector3f ay = py.sub(center, new Vector3f());
            Vector3f az = pz.sub(center, new Vector3f());

            float lx = ax.length();
            float ly = ay.length();
            float lz = az.length();
            if (lx < 1.0e-5f || ly < 1.0e-5f || lz < 1.0e-5f) continue;
            ax.div(lx);
            ay.div(ly);
            az.div(lz);

            float worldX = center.x + context.offsetX;
            float worldY = center.y + context.offsetY;
            float worldZ = center.z + context.offsetZ;
            if (Math.abs(worldX - context.entity.getX()) > 8.0
                    || Math.abs(worldY - context.entity.getY()) > 8.0
                    || Math.abs(worldZ - context.entity.getZ()) > 8.0) continue;

            if (!RayTracingRenderer.INSTANCE.addBoneBox(
                    worldX, worldY, worldZ,
                    context.material,
                    ax.x, ax.y, ax.z, lx,
                    ay.x, ay.y, ay.z, ly,
                    az.x, az.y, az.z, lz,
                    context.red, context.green, context.blue, a)) return;
            context.boxes++;
        }
    }

    private SkeletonCapture() {
    }
}
