package com.fish.yaoguang.client;

import com.fish.yaoguang.YaoguangConfig;
import net.minecraftforge.api.distmarker.Dist;
import net.minecraftforge.client.event.RenderLevelStageEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

@Mod.EventBusSubscriber(modid = "yaoguang", bus = Mod.EventBusSubscriber.Bus.FORGE, value = Dist.CLIENT)
public class YaoguangRenderHook {

    private static boolean warned = false;

    @SubscribeEvent
    public static void onRenderStage(RenderLevelStageEvent event) {
        if (event.getStage() != RenderLevelStageEvent.Stage.AFTER_LEVEL) return;
        if (!YaoguangConfig.isRayTracingEnabled()) return;
        try {
            RayTracingRenderer.INSTANCE.beginFrame();
            RayTracingRenderer.INSTANCE.endFrame();
        } catch (Throwable t) {
            if (!warned) {
                warned = true;
                System.out.println("[Yaoguang] render hook failed: " + t);
                t.printStackTrace();
            }
        }
    }
}