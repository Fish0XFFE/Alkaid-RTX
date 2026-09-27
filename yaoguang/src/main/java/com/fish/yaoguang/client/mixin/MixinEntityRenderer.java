package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.client.RayTracingRenderer;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.renderer.MultiBufferSource;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(EntityRenderer.class)
public class MixinEntityRenderer {

    private static int dbg = 0;

    @Inject(method = "render", at = @At("HEAD"), remap = true)
    private void yaoguang$captureEntity(Entity entity, float entityYaw, float partialTicks,
                                        PoseStack poseStack, MultiBufferSource buffer,
                                        int packedLight, CallbackInfo ci) {
        try {
            RayTracingRenderer.INSTANCE.onEntityRendered(entity, poseStack);
            if (dbg < 3) {
                dbg++;
                System.out.println("[Yaoguang] MixinEntityRenderer triggered: " + entity.getType());
            }
        } catch (Throwable t) {
            if (dbg < 3) { dbg++; System.out.println("[Yaoguang] entity mixin err: " + t); }
        }
    }
}