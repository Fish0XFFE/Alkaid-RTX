package com.fish.yaoguang.client;

import com.fish.yaoguang.client.mixin.LevelRendererInvoker;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.math.Axis;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.MultiBufferSource;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.entity.EntityRenderDispatcher;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;

public final class VanillaOverlayRenderer {
    private static final double MAX_DISTANCE_SQ = 192.0 * 192.0;

    public static void render() {
        Minecraft mc = Minecraft.getInstance();
        if (mc == null || mc.level == null || mc.player == null) return;
        if (mc.gameRenderer == null) return;

        Camera camera = mc.gameRenderer.getMainCamera();
        Vec3 cam = camera.getPosition();
        float partialTick = mc.getFrameTime();
        Frustum frustum = mc.levelRenderer.getFrustum();
        RenderSystem.getModelViewStack().pushPose();
        try {
        RenderSystem.getModelViewStack().setIdentity();
        RenderSystem.applyModelViewMatrix();
        RenderSystem.enableDepthTest();
        RenderSystem.depthMask(true);
        PoseStack pose = new PoseStack();
        pose.mulPose(Axis.XP.rotationDegrees(camera.getXRot()));
        pose.mulPose(Axis.YP.rotationDegrees(camera.getYRot() + 180.0f));
        MultiBufferSource.BufferSource buffers = mc.renderBuffers().bufferSource();
        EntityRenderDispatcher dispatcher = mc.getEntityRenderDispatcher();
        boolean firstPerson = mc.options.getCameraType().isFirstPerson();
        boolean playerSeen = false;

        for (Entity entity : mc.level.entitiesForRendering()) {
            if (entity == null || entity.isInvisible()) continue;
            if (entity == mc.player) {
                playerSeen = true;
                if (firstPerson) continue;
            }
            if (entity.distanceToSqr(cam.x, cam.y, cam.z) > MAX_DISTANCE_SQ) continue;

            renderEntity(dispatcher, entity, cam, partialTick, pose, buffers);
        }
        if (!firstPerson && !playerSeen && mc.player != null
                && mc.player.distanceToSqr(cam.x, cam.y, cam.z) <= MAX_DISTANCE_SQ) {
            renderEntity(dispatcher, mc.player, cam, partialTick, pose, buffers);
        }
        buffers.endBatch();

        if (System.getenv("YAOGUANG_NO_PARTICLES") == null) {
            PoseStack particlePose = new PoseStack();
            particlePose.mulPose(Axis.XP.rotationDegrees(camera.getXRot()));
            particlePose.mulPose(Axis.YP.rotationDegrees(camera.getYRot() + 180.0f));
            mc.particleEngine.render(particlePose, buffers, mc.gameRenderer.lightTexture(), camera, partialTick, frustum);
            buffers.endBatch();
        }

        if (System.getenv("YAOGUANG_NO_RAIN") == null && mc.level.getRainLevel(partialTick) > 0.01f) {
            PoseStack rainModelView = RenderSystem.getModelViewStack();
            rainModelView.pushPose();
            rainModelView.mulPose(Axis.XP.rotationDegrees(camera.getXRot()));
            rainModelView.mulPose(Axis.YP.rotationDegrees(camera.getYRot() + 180.0f));
            RenderSystem.applyModelViewMatrix();
            try {
                ((LevelRendererInvoker) (Object) mc.levelRenderer)
                        .yaoguang$renderSnowAndRain(mc.gameRenderer.lightTexture(), partialTick,
                                cam.x, cam.y, cam.z);
            } catch (Throwable ignored) {
            }
            rainModelView.popPose();
            RenderSystem.applyModelViewMatrix();
        }

        } finally {
            RenderSystem.getModelViewStack().popPose();
            RenderSystem.applyModelViewMatrix();
        }
    }

    private static void renderEntity(EntityRenderDispatcher dispatcher, Entity entity,
                                     Vec3 cam, float partialTick, PoseStack pose,
                                     MultiBufferSource.BufferSource buffers) {
        double x = Mth.lerp(partialTick, entity.xOld, entity.getX()) - cam.x;
        double y = Mth.lerp(partialTick, entity.yOld, entity.getY()) - cam.y;
        double z = Mth.lerp(partialTick, entity.zOld, entity.getZ()) - cam.z;
        float yaw = Mth.lerp(partialTick, entity.yRotO, entity.getYRot());
        int light = dispatcher.getPackedLightCoords(entity, partialTick);
        dispatcher.render(entity, x, y, z, yaw, partialTick, pose, buffers, light);
    }

    private VanillaOverlayRenderer() {
    }
}

