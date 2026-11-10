package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.client.SkeletonCapture;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexConsumer;
import net.minecraft.client.model.geom.ModelPart;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ModelPart.class)
public class MixinModelPart {

    @Inject(method = "render(Lcom/mojang/blaze3d/vertex/PoseStack;Lcom/mojang/blaze3d/vertex/VertexConsumer;IIFFFF)V",
            at = @At("HEAD"))
    private void yaoguang$capturePart(PoseStack pose, VertexConsumer consumer, int light, int overlay,
                                      float red, float green, float blue, float alpha, CallbackInfo ci) {
        SkeletonCapture.capture((ModelPart) (Object) this, pose, alpha);
    }
}
