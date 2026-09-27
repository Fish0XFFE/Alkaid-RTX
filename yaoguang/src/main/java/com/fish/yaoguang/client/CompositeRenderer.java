package com.fish.yaoguang.client;

import net.minecraft.client.Minecraft;
import org.lwjgl.opengl.*;

import java.nio.ByteBuffer;

public final class CompositeRenderer {

    private static int program = 0;
    private static int accumProgram = 0;
    private static int vao = 0;
    private static int vbo = 0;
    private static int sharedTex = 0;
    private static int accumTex = 0;
    private static int accumFbo = 0;

    private static int texWidth = 0;
    private static int texHeight = 0;
    private static int accumTexWidth = 0;
    private static int accumTexHeight = 0;

    private static boolean initialized = false;
    private static boolean initFailed = false;
    private static int frameCount = 0;
    private static int accumFrame = 0;

    private static int locAccumNewFrame = -1;
    private static int locAccumMixAmount = -1;
    private static int locBlitTex = -1;

    private static int importedMemoryObject = 0;
    private static long lastImportedHandle = 0L;

    private static final int MAX_ACCUM = 256;

    private static double lastX, lastY, lastZ;
    private static float lastYaw, lastPitch;

    private static volatile boolean pendingShutdown = false;

    public static void notifyCamera(double x, double y, double z, float yaw, float pitch) {
        boolean moved = accumFrame > 0 && (
                Math.abs(x - lastX) > 0.02 || Math.abs(y - lastY) > 0.02 || Math.abs(z - lastZ) > 0.02 ||
                        Math.abs(yaw - lastYaw) > 0.25f || Math.abs(pitch - lastPitch) > 0.25f);
        if (moved) accumFrame = 0;
        lastX = x; lastY = y; lastZ = z;
        lastYaw = yaw; lastPitch = pitch;
    }

    public static void resetAccum() { accumFrame = 0; }

    public static void requestShutdown() {
        pendingShutdown = true;
    }

    public static void tickShutdown() {
        if (!pendingShutdown) return;
        try {
            if (GL.getCapabilities() == null) return;
            if (!GL.getCapabilities().OpenGL30) return;
            shutdown();
        } catch (Throwable ignored) {
        } finally {
            pendingShutdown = false;
        }
    }

    public static void importSharedResources(long imageHandle, int width, int height) {
        System.out.println("[Yaoguang] importShared: img=" + Long.toHexString(imageHandle)
                + " size=" + width + "x" + height);

        if (imageHandle == 0L || width <= 0 || height <= 0) return;

        boolean imageChanged = (imageHandle != lastImportedHandle || width != texWidth || height != texHeight);
        if (!imageChanged) return;

        if (importedMemoryObject != 0) {
            EXTMemoryObject.glDeleteMemoryObjectsEXT(importedMemoryObject);
            importedMemoryObject = 0;
        }
        if (sharedTex != 0) {
            GL11.glDeleteTextures(sharedTex);
            sharedTex = 0;
        }

        importedMemoryObject = EXTMemoryObject.glCreateMemoryObjectsEXT();
        if (importedMemoryObject == 0) return;

        EXTMemoryObjectWin32.glImportMemoryWin32HandleEXT(
                importedMemoryObject, 0,
                EXTMemoryObjectWin32.GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                imageHandle);

        int err = GL11.glGetError();
        if (err != GL11.GL_NO_ERROR) {
            System.err.println("[Yaoguang] glImportMemoryWin32HandleEXT failed, glError=" + err);
            EXTMemoryObject.glDeleteMemoryObjectsEXT(importedMemoryObject);
            importedMemoryObject = 0;
            return;
        }

        sharedTex = GL11.glGenTextures();
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, sharedTex);

        EXTMemoryObject.glTexStorageMem2DEXT(
                GL11.GL_TEXTURE_2D, 1, GL11.GL_RGBA8,
                width, height,
                importedMemoryObject, 0L);

