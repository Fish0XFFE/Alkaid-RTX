package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.YaoguangConfig;
import com.fish.yaoguang.client.RayTracingRenderer;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.LightTexture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LevelRenderer.class)
public class MixinLevelRenderer {
    @Inject(method = "setBlockDirty", at = @At("HEAD"), require = 0)
    private void yaoguang$onBlockDirty(net.minecraft.core.BlockPos pos, boolean flag, CallbackInfo ci) {
        if (YaoguangConfig.isRayTracingEnabled()) {
            RayTracingRenderer.INSTANCE.markChunkDirty(pos.getX() >> 4, pos.getZ() >> 4);
        }
    }

    @Inject(method = "renderSnowAndRain", at = @At("HEAD"), cancellable = true, require = 0)
    private void yaoguang$cancelVanillaWeather(LightTexture lightTexture, float partialTick, double camX, double camY, double camZ, CallbackInfo ci) {
        if (YaoguangConfig.isRayTracingEnabled()) ci.cancel();
    }
}
