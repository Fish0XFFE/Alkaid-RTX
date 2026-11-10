#pragma once

#include <vulkan/vulkan.h>
#include <NRD.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct NrdCamera {
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float dirX = 0.0f;
    float dirY = 0.0f;
    float dirZ = -1.0f;
    float upX = 0.0f;
    float upY = 1.0f;
    float upZ = 0.0f;
    float fov = 70.0f;
    float aspect = 16.0f / 9.0f;

    float prevPosX = 0.0f;
    float prevPosY = 0.0f;
    float prevPosZ = 0.0f;
    float prevDirX = 0.0f;
    float prevDirY = 0.0f;
    float prevDirZ = -1.0f;
    float prevUpX = 0.0f;
    float prevUpY = 1.0f;
    float prevUpZ = 0.0f;
    float prevFov = 70.0f;
    float prevAspect = 16.0f / 9.0f;

    float jitterX = 0.0f;
    float jitterY = 0.0f;
    float prevJitterX = 0.0f;
    float prevJitterY = 0.0f;
    float timeDelta = 0.016f;
    uint32_t frameIndex = 0;
};

struct NrdInputs {
    VkImage diffuse = VK_NULL_HANDLE;
    VkImageView diffuseView = VK_NULL_HANDLE;
    VkImage specular = VK_NULL_HANDLE;
    VkImageView specularView = VK_NULL_HANDLE;
    VkImage normal = VK_NULL_HANDLE;
    VkImageView normalView = VK_NULL_HANDLE;
    VkImage viewZ = VK_NULL_HANDLE;
    VkImageView viewZView = VK_NULL_HANDLE;
    VkImage motion = VK_NULL_HANDLE;
    VkImageView motionView = VK_NULL_HANDLE;
};

class NrdDenoiser {
public:
    NrdDenoiser();
    ~NrdDenoiser();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkQueue queue, uint32_t queueFamilyIndex,
                    uint32_t width, uint32_t height);
    void shutdown();
    bool resize(uint32_t width, uint32_t height);

    bool denoise(VkCommandBuffer commandBuffer, const NrdInputs& inputs, const NrdCamera& camera);

    bool isInitialized() const { return m_instance != nullptr; }
    VkImage getDiffuseImage() const { return m_outputDiffuse.image; }
    VkImageView getDiffuseView() const { return m_outputDiffuse.view; }
    VkImage getSpecularImage() const { return m_outputSpecular.image; }
    VkImageView getSpecularView() const { return m_outputSpecular.view; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_prevWidth = 0;
    uint32_t m_prevHeight = 0;

    nrd::Instance* m_instance = nullptr;
    const nrd::InstanceDesc* m_instanceDesc = nullptr;
    nrd::Identifier m_identifier = 0;

    VkDescriptorSetLayout m_resourcesLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_constantsLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkPipeline> m_pipelines;

    VkSampler m_samplerNearest = VK_NULL_HANDLE;
    VkSampler m_samplerLinear = VK_NULL_HANDLE;

    VkBuffer m_constantBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_constantMemory = VK_NULL_HANDLE;
    void* m_constantMapped = nullptr;
    VkDeviceSize m_constantStride = 0;
    VkDeviceSize m_constantCapacity = 0;

    std::vector<Image> m_permanent;
    std::vector<Image> m_transient;
    Image m_outputDiffuse;
    Image m_outputSpecular;

    std::unordered_map<VkImage, VkImageLayout> m_layouts;

    bool m_restart = true;
    uint32_t m_frameIndex = 0;
    uint32_t m_maxTextures = 0;
    uint32_t m_maxStorageTextures = 0;

    static VkFormat toVkFormat(nrd::Format format);
    static uint32_t alignUp(uint32_t value, uint32_t alignment);

    bool createDescriptorLayouts();
    bool createPipelines();
    bool createConstantBuffer();
    bool createImages();
    bool createOutputImage(Image& target);
    bool createPoolImage(const nrd::TextureDesc& desc, Image& image);
    void destroyImage(Image& image);
    void destroyPoolImages();

    void buildMatrices(const NrdCamera& camera, float* viewToClip, float* viewToClipPrev,
                       float* worldToView, float* worldToViewPrev) const;

    VkImage resolveImage(const nrd::ResourceDesc& resource, const NrdInputs& inputs,
                         VkImageView& view) const;
    void barrierForCompute(VkCommandBuffer commandBuffer, VkImage image);

    NrdDenoiser(const NrdDenoiser&) = delete;
    NrdDenoiser& operator=(const NrdDenoiser&) = delete;
};
