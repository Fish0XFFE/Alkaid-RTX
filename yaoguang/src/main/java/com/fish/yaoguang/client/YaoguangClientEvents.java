package com.fish.yaoguang.client;

import com.fish.yaoguang.nativebridge.NativeLoader;
import com.fish.yaoguang.nativebridge.YaoguangNative;

import java.nio.file.Path;

public final class YaoguangClientEvents {

    private static volatile boolean initialized = false;
    private static volatile boolean initFailed = false;

    public static void register() {
    }

    public static synchronized boolean ensureInitialized() {
        if (initialized) return true;
        if (initFailed) return false;
        try {
            Path dir = NativeLoader.getNativeDir();
            if (dir != null) {
                String abs = dir.toAbsolutePath().toString();
                System.out.println("[Yaoguang] Passing asset dir: " + abs);
                YaoguangNative.setAssetDir(abs);
            } else {
                System.out.println("[Yaoguang] getNativeDir() returned null");
            }

            boolean ok = YaoguangNative.init();
            if (ok) {
                initialized = true;
                return true;
            }
            initFailed = true;
            return false;
        } catch (Throwable t) {
            t.printStackTrace();
            initFailed = true;
            return false;
        }
    }

    public static boolean isNativeAvailable() {
        if (!initialized) return false;
        try {
            return YaoguangNative.isRayTracingSupported();
        } catch (Throwable t) {
            return false;
        }
    }

    public static String getRayTracingUnitName() {
        if (!initialized) return "未初始化";
        try {
            int unit = YaoguangNative.getRayTracingUnit();
            switch (unit) {
                case 1: return "NVIDIA RT Core";
                case 2: return "AMD Ray Accelerator";
                case 3: return "Intel RTU";
                default: return "未知";
            }
        } catch (Throwable t) {
            return "未知";
        }
    }

    public static String getDeviceName() {
        if (!initialized) return "未初始化";
        try {
            return YaoguangNative.getDeviceName();
        } catch (Throwable t) {
            return "Unknown";
        }
    }

    private YaoguangClientEvents() {}
}