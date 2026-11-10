package com.fish.yaoguang.client;

import com.fish.yaoguang.YaoguangConfig;
import com.fish.yaoguang.nativebridge.YaoguangNative;
import com.fish.yaoguang.util.VramExtender;
import net.minecraftforge.api.distmarker.Dist;
import net.minecraftforge.client.event.RenderLevelStageEvent;
import net.minecraftforge.event.GameShuttingDownEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

@Mod.EventBusSubscriber(modid = "yaoguang", bus = Mod.EventBusSubscriber.Bus.FORGE, value = Dist.CLIENT)
public class YaoguangRenderHook {

    private static boolean warned = false;

    @SubscribeEvent
    public static void onClientStopping(GameShuttingDownEvent event) {
        try {
            CompositeRenderer.requestShutdown();
            CompositeRenderer.tickShutdown();
        } catch (Throwable ignored) {
        }
        try {
            VramExtender.disableExtension();
        } catch (Throwable ignored) {
        }
        try {
            YaoguangNative.shutdown();
        } catch (Throwable ignored) {
        }
    }

    @SubscribeEvent
    public static void onRenderStage(RenderLevelStageEvent event) {
        if (event.getStage() != RenderLevelStageEvent.Stage.AFTER_LEVEL) return;
        if (!YaoguangConfig.isRayTracingEnabled()) return;
        if (!YaoguangClientEvents.ensureInitialized()) return;
        try {
            RayTracingRenderer.INSTANCE.beginFrame();
            RayTracingRenderer.INSTANCE.endFrame();
            if (RayTracingRenderer.INSTANCE.isRendering()) {
                VanillaOverlayRenderer.render();
            }
        } catch (Throwable t) {
            if (!warned) {
                warned = true;
                System.out.println("[Yaoguang] render hook failed: " + t);
                t.printStackTrace();
            }
        }
    }
}