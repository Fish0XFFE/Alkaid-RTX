package com.fish.yaoguang.client;

import net.minecraftforge.api.distmarker.Dist;
import net.minecraftforge.event.level.BlockEvent;
import net.minecraftforge.event.level.ChunkEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
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
    private static volatile ScheduledFuture<?> pendingChunkMark;

    @SubscribeEvent
    public static void onPlace(BlockEvent.EntityPlaceEvent event) {
        markDirtyAround(event.getPos());
        scheduleUrgent();
    }

    @SubscribeEvent
    public static void onBreak(BlockEvent.BreakEvent event) {
        markDirtyAround(event.getPos());
        scheduleUrgent();
    }

    private static void markDirtyAround(net.minecraft.core.BlockPos pos) {
        if (pos == null) return;
        int cx = pos.getX() >> 4;
        int cz = pos.getZ() >> 4;
        try {
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    RayTracingRenderer.INSTANCE.markChunkDirty(cx + dx, cz + dz);
                }
            }
        } catch (Throwable ignored) {
        }
    }

    @SubscribeEvent
    public static void onChunkLoad(ChunkEvent.Load event) {
    }

    @SubscribeEvent
    public static void onChunkUnload(ChunkEvent.Unload event) {
    }

    private static void scheduleChunkMark() {
        ScheduledFuture<?> old = pendingChunkMark;
        if (old != null) old.cancel(false);
        pendingChunkMark = SCHEDULER.schedule(() -> {
            try {
                RayTracingRenderer.INSTANCE.markSceneDirty();
            } catch (Throwable ignored) {
            }
        }, 1200, TimeUnit.MILLISECONDS);
    }

    private static void scheduleUrgent() {
        long now = System.currentTimeMillis();
        long last = LAST_MS.get();
        if (now - last < 120) return;
        LAST_MS.set(now);

        try {
            RayTracingRenderer.INSTANCE.markSceneDirtyUrgent();
        } catch (Throwable ignored) {}

        SCHEDULER.schedule(() -> {
            try {
                RayTracingRenderer.INSTANCE.markSceneDirtyUrgent();
            } catch (Throwable ignored) {}
        }, 250, TimeUnit.MILLISECONDS);
    }
}