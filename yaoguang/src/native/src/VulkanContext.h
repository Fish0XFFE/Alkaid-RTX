#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

enum class GpuRayTracingUnit {
    NONE = 0,
    RT_CORE = 1,
    RAY_ACCELERATOR = 2,
    RTU = 3
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    bool initialize();
    void shutdown();

    bool supportsRayTracing() const { return m_rayTracingSupported; }
    GpuRayTracingUnit getRayTracingUnit() const { return m_rayTracingUnit; }
    std::string getDeviceName() const { return m_deviceName; }

    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkQueue getQueue() const { return m_queue; }
    uint32_t getQueueFamilyIndex() const { return m_queueFamilyIndex; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    void reserveVirtualVram(bool enabled, uint32_t gigabytes);

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    bool m_rayTracingSupported = false;
    GpuRayTracingUnit m_rayTracingUnit = GpuRayTracingUnit::NONE;
    std::string m_deviceName;
    VkDeviceSize m_virtualVramSize = 0;

    bool m_externalMemoryInstanceSupported = false;
    bool m_externalSemaphoreInstanceSupported = false;

    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    GpuRayTracingUnit detectRayTracingUnit(VkPhysicalDevice dev, bool hasAS, bool hasRT);
    bool deviceHasRtExtensions(VkPhysicalDevice dev);
};