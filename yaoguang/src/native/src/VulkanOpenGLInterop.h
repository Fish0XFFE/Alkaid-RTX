#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <windows.h>

class VulkanOpenGLInterop {
public:
    VulkanOpenGLInterop();
    ~VulkanOpenGLInterop();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkQueue queue, uint32_t queueFamilyIndex,
                    uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint32_t width, uint32_t height);

    VkImage           getVulkanImage()             const { return m_vkImage; }
    VkImageView       getVulkanImageView()         const { return m_vkImageView; }
    VkSemaphore       getVkSignalSemaphore()       const { return m_vkSignalSemaphore; }
    VkSemaphore       getGlSignalSemaphore()       const { return m_glSignalSemaphore; }
    uint32_t          getWidth()                   const { return m_width; }
    uint32_t          getHeight()                  const { return m_height; }
    VkDeviceSize      getMemorySize()              const { return m_memorySize; }
    HANDLE            getSharedImageHandle()       const { return m_sharedHandle; }
    HANDLE            getVkSignalSemaphoreHandle() const { return m_vkSignalSemaphoreHandle; }
    HANDLE            getGlSignalSemaphoreHandle() const { return m_glSignalSemaphoreHandle; }

    bool isValid() const { return m_vkImage != VK_NULL_HANDLE && m_sharedHandle != nullptr; }

private:
    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkImage        m_vkImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkMemory = VK_NULL_HANDLE;
    VkImageView    m_vkImageView = VK_NULL_HANDLE;
    HANDLE         m_sharedHandle = nullptr;
    VkDeviceSize   m_memorySize = 0;

    VkSemaphore m_vkSignalSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_glSignalSemaphore = VK_NULL_HANDLE;
    HANDLE      m_vkSignalSemaphoreHandle = nullptr;
    HANDLE      m_glSignalSemaphoreHandle = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    PFN_vkGetMemoryWin32HandleKHR    m_vkGetMemoryWin32HandleKHR = nullptr;
    PFN_vkGetSemaphoreWin32HandleKHR m_vkGetSemaphoreWin32HandleKHR = nullptr;

    bool createSharedImage();
    bool createSemaphores();
    void destroyResources();
};