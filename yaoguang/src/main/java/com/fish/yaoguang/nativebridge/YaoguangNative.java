package com.fish.yaoguang.nativebridge;

import java.nio.FloatBuffer;

public final class YaoguangNative {
    static {
        NativeLoader.load();
    }

    public static native void setAssetDir(String path);
    public static native boolean init();
    public static native void shutdown();
    public static native void setRayTracingEnabled(boolean enabled);
    public static native void applyPreset(int presetOrdinal);
    public static native void setMaxBounces(int bounces);
    public static native void setSamplesPerPixel(int samples);
    public static native void setRayLength(float length);
    public static native void setDenoiserEnabled(boolean enabled);
    public static native void setExposure(float exposure);
    public static native void setPathTracingEnabled(boolean enabled);
    public static native void setVramExtension(boolean enabled, int gigabytes);
    public static native void onFrameBegin(long windowHandle, int width, int height);
    public static native void onFrameEnd();
    public static native void rebuildSceneAccel(long vertexBufferPtr, long indexBufferPtr, int vertexCount, int indexCount);
    public static native boolean isRayTracingSupported();
    public static native int getRayTracingUnit();
    public static native String getDeviceName();
    public static native void setCamera(float px, float py, float pz,
                                        float dx, float dy, float dz,
                                        float ux, float uy, float uz,
                                        float fov, float aspect);
    public static native void renderFrame();
    public static native int getOutputTexture();
    public static native int getOutputWidth();
    public static native int getOutputHeight();
    public static native long getOutputNativePtr();
    public static native long getSharedHandle();
    public static native long getVkSignalSemaphoreHandle();
    public static native long getGlSignalSemaphoreHandle();
    public static native void uploadSceneGeometry(float[] vertices, int[] indices);
    public static native void setSun(float dx, float dy, float dz, float intensity);
    public static native void setWeather(float rain, float thunder);
    public static native void uploadBlockAtlas(int width, int height, java.nio.ByteBuffer pixels);
    public static native void uploadDynamic(FloatBuffer data, int count);
    public static native void clearDynamic();
    public static native void uploadParticleAtlas(int width, int height, java.nio.ByteBuffer pixels);
    public static native void uploadParticles(FloatBuffer data, int count);
    public static native void clearParticles();

    private YaoguangNative() {}
}