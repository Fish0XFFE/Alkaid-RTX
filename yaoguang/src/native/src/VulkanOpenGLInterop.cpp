#include "VulkanOpenGLInterop.h"
#include <iostream>

static constexpr float RENDER_SCALE = 1.0f;

VulkanOpenGLInterop::VulkanOpenGLInterop() = default;

VulkanOpenGLInterop::~VulkanOpenGLInterop() {
    shutdown();
}

bool VulkanOpenGLInterop::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                      VkQueue queue, uint32_t queueFamilyIndex,
                                      uint32_t width, uint32_t height) {
    (void)queue;
    (void)queueFamilyIndex;

    m_device = device;
    m_physicalDevice = physicalDevice;
    m_width  = (uint32_t)(width  * RENDER_SCALE);
    m_height = (uint32_t)(height * RENDER_SCALE);

    if (m_width == 0 || m_height == 0) return false;

    m_vkGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)
        vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandleKHR");
    m_vkGetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)
        vkGetDeviceProcAddr(m_device, "vkGetSemaphoreWin32HandleKHR");

    if (!m_vkGetMemoryWin32HandleKHR || !m_vkGetSemaphoreWin32HandleKHR) {
        std::cerr << "[Interop] Win32 handle functions unavailable" << std::endl;
        return false;
    }

    if (!createSharedImage()) {
        destroyResources();
        return false;
    }

    if (!createSemaphores()) {
        destroyResources();
        return false;
    }

    return true;
}

void VulkanOpenGLInterop::shutdown() {
    destroyResources();
}

void VulkanOpenGLInterop::resize(uint32_t width, uint32_t height) {
    uint32_t sw = (uint32_t)(width  * RENDER_SCALE);
    uint32_t sh = (uint32_t)(height * RENDER_SCALE);
    if (sw == m_width && sh == m_height) return;
    if (sw == 0 || sh == 0) return;

    vkDeviceWaitIdle(m_device);
    destroyResources();
    m_width  = sw;
    m_height = sh;
    if (!createSharedImage() || !createSemaphores()) {
        destroyResources();
    }
}

bool VulkanOpenGLInterop::createSharedImage() {
    VkExternalMemoryImageCreateInfo externalImageInfo{};
    externalImageInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext         = &externalImageInfo;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width  = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_vkImage) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_vkImage, &memReqs);
    m_memorySize = memReqs.size;

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memType = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memType = i;
            break;
        }
    }
    if (memType == UINT32_MAX) return false;

    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = m_vkImage;

    VkExportMemoryAllocateInfo exportInfo{};
    exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    exportInfo.pNext       = &dedicatedInfo;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext           = &exportInfo;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vkMemory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(m_device, m_vkImage, m_vkMemory, 0) != VK_SUCCESS) return false;

    VkMemoryGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory     = m_vkMemory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    if (m_vkGetMemoryWin32HandleKHR(m_device, &handleInfo, &m_sharedHandle) != VK_SUCCESS) return false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS) return false;

    std::cout << "[Interop] shared image created: " << m_width << "x" << m_height
              << " handle=" << m_sharedHandle << " size=" << m_memorySize << std::endl;
    return true;
}

bool VulkanOpenGLInterop::createSemaphores() {
    VkExportSemaphoreCreateInfo exportInfo{};
    exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &exportInfo;

    VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_vkSignalSemaphore) != VK_SUCCESS) return false;
    handleInfo.semaphore = m_vkSignalSemaphore;
    if (m_vkGetSemaphoreWin32HandleKHR(m_device, &handleInfo, &m_vkSignalSemaphoreHandle) != VK_SUCCESS) return false;

    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_glSignalSemaphore) != VK_SUCCESS) return false;
    handleInfo.semaphore = m_glSignalSemaphore;
    if (m_vkGetSemaphoreWin32HandleKHR(m_device, &handleInfo, &m_glSignalSemaphoreHandle) != VK_SUCCESS) return false;

    std::cout << "[Interop] semaphores created: vk=" << m_vkSignalSemaphoreHandle
              << " gl=" << m_glSignalSemaphoreHandle << std::endl;
    return true;
}

void VulkanOpenGLInterop::destroyResources() {
    if (m_glSignalSemaphore) {
        vkDestroySemaphore(m_device, m_glSignalSemaphore, nullptr);
        m_glSignalSemaphore = VK_NULL_HANDLE;
    }
    if (m_vkSignalSemaphore) {
        vkDestroySemaphore(m_device, m_vkSignalSemaphore, nullptr);
        m_vkSignalSemaphore = VK_NULL_HANDLE;
    }
    if (m_glSignalSemaphoreHandle) {
        CloseHandle(m_glSignalSemaphoreHandle);
        m_glSignalSemaphoreHandle = nullptr;
    }
    if (m_vkSignalSemaphoreHandle) {
        CloseHandle(m_vkSignalSemaphoreHandle);
        m_vkSignalSemaphoreHandle = nullptr;
    }
    if (m_vkImageView) {
        vkDestroyImageView(m_device, m_vkImageView, nullptr);
        m_vkImageView = VK_NULL_HANDLE;
    }
    if (m_vkImage) {
        vkDestroyImage(m_device, m_vkImage, nullptr);
        m_vkImage = VK_NULL_HANDLE;
    }
    if (m_vkMemory) {
        vkFreeMemory(m_device, m_vkMemory, nullptr);
        m_vkMemory = VK_NULL_HANDLE;
    }
    if (m_sharedHandle) {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }
    m_memorySize = 0;
    m_width = 0;
    m_height = 0;
}