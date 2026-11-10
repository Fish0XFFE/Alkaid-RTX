#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>
#include <memory>

class VulkanContext;
class SceneAccel;
class VulkanOpenGLInterop;
class NrdDenoiser;
class DlssUpscaler;

struct RtPushConstants {
    uint32_t maxBounces;
    uint32_t samplesPerPixel;
    uint32_t frameIndex;
    uint32_t flags;
    float exposure;
    float rayLength;
    float sunIntensity;
    float ambientIntensity;
    float camPos[4];
    float camDir[4];
    float camUp[4];
    float fov;
    float aspect;
    float time;
    float wetness;
    uint32_t dynamicCount;
    uint32_t particleCount;
    uint32_t pad0;
    uint32_t pad1;
    float sunDir[4];
    float weather[4];
    float prevCamPos[4];
    float prevCamDir[4];
    float prevCamUp[4];
    float lens[4];
    float misc[4];
    float sunColor[4];
};

struct CombinePushConstants {
    float exposure;
    float farPlane;
    uint32_t width;
    uint32_t height;
    uint32_t pad0;
    uint32_t pad1;
};

struct CompositePushConstants {
    float exposure;
    float sharpen;
    uint32_t width;
    uint32_t height;
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    uint32_t upscale;
    uint32_t pad0;
    float blend;
    uint32_t reset;
};

class RayTracingPipeline {
public:
    RayTracingPipeline();
    ~RayTracingPipeline();

    void setAssetDir(const std::string& dir) { m_assetDir = dir; }

    void waitIdle() {
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
    }
    bool isDeviceLost() const { return m_deviceLost; }
    void initialize(VulkanContext& ctx, SceneAccel& scene, VulkanOpenGLInterop& interop);
    void shutdown();

    void updateParams(int maxBounces, int spp, bool denoiser, float exposure,
                      bool pathTracing, float rayLength);
    void updateCameraRaw(float px, float py, float pz,
                         float dx, float dy, float dz,
                         float ux, float uy, float uz,
                         float fov, float aspect);
    void setSun(float dx, float dy, float dz, float intensity) {
        m_sunDirX = dx;
        m_sunDirY = dy;
        m_sunDirZ = dz;
        m_sunIntensity = intensity;
    }
    void setWeather(float rain, float thunder, float snow) {
        m_snow = snow;
        m_rainLevel = rain;
        m_thunderLevel = thunder;
    }
    void setDimension(int dimension) {
        m_dimension = dimension;
    }
    void setEnvironment(float skyR, float skyG, float skyB,
                        float cloudR, float cloudG, float cloudB,
                        float flash, float time) {
        m_skyR = skyR;
        m_skyG = skyG;
        m_skyB = skyB;
        m_cloudR = cloudR;
        m_cloudG = cloudG;
        m_cloudB = cloudB;
        m_flash = flash;
        m_time = time;
    }
    void setRenderScale(float scale) { m_renderScale = scale < 0.25f ? 0.25f : (scale > 1.0f ? 1.0f : scale); }
    void setDlssQuality(int quality);
    bool isDlssAvailable() const;
    void uploadAtlas(const void* pixels, uint32_t w, uint32_t h);
    void uploadAtlasLevel0(const void* pixels, uint32_t w, uint32_t h);
    bool ensureAtlasStaging(VkDeviceSize bytes);
    VkBuffer m_atlasStaging[2]{};
    VkDeviceMemory m_atlasStagingMemory[2]{};
    VkDeviceSize m_atlasStagingSize = 0;
    uint32_t m_atlasStagingIndex = 0;
    VkCommandBuffer m_atlasCommands[2]{};
    VkFence m_atlasFences[2]{};
    void uploadParticleAtlas(const void* pixels, uint32_t w, uint32_t h);
    void uploadDynamic(const void* data, uint32_t count);
    void clearDynamic();
    void uploadParticles(const void* data, uint32_t count);
    void clearParticles();
    void renderFrame();

