#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>
#include <vector>

class VulkanContext;
class SceneAccel;
class VulkanOpenGLInterop;

struct RtPushConstants {
    uint32_t maxBounces;
    uint32_t samplesPerPixel;
    uint32_t frameIndex;
    uint32_t pathTracingEnabled;
    float exposure;
    float rayLength;
    float sunIntensity;
    float ambientIntensity;
    float camPosX;
    float camPosY;
    float camPosZ;
    float camDirX;
    float camDirY;
    float camDirZ;
    float camUpX;
    float camUpY;
    float camUpZ;
    float fov;
    float aspect;
    uint32_t denoiserEnabled;
    uint32_t dynamicCount;
    float sunDirX;
    float sunDirY;
    float sunDirZ;
    float rainLevel;
    float thunderLevel;
    uint32_t particleCount;
    float _pad2;
};

class RayTracingPipeline {
public:
    RayTracingPipeline();
    ~RayTracingPipeline();

    void setAssetDir(const std::string& dir) { m_assetDir = dir; }
    void initialize(VulkanContext& ctx, SceneAccel& scene, VulkanOpenGLInterop& interop);
    void shutdown();

    void updateParams(int maxBounces, int spp, bool denoiser, float exposure,
                      bool pathTracing, float rayLength);
    void updateCameraRaw(float px, float py, float pz,
                          float dx, float dy, float dz,
                          float ux, float uy, float uz,
                          float fov, float aspect);
    void setSun(float dx, float dy, float dz, float intensity) {
        m_sunDirX = dx; m_sunDirY = dy; m_sunDirZ = dz;
        m_sunIntensity = intensity;
    }
    void setWeather(float rain, float thunder) {
        m_rainLevel = rain;
        m_thunderLevel = thunder;
    }
    void uploadAtlas(const void* pixels, uint32_t w, uint32_t h);
    void uploadParticleAtlas(const void* pixels, uint32_t w, uint32_t h);
    void uploadDynamic(const void* data, uint32_t count);
    void clearDynamic();
    void uploadParticles(const void* data, uint32_t count);
    void clearParticles();
    void renderFrame();

    bool isInitialized() const { return m_pipeline != VK_NULL_HANDLE; }

private:
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

    std::string m_assetDir = ".";

    int m_maxBounces = 3;
    int m_spp = 1;
    bool m_denoiser = true;
    bool m_pathTracing = false;
    float m_exposure = 1.0f;
    float m_rayLength = 64.0f;
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

    float m_sunDirX = 0.4879f;
    float m_sunDirY = 0.7807f;
    float m_sunDirZ = 0.3903f;
    float m_sunIntensity = 1.6f;

    float m_rainLevel = 0.0f;
    float m_thunderLevel = 0.0f;

    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
    VkImageView m_atlasImageView = VK_NULL_HANDLE;
    VkSampler m_atlasSampler = VK_NULL_HANDLE;
    bool m_atlasReady = false;

    VkImage m_particleAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_particleAtlasMemory = VK_NULL_HANDLE;
    VkImageView m_particleAtlasImageView = VK_NULL_HANDLE;
    bool m_particleAtlasReady = false;

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

    VkImage m_lastBoundImage = VK_NULL_HANDLE;

    void createAtlasSampler();
    void destroyAtlas();
    void destroyParticleAtlas();
    void ensureDynamicBuffer(uint32_t count);
    void ensureParticleBuffer(uint32_t count);
    void uploadImageInternal(const void* pixels, uint32_t w, uint32_t h,
                              VkImage& image, VkDeviceMemory& memory,
                              VkImageView& view, bool& ready);
    void destroyImageInternal(VkImage& image, VkDeviceMemory& memory, VkImageView& view, bool& ready);

    VkShaderModule loadShader(const std::string& path);
    void createCommandResources();
    void createSyncObjects();
    void updateDescriptorSet();
    void buildShaderBindingTable();
    void destroySBT();
    VkDeviceAddress getBufferAddress(VkBuffer buffer);
};