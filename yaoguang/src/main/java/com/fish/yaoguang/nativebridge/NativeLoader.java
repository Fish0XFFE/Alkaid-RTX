package com.fish.yaoguang.nativebridge;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

public final class NativeLoader {
    private static boolean loaded = false;
    private static Path nativeDir = null;

    public static synchronized void load() {
        if (loaded) return;

        String os = System.getProperty("os.name").toLowerCase();
        String arch = System.getProperty("os.arch").toLowerCase();
        String libName;

        if (os.contains("win")) {
            libName = "native/windows/x86_64/yaoguang_rt.dll";
        } else if (os.contains("linux")) {
            libName = "native/linux/x86_64/libyaoguang_rt.so";
        } else if (os.contains("mac")) {
            libName = arch.contains("aarch64")
                    ? "native/macos/aarch64/libyaoguang_rt.dylib"
                    : "native/macos/x86_64/libyaoguang_rt.dylib";
        } else {
            throw new UnsatisfiedLinkError("Unsupported OS: " + os);
        }

        try {
            nativeDir = Files.createTempDirectory("yaoguang_native");
            nativeDir.toFile().deleteOnExit();

            String fileName = libName.substring(libName.lastIndexOf('/') + 1);
            Path tmpLib = nativeDir.resolve(fileName);
            try (InputStream is = NativeLoader.class.getClassLoader().getResourceAsStream(libName)) {
                if (is == null) throw new UnsatisfiedLinkError("Native library not found: " + libName);
                Files.copy(is, tmpLib, StandardCopyOption.REPLACE_EXISTING);
            }
            tmpLib.toFile().deleteOnExit();

            Path shaderDir = nativeDir.resolve("shaders");
            Files.createDirectories(shaderDir);

            String[] shaders = {
                    "ray_gen.rgen.spv",
                    "closest_hit.rchit.spv",
                    "miss.rmiss.spv"
            };
            for (String s : shaders) {
                String path = "native/shaders/" + s;
                Path dst = shaderDir.resolve(s);
                try (InputStream is = NativeLoader.class.getClassLoader().getResourceAsStream(path)) {
                    if (is == null) {
                        System.err.println("[Yaoguang] Shader not found in JAR: " + path);
                        continue;
                    }
                    Files.copy(is, dst, StandardCopyOption.REPLACE_EXISTING);
                    dst.toFile().deleteOnExit();
                }
            }

            System.load(tmpLib.toAbsolutePath().toString());
            loaded = true;

            System.out.println("[Yaoguang] Native library loaded from: " + nativeDir);
        } catch (IOException e) {
            throw new RuntimeException("Failed to extract native library", e);
        }
    }

    public static Path getNativeDir() {
        return nativeDir;
    }

    private NativeLoader() {}
}