    bool isInitialized() const { return m_pipeline != VK_NULL_HANDLE; }

private:
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool initialized = false;
    };

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

    VkBuffer m_sbtBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sbtMemory = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR m_sbtRayGen{};
    VkStridedDeviceAddressRegionKHR m_sbtMiss{};
    VkStridedDeviceAddressRegionKHR m_sbtHit{};
    VkStridedDeviceAddressRegionKHR m_sbtCallable{};

    PFN_vkCmdTraceRaysKHR m_vkCmdTraceRaysKHR = nullptr;

    VulkanOpenGLInterop* m_interop = nullptr;
    SceneAccel* m_scene = nullptr;
    std::unique_ptr<NrdDenoiser> m_nrd;
    std::unique_ptr<DlssUpscaler> m_dlss;
    Target m_diffuse;
    Target m_specular;
    Target m_normalRough;
    Target m_viewZ;
    Target m_motion;
    Target m_hdr;
    Target m_depth;
    Target m_dlssOutput;
    Target m_taaHistory[2];
    uint32_t m_taaIndex = 0;
    bool m_taaReset = true;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;

    VkDescriptorSetLayout m_combineSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_combineLayout = VK_NULL_HANDLE;
    VkPipeline m_combinePipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_combinePool = VK_NULL_HANDLE;
    VkDescriptorSet m_combineSet = VK_NULL_HANDLE;

    std::string m_assetDir = ".";

    int m_maxBounces = 3;
    int m_spp = 1;
    int m_lastRequestedSpp = 0;
    bool m_denoiser = true;
    bool m_pathTracing = true;
    float m_exposure = 1.0f;
    float m_rayLength = 192.0f;
    uint32_t m_frameIndex = 0;

    float m_camPosX = 0.0f;
    float m_camPosY = 0.0f;
    float m_camPosZ = 0.0f;
    float m_camDirX = 0.0f;
    float m_camDirY = 0.0f;
    float m_camDirZ = -1.0f;
    float m_camUpX = 0.0f;
    float m_camUpY = 1.0f;
    float m_camUpZ = 0.0f;
    float m_fov = 70.0f;
    float m_aspect = 16.0f / 9.0f;

    float m_prevCamPosX = 0.0f;
    float m_prevCamPosY = 0.0f;
    float m_prevCamPosZ = 0.0f;
    float m_prevCamDirX = 0.0f;
    float m_prevCamDirY = 0.0f;
    float m_prevCamDirZ = -1.0f;
    float m_prevCamUpX = 0.0f;
    float m_prevCamUpY = 1.0f;
    float m_prevCamUpZ = 0.0f;
    float m_prevFov = 70.0f;
    float m_prevAspect = 16.0f / 9.0f;
    bool m_hasPrevCamera = false;

    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
    float m_prevJitterX = 0.0f;
    float m_prevJitterY = 0.0f;
    uint32_t m_jitterIndex = 0;

    float m_sunDirX = 0.4879f;
    float m_sunDirY = 0.7807f;
    float m_sunDirZ = 0.3903f;
    float m_sunIntensity = 1.6f;
    float m_rainLevel = 0.0f;
    float m_snow = 0.0f;
    int m_dimension = 0;
    float m_thunderLevel = 0.0f;
    float m_skyR = 0.55f;
    float m_skyG = 0.75f;
    float m_skyB = 1.0f;
    float m_cloudR = 1.0f;
    float m_cloudG = 1.0f;
    float m_cloudB = 1.0f;
    float m_flash = 0.0f;
    float m_time = 0.0f;
    float m_wetness = 0.0f;

    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
    VkImageView m_atlasImageView = VK_NULL_HANDLE;
    VkSampler m_atlasSampler = VK_NULL_HANDLE;
    bool m_atlasReady = false;
    uint32_t m_atlasMipLevels = 1;
    uint32_t m_atlasWidth = 0;
    uint32_t m_atlasHeight = 0;

    VkImage m_particleAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_particleAtlasMemory = VK_NULL_HANDLE;
    VkImageView m_particleAtlasImageView = VK_NULL_HANDLE;
    bool m_particleAtlasReady = false;

    VkImage m_dummyAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyAtlasMemory = VK_NULL_HANDLE;
    VkImageView m_dummyAtlasView = VK_NULL_HANDLE;

    VkImage m_transmittanceLutImage = VK_NULL_HANDLE;
    VkDeviceMemory m_transmittanceLutMemory = VK_NULL_HANDLE;
    VkImageView m_transmittanceLutView = VK_NULL_HANDLE;
    VkSampler m_transmittanceLutSampler = VK_NULL_HANDLE;

    VkImage m_cloudImage[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory m_cloudMemory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView m_cloudView[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t m_cloudWriteIndex = 0;
    VkSampler m_cloudSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_cloudSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_cloudPool = VK_NULL_HANDLE;
    VkDescriptorSet m_cloudSet = VK_NULL_HANDLE;
    VkPipelineLayout m_cloudLayout = VK_NULL_HANDLE;
    VkPipeline m_cloudPipeline = VK_NULL_HANDLE;
    uint32_t m_cloudWidth = 0;
    uint32_t m_cloudHeight = 0;
    bool m_cloudInitialized = false;

    VkBuffer m_dynamicBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_dynamicMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_dynamicAddress = 0;
    uint32_t m_dynamicCapacity = 0;
    uint32_t m_dynamicCount = 0;
    void* m_dynamicMapped = nullptr;

    VkBuffer m_particleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_particleAddress = 0;
    uint32_t m_particleCapacity = 0;
    uint32_t m_particleCount = 0;
    void* m_particleMapped = nullptr;

    VkPipeline m_compositePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_compositeLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compositeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_compositePool = VK_NULL_HANDLE;
    VkDescriptorSet m_compositeSet = VK_NULL_HANDLE;
    VkSampler m_compositeSampler = VK_NULL_HANDLE;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_renderScale = 1.0f;
    int m_dlssQuality = 0;
    bool m_frameTimedOut = false;
    bool m_deviceLost = false;
    int m_fenceTimeouts = 0;
    bool m_skipNrd = false;
    bool m_skipCloud = false;
    bool m_targetsDirty = true;
    VkBuffer m_debugBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_debugMemory = VK_NULL_HANDLE;
    void* m_debugMapped = nullptr;
    VkBuffer m_captureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_captureMemory = VK_NULL_HANDLE;
    void* m_captureMapped = nullptr;
    uint32_t m_captureWidth = 0;
    uint32_t m_captureHeight = 0;
    bool m_capturePending = false;
    VkImage m_boundSharedImage = VK_NULL_HANDLE;
    VkImageLayout m_boundSharedLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    void createCommandResources();
    void createSyncObjects();
    void createDescriptorLayout();
    void createCompositeResources();
    bool createTargets(uint32_t width, uint32_t height);
    void destroyTargets();
    bool createTarget(Target& target, uint32_t width, uint32_t height, VkFormat format);
    bool createDummyAtlas();
    bool createTransmittanceLut();
    void createCloudResources();
    bool createCloudTarget(uint32_t width, uint32_t height);
    void destroyCloudTarget();
    void destroyCloudResources();
    void updateDescriptorSet();
    void updateCompositeDescriptorSet(VkImageView source, uint32_t sourceWidth,
                                      uint32_t sourceHeight, bool upscale);
    void createCombineResources();
    void updateCombineDescriptorSet(bool denoised);
    void destroyCombineResources();
    void buildShaderBindingTable();
    void destroySBT();
    VkDeviceAddress getBufferAddress(VkBuffer buffer);
    VkShaderModule loadShader(const std::string& path);
    void uploadImageInternal(const void* pixels, uint32_t w, uint32_t h,
                             VkImage& image, VkDeviceMemory& memory,
                             VkImageView& view, bool& ready);
    void destroyImageInternal(VkImage& image, VkDeviceMemory& memory,
                              VkImageView& view, bool& ready);
    void destroyAtlas();
    void destroyParticleAtlas();
    void ensureDynamicBuffer(uint32_t count);
    void ensureParticleBuffer(uint32_t count);
    void createAtlasSampler();
    void barrierToGeneral(VkCommandBuffer commandBuffer, Target& target);
    bool createDebugBuffer();
    bool ensureCaptureBuffer(uint32_t width, uint32_t height);
    void writeCaptureToPpm(const char* path);
    void recreateDescriptorSet();
    void reportFrameStats();
    float debugBufferMean();
    float dlssRatio() const;

    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;
};
