package com.fish.yaoguang.client.gui;

public enum RayTracingPreset {
    LOW("低", 1, 1, true, 0.5f, false),
    MEDIUM("中", 2, 2, true, 1.0f, false),
    HIGH("高", 4, 4, true, 1.0f, false),
    ULTRA("极致", 8, 8, false, 1.2f, true),
    CINEMATIC("电影", 16, 16, false, 1.5f, true),
    CUSTOM("自定义", 3, 3, true, 1.0f, false);

    private final String displayName;
    private final int maxBounces;
    private final int samplesPerPixel;
    private final boolean denoiser;
    private final float exposureMultiplier;
    private final boolean pathTracing;

    RayTracingPreset(String displayName, int maxBounces, int samplesPerPixel,
                     boolean denoiser, float exposureMultiplier, boolean pathTracing) {
        this.displayName = displayName;
        this.maxBounces = maxBounces;
        this.samplesPerPixel = samplesPerPixel;
        this.denoiser = denoiser;
        this.exposureMultiplier = exposureMultiplier;
        this.pathTracing = pathTracing;
    }

    public String getDisplayName() { return displayName; }
    public int getMaxBounces() { return maxBounces; }
    public int getSamplesPerPixel() { return samplesPerPixel; }
    public boolean isDenoiser() { return denoiser; }
    public float getExposureMultiplier() { return exposureMultiplier; }
    public boolean isPathTracing() { return pathTracing; }
}