package com.fish.yaoguang.client.mixin;

import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.LightTexture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(LevelRenderer.class)
public interface LevelRendererInvoker {
    @Invoker("renderSnowAndRain")
    void yaoguang$renderSnowAndRain(LightTexture lightTexture, float partialTick,
                                    double camX, double camY, double camZ);
}
