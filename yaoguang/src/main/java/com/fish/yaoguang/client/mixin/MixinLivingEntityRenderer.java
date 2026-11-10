package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.client.SkeletonCapture;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.renderer.MultiBufferSource;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LivingEntityRenderer.class)
public class MixinLivingEntityRenderer {

    @Inject(method = "render", at = @At("HEAD"), remap = true)
    private void yaoguang$beginSkeleton(LivingEntity entity, float yaw, float partialTick,
                                        PoseStack pose, MultiBufferSource buffer, int light,
                                        CallbackInfo ci) {
        SkeletonCapture.begin(entity, pose);
    }

    @Inject(method = "render", at = @At("TAIL"), remap = true)
    private void yaoguang$endSkeleton(LivingEntity entity, float yaw, float partialTick,
                                      PoseStack pose, MultiBufferSource buffer, int light,
                                      CallbackInfo ci) {
        SkeletonCapture.end();
    }
}
