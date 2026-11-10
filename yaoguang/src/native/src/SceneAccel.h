#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

class VulkanContext;

class SceneAccel {
public:
    SceneAccel();

    void initialize(VulkanContext& ctx);
    void shutdown();

    void reserve(uint32_t totalVertices, uint32_t totalIndices);
    void beginUpdate();
    bool setChunk(uint32_t slot, int32_t chunkX, int32_t chunkZ,
                  const float* vertices, uint32_t vertexCount,
                  const uint32_t* indices, uint32_t indexCount);
    void removeChunk(uint32_t slot);
    void commit();

    VkAccelerationStructureKHR getTLAS() const { return m_tlas; }
    VkBuffer getTLASBuffer() const { return m_tlasBuffer; }
    VkBuffer getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer; }
    VkBuffer getChunkTableBuffer() const { return m_chunkTableBuffer; }
    uint32_t getVertexCount() const { return m_vertexUsed; }
    uint32_t getIndexCount() const { return m_indexUsed; }
    uint32_t getChunkCount() const { return m_chunkCount; }
    VkDeviceAddress getVertexAddress() const { return m_vertexAddress; }
    VkDeviceAddress getIndexAddress() const { return m_indexAddress; }

    bool isValid() const {
        return m_initialized && m_tlas != VK_NULL_HANDLE
            && m_vertexBuffer != VK_NULL_HANDLE
            && m_indexBuffer != VK_NULL_HANDLE
            && m_chunkCount > 0;
    }

private:
    struct Chunk {
        uint32_t slot = 0;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;
        uint32_t vertexOffset = 0;
        uint32_t vertexCapacity = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCapacity = 0;
        uint32_t indexCount = 0;
        bool live = false;
        bool dirty = false;
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        VkBuffer blasBuffer = VK_NULL_HANDLE;
        VkDeviceMemory blasMemory = VK_NULL_HANDLE;
        VkDeviceAddress blasAddress = 0;
    };

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    VkAccelerationStructureKHR m_tlas = VK_NULL_HANDLE;
    VkBuffer m_tlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_tlasMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_tlasAddress = 0;
    uint64_t m_tlasSize = 0;

    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_instanceAddress = 0;
    uint32_t m_instanceCapacity = 0;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_vertexAddress = 0;
    uint32_t m_vertexCapacity = 0;
    uint32_t m_vertexUsed = 0;

    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    VkDeviceAddress m_indexAddress = 0;
    uint32_t m_indexCapacity = 0;
    uint32_t m_indexUsed = 0;

    VkBuffer m_chunkTableBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_chunkTableMemory = VK_NULL_HANDLE;
    void* m_chunkTableMapped = nullptr;
    uint32_t m_chunkTableCapacity = 0;

    VkBuffer m_scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_scratchMemory = VK_NULL_HANDLE;
    VkDeviceSize m_scratchSize = 0;

    struct CopyRegion {
        VkDeviceSize srcOffset;
        VkDeviceSize dstOffset;
        VkDeviceSize size;
    };
    std::vector<CopyRegion> m_pendingVertices;
    std::vector<CopyRegion> m_pendingIndices;

    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    uint64_t m_stagingCapacity = 0;
    uint64_t m_stagingUsed = 0;
    VkDeviceAddress m_stagingAddress = 0;
    void* m_stagingMapped = nullptr;
    VulkanContext* m_context = nullptr;

    std::unordered_map<uint32_t, Chunk> m_chunks;
    uint32_t m_chunkCount = 0;
    bool m_initialized = false;
    bool m_allDirty = false;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory);
    VkDeviceAddress getBufferAddress(VkBuffer buffer);
    void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory);

    bool ensureVertexCapacity(uint32_t vertices);
    bool ensureIndexCapacity(uint32_t indices);
    bool ensureStaging(uint64_t bytes);
    void ensureChunkTable(uint32_t slots);

    void destroyBLAS(Chunk& chunk);
    bool createBLAS(Chunk& chunk, VkCommandBuffer cmd, VkBuffer scratch, VkDeviceSize scratchSize);
    bool buildTLAS(VkCommandBuffer cmd, VkBuffer scratch, VkDeviceSize scratchSize);
    bool ensureScratch(VkDeviceSize size);

    void submitAndWait(VkCommandBuffer cmd);
};
