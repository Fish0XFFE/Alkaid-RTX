package com.fish.yaoguang.client.gui;

public enum RayTracingPreset {
    LOW("低", 1, 1, true, 0.36f, true),
    MEDIUM("中", 2, 1, true, 0.42f, true),
    HIGH("高", 3, 1, true, 0.42f, true),
    ULTRA("极致", 3, 1, true, 0.46f, true),
    CINEMATIC("电影", 4, 1, true, 0.50f, true),
    CUSTOM("自定义", 2, 1, true, 0.42f, true);

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
