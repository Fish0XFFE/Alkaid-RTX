#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class VulkanContext;

class SceneAccel {
public:
    SceneAccel();
    ~SceneAccel();

    void initialize(VulkanContext& ctx);
    void shutdown();

    void rebuild(const float* vertices, const uint32_t* indices,
                 uint32_t vertexCount, uint32_t indexCount);

    VkAccelerationStructureKHR getTLAS() const { return m_tlas; }
    VkBuffer getTLASBuffer() const { return m_tlasBuffer; }
    VkBuffer getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer; }
    uint32_t getVertexCount() const { return m_vertexCount; }
    uint32_t getIndexCount() const { return m_indexCount; }
    VkDeviceAddress getVertexAddress() const { return m_vertexAddress; }
    VkDeviceAddress getIndexAddress() const { return m_indexAddress; }

    bool isValid() const {
        return m_initialized
            && m_tlas != VK_NULL_HANDLE
            && m_vertexBuffer != VK_NULL_HANDLE
            && m_indexBuffer != VK_NULL_HANDLE
            && m_vertexCount > 0
            && m_indexCount > 0;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    VkAccelerationStructureKHR m_blas = VK_NULL_HANDLE;
    VkBuffer m_blasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_blasMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_blasAddress = 0;

    VkAccelerationStructureKHR m_tlas = VK_NULL_HANDLE;
    VkBuffer m_tlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_tlasMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_tlasAddress = 0;

    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_instanceAddress = 0;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_vertexAddress = 0;

    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_indexAddress = 0;

    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;

    bool m_initialized = false;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory);

    VkDeviceAddress getBufferAddress(VkBuffer buffer);

    void destroyBLAS();
    void destroyTLAS();
    void destroyGeometryBuffers();

    bool buildBLAS();
    bool buildTLAS();

    void submitAndWait(VkCommandBuffer cmd);
};