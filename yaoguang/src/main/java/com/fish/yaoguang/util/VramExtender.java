package com.fish.yaoguang.util;

import com.fish.yaoguang.nativebridge.YaoguangNative;

import java.nio.file.Path;

public final class VramExtender {
    private static int requestedSize = 16;
    private static boolean enabled = false;
    private static boolean night = false;
    private static boolean active = false;
    private static long lastAttemptMs = 0L;

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(VramExtender::disableExtension));
    }

    public static void setGameDir(Path dir) {
    }

    public static boolean enableExtension(int gigabytes) {
        if (gigabytes < 8 || gigabytes > 32) return false;
        requestedSize = gigabytes;
        enabled = true;
        return apply();
    }

    public static void disableExtension() {
        enabled = false;
        active = false;
        try {
            YaoguangNative.setVramExtension(false, 0);
        } catch (Throwable ignored) {
        }
    }

    public static void setNight(boolean isNight) {
        night = isNight;
        if (enabled) apply();
    }

    private static boolean apply() {
        boolean shouldEnable = enabled && night;
        if (shouldEnable == active) return true;
        long now = System.currentTimeMillis();
        if (shouldEnable && !active && now - lastAttemptMs < 30000L) return false;
        lastAttemptMs = now;
        try {
            YaoguangNative.setVramExtension(shouldEnable, shouldEnable ? requestedSize : 0);
            active = shouldEnable;
            return true;
        } catch (Throwable t) {
            active = false;
            return false;
        }
    }

    public static boolean isEnabled() {
        return enabled;
    }

    public static boolean isActive() {
        return active;
    }

    public static long getExtendedSize() {
        return active ? (long) requestedSize * 1024L * 1024L * 1024L : 0L;
    }

    private VramExtender() {
    }
}
