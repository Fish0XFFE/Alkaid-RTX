package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.client.RayTracingRenderer;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.Camera;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleEngine;
import net.minecraft.client.particle.ParticleRenderType;
import net.minecraft.client.renderer.LightTexture;
import net.minecraft.client.renderer.MultiBufferSource;
import net.minecraft.util.Mth;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.Map;
import java.util.Queue;

@Mixin(ParticleEngine.class)
public abstract class MixinParticleEngine {

    @Shadow
    private Map<ParticleRenderType, Queue<Particle>> particles;

    private static int dbgCount = 0;

    @Inject(method = "render", at = @At("HEAD"), remap = true)
    private void yaoguang$captureParticles(PoseStack poseStack,
                                           MultiBufferSource.BufferSource bufferSource,
                                           LightTexture lightTexture,
                                           Camera camera,
                                           float partialTick,
                                           CallbackInfo ci) {
        try {
            if (particles == null) return;
            int total = 0;
            for (Map.Entry<ParticleRenderType, Queue<Particle>> entry : particles.entrySet()) {
                Queue<Particle> queue = entry.getValue();
                if (queue == null) continue;
                for (Particle p : queue) {
                    if (p == null) continue;
                    ParticleAccessor acc = (ParticleAccessor) p;
                    double px = Mth.lerp(partialTick, acc.yaoguang$getXo(), acc.yaoguang$getX());
                    double py = Mth.lerp(partialTick, acc.yaoguang$getYo(), acc.yaoguang$getY());
                    double pz = Mth.lerp(partialTick, acc.yaoguang$getZo(), acc.yaoguang$getZ());
                    int age = acc.yaoguang$getAge();
                    int life = acc.yaoguang$getLifetime();
                    float alpha = life > 0 ? Math.max(0.0f, 1.0f - (float) age / (float) life) : 1.0f;
                    RayTracingRenderer.INSTANCE.onParticleRendered(
                            px, py, pz,
                            0.12f,
                            acc.yaoguang$getRCol(),
                            acc.yaoguang$getGCol(),
                            acc.yaoguang$getBCol(),
                            alpha,
                            age,
                            life
                    );
                    total++;
                }
            }
            if (dbgCount < 5 && total > 0) {
                dbgCount++;
                System.out.println("[Yaoguang] MixinParticleEngine: " + total + " particles");
            }
        } catch (Throwable t) {
            if (dbgCount < 5) {
                dbgCount++;
                System.out.println("[Yaoguang] particle mixin err: " + t);
            }
        }
    }
}