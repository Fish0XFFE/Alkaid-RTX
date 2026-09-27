package com.fish.yaoguang.client;

import net.minecraftforge.api.distmarker.Dist;
import net.minecraftforge.event.level.BlockEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

@Mod.EventBusSubscriber(modid = "yaoguang", bus = Mod.EventBusSubscriber.Bus.FORGE, value = Dist.CLIENT)
public class YaoguangBlockEvents {

    private static final ScheduledExecutorService SCHEDULER =
            Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "Yaoguang-BlockDebounce");
                t.setDaemon(true);
                t.setPriority(Thread.MIN_PRIORITY);
                return t;
            });

    private static final AtomicLong LAST_MS = new AtomicLong(0);

    @SubscribeEvent
    public static void onPlace(BlockEvent.EntityPlaceEvent event) {
        scheduleMark();
    }

    @SubscribeEvent
    public static void onBreak(BlockEvent.BreakEvent event) {
        scheduleMark();
    }

    private static void scheduleMark() {
        long now = System.currentTimeMillis();
        long last = LAST_MS.get();
        if (now - last < 120) return;
        LAST_MS.set(now);

        try {
            RayTracingRenderer.INSTANCE.markSceneDirty();
        } catch (Throwable ignored) {}

        SCHEDULER.schedule(() -> {
            try {
                RayTracingRenderer.INSTANCE.markSceneDirty();
            } catch (Throwable ignored) {}
        }, 250, TimeUnit.MILLISECONDS);
    }
}