        err = GL11.glGetError();
        if (err != GL11.GL_NO_ERROR) {
            System.err.println("[Yaoguang] glTexStorageMem2DEXT failed, glError=" + err);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);
            GL11.glDeleteTextures(sharedTex);
            sharedTex = 0;
            EXTMemoryObject.glDeleteMemoryObjectsEXT(importedMemoryObject);
            importedMemoryObject = 0;
            return;
        }

        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL12.GL_CLAMP_TO_EDGE);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL12.GL_CLAMP_TO_EDGE);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);

        lastImportedHandle = imageHandle;
        texWidth = width;
        texHeight = height;
        System.out.println("[Yaoguang] Shared texture imported: " + width + "x" + height);
    }

    public static void render(int width, int height) {
        if (initFailed) return;
        if (sharedTex == 0) return;
        if (width <= 0 || height <= 0) return;

        Minecraft mc = Minecraft.getInstance();
        if (mc == null) return;

        if (!initialized) {
            try {
                initAll();
                initialized = true;
            } catch (Throwable t) {
                initFailed = true;
                return;
            }
        }

        if (width != texWidth || height != texHeight) return;

        GL11.glFinish();

        int prevProg      = GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM);
        int prevVao       = GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING);
        int prevActiveTex = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        int prevTex       = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        int prevFbo       = GL11.glGetInteger(GL30.GL_FRAMEBUFFER_BINDING);
        boolean wasBlend   = GL11.glIsEnabled(GL11.GL_BLEND);
        boolean wasDepth   = GL11.glIsEnabled(GL11.GL_DEPTH_TEST);
        boolean wasCull    = GL11.glIsEnabled(GL11.GL_CULL_FACE);
        boolean wasScissor = GL11.glIsEnabled(GL11.GL_SCISSOR_TEST);

        int[] prevVp = new int[4];
        GL11.glGetIntegerv(GL11.GL_VIEWPORT, prevVp);

        GL11.glDisable(GL11.GL_DEPTH_TEST);
        GL11.glDisable(GL11.GL_CULL_FACE);
        GL11.glDisable(GL11.GL_SCISSOR_TEST);
        GL11.glDisable(GL11.GL_BLEND);

        boolean accumSizeChanged = (accumTex == 0 || accumFbo == 0
                || accumTexWidth != width || accumTexHeight != height);

        if (accumSizeChanged) {
            if (accumFbo != 0) {
                GL30.glDeleteFramebuffers(accumFbo);
                accumFbo = 0;
            }
            if (accumTex != 0) {
                GL11.glDeleteTextures(accumTex);
                accumTex = 0;
            }

            accumTex = GL11.glGenTextures();
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, accumTex);
            GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA8,
                    width, height, 0, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, (ByteBuffer) null);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL12.GL_CLAMP_TO_EDGE);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL12.GL_CLAMP_TO_EDGE);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);

            accumFbo = GL30.glGenFramebuffers();
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, accumFbo);
            GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                    GL11.GL_TEXTURE_2D, accumTex, 0);
            int status = GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER);
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, prevFbo);
            if (status != GL30.GL_FRAMEBUFFER_COMPLETE) {
                System.out.println("[Yaoguang] accumFbo incomplete after resize: " + status);
                if (wasBlend)   GL11.glEnable(GL11.GL_BLEND);
                if (wasDepth)   GL11.glEnable(GL11.GL_DEPTH_TEST);
                if (wasCull)    GL11.glEnable(GL11.GL_CULL_FACE);
                if (wasScissor) GL11.glEnable(GL11.GL_SCISSOR_TEST);
                return;
            }

            accumTexWidth = width;
            accumTexHeight = height;
            accumFrame = 0;
        }

        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, accumFbo);
        GL11.glViewport(0, 0, width, height);

        GL11.glEnable(GL11.GL_BLEND);
        GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
        GL11.glColorMask(true, true, true, false);

        GL20.glUseProgram(accumProgram);
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, sharedTex);
        if (locAccumNewFrame >= 0) GL20.glUniform1i(locAccumNewFrame, 0);
        float mixAmount = (accumFrame == 0) ? 1.0f : (1.0f / (float)(accumFrame + 1));
        if (mixAmount > 1.0f) mixAmount = 1.0f;
        if (locAccumMixAmount >= 0) GL20.glUniform1f(locAccumMixAmount, mixAmount);

        GL30.glBindVertexArray(vao);
        GL11.glDrawArrays(GL11.GL_TRIANGLES, 0, 6);

        GL11.glColorMask(true, true, true, true);
        GL11.glDisable(GL11.GL_BLEND);

        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, prevFbo);
        GL11.glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);

        GL20.glUseProgram(program);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, accumTex);
        if (locBlitTex >= 0) GL20.glUniform1i(locBlitTex, 0);

        GL30.glBindVertexArray(vao);
        GL11.glDrawArrays(GL11.GL_TRIANGLES, 0, 6);

        GL30.glBindVertexArray(prevVao);
        GL20.glUseProgram(prevProg);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTex);
        GL13.glActiveTexture(prevActiveTex);
        if (wasBlend)   GL11.glEnable(GL11.GL_BLEND);
        if (wasDepth)   GL11.glEnable(GL11.GL_DEPTH_TEST);
        if (wasCull)    GL11.glEnable(GL11.GL_CULL_FACE);
        if (wasScissor) GL11.glEnable(GL11.GL_SCISSOR_TEST);

        accumFrame++;
        if (accumFrame > MAX_ACCUM) accumFrame = MAX_ACCUM;
        frameCount++;
    }

    public static void shutdown() {
        if (sharedTex != 0) {
            GL11.glDeleteTextures(sharedTex);
            sharedTex = 0;
        }
        if (importedMemoryObject != 0) {
            EXTMemoryObject.glDeleteMemoryObjectsEXT(importedMemoryObject);
            importedMemoryObject = 0;
        }
        if (accumTex != 0) {
            GL11.glDeleteTextures(accumTex);
            accumTex = 0;
        }
        if (accumFbo != 0) {
            GL30.glDeleteFramebuffers(accumFbo);
            accumFbo = 0;
        }
        if (program != 0) {
            GL20.glDeleteProgram(program);
            program = 0;
        }
        if (accumProgram != 0) {
            GL20.glDeleteProgram(accumProgram);
            accumProgram = 0;
        }
        if (vao != 0) {
            GL30.glDeleteVertexArrays(vao);
            vao = 0;
        }
        if (vbo != 0) {
            GL15.glDeleteBuffers(vbo);
            vbo = 0;
        }
        initialized = false;
        initFailed = false;
        lastImportedHandle = 0;
        texWidth = 0;
        texHeight = 0;
        accumTexWidth = 0;
        accumTexHeight = 0;
    }

    private static void initAll() {
        float[] quad = {
                -1f, -1f, 0f, 1f,
                1f, -1f, 1f, 1f,
                -1f,  1f, 0f, 0f,
                -1f,  1f, 0f, 0f,
                1f, -1f, 1f, 1f,
                1f,  1f, 1f, 0f
        };

        vao = GL30.glGenVertexArrays();
        GL30.glBindVertexArray(vao);
        vbo = GL15.glGenBuffers();
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, vbo);
        java.nio.FloatBuffer buf = org.lwjgl.BufferUtils.createFloatBuffer(quad.length);
        buf.put(quad).flip();
        GL15.glBufferData(GL15.GL_ARRAY_BUFFER, buf, GL15.GL_STATIC_DRAW);
        GL20.glEnableVertexAttribArray(0);
        GL20.glVertexAttribPointer(0, 2, GL11.GL_FLOAT, false, 16, 0);
        GL20.glEnableVertexAttribArray(1);
        GL20.glVertexAttribPointer(1, 2, GL11.GL_FLOAT, false, 16, 8);
        GL30.glBindVertexArray(0);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, 0);

        String vertSrc = "#version 150\n" +
                "in vec2 aPos;\n" +
                "in vec2 aUV;\n" +
                "out vec2 vUV;\n" +
                "void main() {\n" +
                "    vUV = aUV;\n" +
                "    gl_Position = vec4(aPos, 0.0, 1.0);\n" +
                "}\n";

        String blitFragSrc = "#version 150\n" +
                "uniform sampler2D uTex;\n" +
                "in vec2 vUV;\n" +
                "out vec4 fragColor;\n" +
                "void main() {\n" +
                "    fragColor = vec4(texture(uTex, vec2(vUV.x, 1.0 - vUV.y)).rgb, 1.0);\n" +
                "}\n";

        program = createProgram(vertSrc, blitFragSrc);
        locBlitTex = GL20.glGetUniformLocation(program, "uTex");

        String accumFragSrc = "#version 150\n" +
                "uniform sampler2D uNewFrame;\n" +
                "uniform float uMixAmount;\n" +
                "in vec2 vUV;\n" +
                "out vec4 fragColor;\n" +
                "void main() {\n" +
                "    vec3 c = texture(uNewFrame, vec2(vUV.x, 1.0 - vUV.y)).rgb;\n" +
                "    fragColor = vec4(c, uMixAmount);\n" +
                "}\n";

        accumProgram = createProgram(vertSrc, accumFragSrc);
        locAccumNewFrame  = GL20.glGetUniformLocation(accumProgram, "uNewFrame");
        locAccumMixAmount = GL20.glGetUniformLocation(accumProgram, "uMixAmount");
    }

    private static int createProgram(String vs, String fs) {
        int v = compileShader(GL20.GL_VERTEX_SHADER, vs);
        int f = compileShader(GL20.GL_FRAGMENT_SHADER, fs);
        int p = GL20.glCreateProgram();
        GL20.glBindAttribLocation(p, 0, "aPos");
        GL20.glBindAttribLocation(p, 1, "aUV");
        GL20.glAttachShader(p, v);
        GL20.glAttachShader(p, f);
        GL20.glLinkProgram(p);
        GL20.glDeleteShader(v);
        GL20.glDeleteShader(f);
        return p;
    }

    private static int compileShader(int type, String src) {
        int id = GL20.glCreateShader(type);
        GL20.glShaderSource(id, src);
        GL20.glCompileShader(id);
        return id;
    }

    private CompositeRenderer() {}
}