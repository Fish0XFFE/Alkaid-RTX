package com.fish.yaoguang;

import com.fish.yaoguang.client.gui.RayTracingPreset;
import com.fish.yaoguang.nativebridge.YaoguangNative;

public final class YaoguangConfig {
    private static boolean rayTracingEnabled = false;
    private static RayTracingPreset currentPreset = RayTracingPreset.MEDIUM;
    private static int maxBounces = 2;
    private static int samplesPerPixel = 2;
    private static float rayLength = 64.0f;
    private static boolean denoiserEnabled = true;
    private static float exposure = 1.0f;
    private static boolean pathTracingEnabled = false;
    private static boolean vramExtensionEnabled = false;
    private static int vramExtensionSize = 16;

    public static void init() {}

    public static boolean isRayTracingEnabled() { return rayTracingEnabled; }

    public static void setRayTracingEnabled(boolean v) {
        rayTracingEnabled = v;
        YaoguangNative.setRayTracingEnabled(v);
    }

    public static RayTracingPreset getCurrentPreset() { return currentPreset; }

    public static void setCurrentPreset(RayTracingPreset preset) {
        currentPreset = preset;
        maxBounces = preset.getMaxBounces();
        samplesPerPixel = preset.getSamplesPerPixel();
        denoiserEnabled = preset.isDenoiser();
        exposure = preset.getExposureMultiplier();
        pathTracingEnabled = preset.isPathTracing();
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
        YaoguangNative.setVramExtension(v, vramExtensionSize);
    }

    public static int getVramExtensionSize() { return vramExtensionSize; }

    public static void setVramExtensionSize(int gb) {
        if (gb < 8) gb = 8;
        if (gb > 32) gb = 32;
        vramExtensionSize = gb;
        YaoguangNative.setVramExtension(vramExtensionEnabled, gb);
    }
}