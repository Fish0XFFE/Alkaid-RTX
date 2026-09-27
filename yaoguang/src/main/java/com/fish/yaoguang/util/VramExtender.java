package com.fish.yaoguang.util;

import com.fish.yaoguang.nativebridge.YaoguangNative;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class VramExtender {
    private static final String VRAM_FILE_NAME = "yaoguang_vram.tmp";
    private static RandomAccessFile vramFile;
    private static long extendedSize;
    private static boolean enabled = false;
    private static Path gameDir = null;

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(VramExtender::disableExtension));
    }

    public static void setGameDir(Path dir) {
        gameDir = dir;
    }

    private static Path getVramPath() {
        Path base;
        if (gameDir != null) {
            base = gameDir;
        } else {
            base = Paths.get(System.getProperty("user.home"), ".minecraft");
        }
        return base.resolve(VRAM_FILE_NAME);
    }

    public static boolean enableExtension(int gigabytes) {
        if (gigabytes < 8 || gigabytes > 32) return false;
        if (enabled) disableExtension();

        try {
            Path filePath = getVramPath();
            File file = filePath.toFile();
            File parent = file.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            if (file.exists()) file.delete();

            vramFile = new RandomAccessFile(file, "rw");
            extendedSize = (long) gigabytes * 1024L * 1024L * 1024L;
            vramFile.setLength(extendedSize);

            try {
                YaoguangNative.setVramExtension(true, gigabytes);
            } catch (Throwable ignored) {}

            enabled = true;
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public static void disableExtension() {
        if (vramFile != null) {
            try { vramFile.close(); } catch (IOException ignored) {}
            vramFile = null;
        }

        try {
            YaoguangNative.setVramExtension(false, 0);
        } catch (Throwable ignored) {}

        try {
            Files.deleteIfExists(getVramPath());
        } catch (IOException ignored) {}

        enabled = false;
        extendedSize = 0;
    }

    public static boolean isEnabled() { return enabled; }
    public static long getExtendedSize() { return extendedSize; }
}