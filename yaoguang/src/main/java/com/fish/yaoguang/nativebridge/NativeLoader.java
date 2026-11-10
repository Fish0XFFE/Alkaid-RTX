package com.fish.yaoguang.nativebridge;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;

public final class NativeLoader {
    private static boolean loaded = false;
    private static Path nativeDir = null;

    public static synchronized void load() {
        if (loaded) return;
        System.out.println("[Yaoguang] NativeLoader.load begin");

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

            if (os.contains("win")) {
                String dlss = "native/windows/x86_64/nvngx_dlss.dll";
                Path tmpDlss = nativeDir.resolve("nvngx_dlss.dll");
                boolean copied = false;
                try (InputStream is = NativeLoader.class.getClassLoader().getResourceAsStream(dlss)) {
                    if (is != null) {
                        Files.copy(is, tmpDlss, StandardCopyOption.REPLACE_EXISTING);
                        tmpDlss.toFile().deleteOnExit();
                        copied = true;
                        System.out.println("[Yaoguang] nvngx_dlss.dll bundled in jar");
                    }
                }
                if (!copied) {
                    for (Path candidate : dlssCandidates()) {
                        if (!Files.isRegularFile(candidate)) continue;
                        Files.copy(candidate, tmpDlss, StandardCopyOption.REPLACE_EXISTING);
                        tmpDlss.toFile().deleteOnExit();
                        copied = true;
                        System.out.println("[Yaoguang] nvngx_dlss.dll taken from " + candidate);
                        break;
                    }
                }
                if (!copied) {
                    System.out.println("[Yaoguang] nvngx_dlss.dll not found - DLSS disabled, "
                            + "put the file at config/yaoguang/native/nvngx_dlss.dll to enable it");
                }
            }

            Path shaderDir = nativeDir.resolve("shaders");
            Files.createDirectories(shaderDir);

            String[] shaders = {
                    "ray_gen.rgen.spv",
                    "closest_hit.rchit.spv",
                    "alpha_test.rahit.spv",
                    "miss.rmiss.spv",
                    "combine.comp.spv",
                    "composite.comp.spv",
                    "cloud.comp.spv"
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

    private static java.util.List<Path> dlssCandidates() {
        java.util.List<Path> list = new java.util.ArrayList<>();
        String explicit = System.getenv("YAOGUANG_NGX_DLL");
        if (explicit != null && !explicit.isEmpty()) {
            list.add(Paths.get(explicit));
        }
        Path gameDir = null;
        try {
            gameDir = net.minecraftforge.fml.loading.FMLPaths.GAMEDIR.get();
        } catch (Throwable ignored) {
        }
        if (gameDir == null) {
            String userDir = System.getProperty("user.dir");
            if (userDir != null) gameDir = Paths.get(userDir);
        }
        if (gameDir != null) {
            list.add(gameDir.resolve("config").resolve("yaoguang").resolve("native").resolve("nvngx_dlss.dll"));
            list.add(gameDir.resolve("yaoguang").resolve("native").resolve("nvngx_dlss.dll"));
        }
        return list;
    }

    private NativeLoader() {}
}