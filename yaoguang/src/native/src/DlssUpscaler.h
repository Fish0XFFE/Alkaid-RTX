#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

class DlssUpscaler {
public:
    DlssUpscaler();
    ~DlssUpscaler();

    bool initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    VkQueue queue, uint32_t queueFamily, const std::string& dllDir);
    void shutdown();

    bool isAvailable() const { return m_available && m_quality != 5; }
    void setQuality(int quality);
    bool resize(uint32_t inputWidth, uint32_t inputHeight,
                uint32_t outputWidth, uint32_t outputHeight);
    bool evaluate(VkCommandBuffer commandBuffer,
                  VkImage color, VkImageView colorView,
                  VkImage depth, VkImageView depthView,
                  VkImage motion, VkImageView motionView,
                  VkImage output, VkImageView outputView,
                  float jitterX, float jitterY, bool reset);
    const std::string& status() const { return m_status; }

private:
    void closeFeature();
    bool ensureFeature(uint32_t inputWidth, uint32_t inputHeight,
                       uint32_t outputWidth, uint32_t outputHeight);
    bool runCreateCommands();
    bool loadCore(const std::string& dllDir);
    bool loadFeatureDll(const std::string& dllDir);
    void setImageParams(void* params, VkImage color, VkImageView colorView,
                        VkImage depth, VkImageView depthView,
                        VkImage motion, VkImageView motionView,
                        VkImage output, VkImageView outputView,
                        float jitterX, float jitterY, bool reset);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

    void* m_coreModule = nullptr;
    void* m_featureModule = nullptr;
    void* m_params = nullptr;
    void* m_feature = nullptr;
    bool m_available = false;
    bool m_initialized = false;
    int m_quality = 0;
    uint32_t m_inputWidth = 0;
    uint32_t m_inputHeight = 0;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
    std::string m_status;

    void* m_init = nullptr;
    void* m_initProject = nullptr;
    void* m_initPath = nullptr;
    void* m_getParams = nullptr;
    void* m_createFeature = nullptr;
    void* m_evaluateFeature = nullptr;
    void* m_releaseFeature = nullptr;
    void* m_shutdown = nullptr;
    void* m_destroyParams = nullptr;
};
