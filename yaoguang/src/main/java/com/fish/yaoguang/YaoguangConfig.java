package com.fish.yaoguang;

import com.fish.yaoguang.client.gui.RayTracingPreset;
import com.fish.yaoguang.nativebridge.YaoguangNative;

public final class YaoguangConfig {
    private static boolean rayTracingEnabled = System.getenv("YAOGUANG_FORCE_RT") != null;
    private static RayTracingPreset currentPreset = RayTracingPreset.MEDIUM;
    private static int maxBounces = envInt("YAOGUANG_BOUNCES", 3);
    private static int samplesPerPixel = envInt("YAOGUANG_SPP", 1);
    private static float rayLength = 64.0f;
    private static boolean denoiserEnabled = true;
    private static float exposure = 1.0f;
    private static boolean pathTracingEnabled = true;
    private static boolean vramExtensionEnabled = false;
    private static int vramExtensionSize = 16;
    private static int renderScale = 100;
    private static int dlssQuality = 1;

    private static int envInt(String key, int fallback) {
        try {
            String v = System.getenv(key);
            if (v != null) return Integer.parseInt(v.trim());
        } catch (Throwable ignored) {
        }
        return fallback;
    }

    public static void init() {
        setCurrentPreset(currentPreset);
    }

    public static void applyAll() {
        YaoguangNative.setRayTracingEnabled(rayTracingEnabled);
        YaoguangNative.setMaxBounces(maxBounces);
        YaoguangNative.setSamplesPerPixel(samplesPerPixel);
        YaoguangNative.setRayLength(rayLength);
        YaoguangNative.setDenoiserEnabled(denoiserEnabled);
        YaoguangNative.setExposure(exposure);
        YaoguangNative.setPathTracingEnabled(pathTracingEnabled);
        YaoguangNative.setDlssQuality(dlssQuality);
    }

    public static boolean isRayTracingEnabled() { return rayTracingEnabled; }

    public static void setRayTracingEnabled(boolean v) {
        rayTracingEnabled = v;
        System.out.println("[Yaoguang] RT " + (v ? "enabled" : "disabled") + " by user");
        if (!com.fish.yaoguang.client.YaoguangClientEvents.isInitialized()) {
            System.out.println("[Yaoguang] RT will initialize on world entry");
            return;
        }
        if (v) {
            YaoguangNative.setDenoiserEnabled(denoiserEnabled);
            YaoguangNative.setPathTracingEnabled(pathTracingEnabled);
            YaoguangNative.setMaxBounces(maxBounces);
            YaoguangNative.setSamplesPerPixel(samplesPerPixel);
        }
        YaoguangNative.setRayTracingEnabled(v);
    }

    public static RayTracingPreset getCurrentPreset() { return currentPreset; }

    public static void setCurrentPreset(RayTracingPreset preset) {
        currentPreset = preset;
        maxBounces = envInt("YAOGUANG_BOUNCES", preset.getMaxBounces());
        samplesPerPixel = envInt("YAOGUANG_SPP", preset.getSamplesPerPixel());
        denoiserEnabled = preset.isDenoiser();
        exposure = preset.getExposureMultiplier();
        pathTracingEnabled = preset.isPathTracing();
        switch (preset) {
            case LOW -> renderScale = 85;
            case MEDIUM, HIGH, ULTRA, CINEMATIC -> renderScale = 100;
            default -> {
            }
        }
        YaoguangNative.applyPreset(preset.ordinal());
    }

    public static void markCustom() {
        if (currentPreset != RayTracingPreset.CUSTOM) {
            currentPreset = RayTracingPreset.CUSTOM;
        }
    }

    public static int getMaxBounces() { return maxBounces; }

    public static void setMaxBounces(int v) {
        maxBounces = v;
        markCustom();
        YaoguangNative.setMaxBounces(v);
    }

    public static int getSamplesPerPixel() { return samplesPerPixel; }

    public static void setSamplesPerPixel(int v) {
        samplesPerPixel = v;
        markCustom();
        YaoguangNative.setSamplesPerPixel(v);
    }

    public static float getRayLength() { return rayLength; }

    public static void setRayLength(float v) {
        rayLength = v;
        markCustom();
        YaoguangNative.setRayLength(v);
    }

    public static boolean isDenoiserEnabled() { return denoiserEnabled; }

    public static void setDenoiserEnabled(boolean v) {
        denoiserEnabled = v;
        markCustom();
        YaoguangNative.setDenoiserEnabled(v);
    }

    public static float getExposure() { return exposure; }

    public static void setExposure(float v) {
        exposure = v;
        markCustom();
        YaoguangNative.setExposure(v);
    }

    public static boolean isPathTracingEnabled() { return pathTracingEnabled; }

    public static void setPathTracingEnabled(boolean v) {
        pathTracingEnabled = v;
        markCustom();
        YaoguangNative.setPathTracingEnabled(v);
    }

    public static boolean isVramExtensionEnabled() { return vramExtensionEnabled; }

    public static void setVramExtensionEnabled(boolean v) {
        vramExtensionEnabled = v;
    }

    public static int getRenderScale() { return renderScale; }

    public static void setRenderScale(int value) {
        if (value < 50) value = 50;
        if (value > 100) value = 100;
        renderScale = value;
    }

    public static int getDlssQuality() { return dlssQuality; }

    public static void setDlssQuality(int value) {
        if (value < 0) value = 0;
        if (value > 5) value = 5;
        dlssQuality = value;
        YaoguangNative.setDlssQuality(value);
    }

    public static String getDlssQualityName() {
        return switch (dlssQuality) {
            case 1 -> "质量";
            case 2 -> "平衡";
            case 3 -> "性能";
            case 4 -> "超级性能";
            case 5 -> "关闭";
            default -> "自动";
        };
    }

    public static int getVramExtensionSize() { return vramExtensionSize; }

    public static void setVramExtensionSize(int gb) {
        if (gb < 8) gb = 8;
        if (gb > 32) gb = 32;
        vramExtensionSize = gb;
    }
}