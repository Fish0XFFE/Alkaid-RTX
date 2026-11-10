#include "SceneAccel.h"
#include "VulkanContext.h"

#include <cstring>
#include <iostream>
#include <algorithm>

namespace {
constexpr uint32_t kVertexStrideFloats = 20;
constexpr uint32_t kVertexStrideBytes = kVertexStrideFloats * sizeof(float);
constexpr uint32_t kMaxSlots = 1024;
}

SceneAccel::SceneAccel() = default;

void SceneAccel::initialize(VulkanContext& ctx) {
    m_context = &ctx;
    m_device = ctx.getDevice();
    m_physicalDevice = ctx.getPhysicalDevice();
    m_queue = ctx.getQueue();
    m_queueFamilyIndex = ctx.getQueueFamilyIndex();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "[Yaoguang] SceneAccel command pool failed" << std::endl;
        return;
    }

    m_initialized = true;
}

void SceneAccel::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    if (m_tlas) {
        auto destroyFn = (PFN_vkDestroyAccelerationStructureKHR)
            vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        if (destroyFn) destroyFn(m_device, m_tlas, nullptr);
        m_tlas = VK_NULL_HANDLE;
    }
    destroyBuffer(m_tlasBuffer, m_tlasMemory);
    destroyBuffer(m_instanceBuffer, m_instanceMemory);
    destroyBuffer(m_chunkTableBuffer, m_chunkTableMemory);
    destroyBuffer(m_stagingBuffer, m_stagingMemory);
    destroyBuffer(m_vertexBuffer, m_vertexMemory);
    destroyBuffer(m_indexBuffer, m_indexMemory);
    destroyBuffer(m_scratchBuffer, m_scratchMemory);

    for (auto& entry : m_chunks) destroyBLAS(entry.second);
    m_chunks.clear();

    if (m_commandPool) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_chunkTableMapped = nullptr;
    m_initialized = false;
}

void SceneAccel::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_device, buffer, &req);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(req.memoryTypeBits, props);
    allocInfo.pNext = &flagsInfo;

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);
}

VkDeviceAddress SceneAccel::getBufferAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    auto fn = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddressKHR");
    if (!fn) fn = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddress");
    return fn ? fn(m_device, &info) : 0;
}

void SceneAccel::destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
    if (buffer) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory) {
        vkFreeMemory(m_device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void SceneAccel::reserve(uint32_t totalVertices, uint32_t totalIndices) {
    ensureVertexCapacity(totalVertices);
    ensureIndexCapacity(totalIndices);
}

bool SceneAccel::ensureVertexCapacity(uint32_t vertices) {
    if (vertices <= m_vertexCapacity && m_vertexBuffer) return true;
    uint32_t target = std::max(vertices, m_vertexCapacity ? m_vertexCapacity + m_vertexCapacity / 2 : 65536u);
    VkDeviceSize bytes = (VkDeviceSize)target * kVertexStrideBytes;

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    createBuffer(bytes,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 newBuffer, newMemory);
    if (!newBuffer) return false;

    if (m_vertexBuffer && m_vertexUsed > 0) {
        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = m_commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        VkBufferCopy copy{};
        copy.size = (VkDeviceSize)m_vertexUsed * kVertexStrideBytes;
        vkCmdCopyBuffer(cmd, m_vertexBuffer, newBuffer, 1, &copy);
        vkEndCommandBuffer(cmd);
        submitAndWait(cmd);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    destroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_vertexBuffer = newBuffer;
    m_vertexMemory = newMemory;
    m_vertexCapacity = target;
    m_vertexAddress = getBufferAddress(m_vertexBuffer);
    m_allDirty = true;
    return true;
}

bool SceneAccel::ensureIndexCapacity(uint32_t indices) {
    if (indices <= m_indexCapacity && m_indexBuffer) return true;
    uint32_t target = std::max(indices, m_indexCapacity ? m_indexCapacity + m_indexCapacity / 2 : 65536u);
    VkDeviceSize bytes = (VkDeviceSize)target * sizeof(uint32_t);

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    createBuffer(bytes,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 newBuffer, newMemory);
    if (!newBuffer) return false;

    if (m_indexBuffer && m_indexUsed > 0) {
        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = m_commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        VkBufferCopy copy{};
        copy.size = (VkDeviceSize)m_indexUsed * sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, m_indexBuffer, newBuffer, 1, &copy);
        vkEndCommandBuffer(cmd);
        submitAndWait(cmd);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    destroyBuffer(m_indexBuffer, m_indexMemory);
    m_indexBuffer = newBuffer;
    m_indexMemory = newMemory;
    m_indexCapacity = target;
    m_indexAddress = getBufferAddress(m_indexBuffer);
    m_allDirty = true;
    return true;
}

bool SceneAccel::ensureStaging(uint64_t bytes) {
    if (bytes <= m_stagingCapacity && m_stagingBuffer) return true;
    uint64_t target = std::max<uint64_t>(bytes, m_stagingCapacity ? m_stagingCapacity * 2 : (8u << 20));
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    createBuffer(target,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 newBuffer, newMemory);
    if (!newBuffer) return false;
    uint64_t keep = m_stagingUsed;
    if (m_stagingBuffer && keep > 0) {
        VkCommandBufferAllocateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ci.commandPool = m_commandPool;
        ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ci.commandBufferCount = 1;
        VkCommandBuffer c = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_device, &ci, &c) == VK_SUCCESS) {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(c, &bi);
            VkBufferCopy copy{0, 0, keep};
            vkCmdCopyBuffer(c, m_stagingBuffer, newBuffer, 1, &copy);
            vkEndCommandBuffer(c);
            submitAndWait(c);
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &c);
        }
    }
    destroyBuffer(m_stagingBuffer, m_stagingMemory);
    m_stagingBuffer = newBuffer;
    m_stagingMemory = newMemory;
    m_stagingCapacity = target;
    m_stagingUsed = keep;
    vkMapMemory(m_device, m_stagingMemory, 0, VK_WHOLE_SIZE, 0, &m_stagingMapped);
    return m_stagingMapped != nullptr;
}

void SceneAccel::ensureChunkTable(uint32_t slots) {
    if (slots <= m_chunkTableCapacity && m_chunkTableBuffer) return;
    uint32_t target = std::max(slots, 256u);
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    createBuffer((VkDeviceSize)target * 16,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | 0,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 newBuffer, newMemory);
    if (!newBuffer) return;
    if (m_chunkTableMapped) vkUnmapMemory(m_device, m_chunkTableMemory);
    destroyBuffer(m_chunkTableBuffer, m_chunkTableMemory);
    m_chunkTableBuffer = newBuffer;
    m_chunkTableMemory = newMemory;
    m_chunkTableCapacity = target;
    vkMapMemory(m_device, m_chunkTableMemory, 0, VK_WHOLE_SIZE, 0, &m_chunkTableMapped);
    memset(m_chunkTableMapped, 0, (size_t)target * 16);
    for (auto& e : m_chunks) {
        auto* t = reinterpret_cast<uint32_t*>(m_chunkTableMapped) + e.first * 4;
        t[0] = e.second.vertexOffset;
        t[1] = e.second.indexOffset;
        t[2] = e.second.vertexCount;
        t[3] = e.second.indexCount;
    }
}

void SceneAccel::beginUpdate() {
    ensureChunkTable(kMaxSlots);
    if (!m_vertexBuffer) ensureVertexCapacity(6u * 1024u * 1024u);
    if (!m_indexBuffer) ensureIndexCapacity(9u * 1024u * 1024u);
    m_pendingVertices.clear();
    m_pendingIndices.clear();
    m_stagingUsed = 0;
}

bool SceneAccel::setChunk(uint32_t slot, int32_t chunkX, int32_t chunkZ,
                          const float* vertices, uint32_t vertexCount,
                          const uint32_t* indices, uint32_t indexCount) {
    if (!m_initialized) return false;
    if (!vertices || !indices || vertexCount == 0 || indexCount == 0) return false;
    if (slot >= kMaxSlots) { std::cout << "[SA] setChunk: slot overflow" << std::endl; return false; }

    Chunk& chunk = m_chunks[slot];
    chunk.slot = slot;
    chunk.chunkX = chunkX;
    chunk.chunkZ = chunkZ;

    if (chunk.vertexCapacity < vertexCount) {
        chunk.vertexOffset = m_vertexUsed;
        m_vertexUsed += vertexCount;
        chunk.vertexCapacity = vertexCount;
        chunk.dirty = true;
        if (!ensureVertexCapacity(m_vertexUsed)) return false;
    }
    if (chunk.indexCapacity < indexCount) {
        chunk.indexOffset = m_indexUsed;
        m_indexUsed += indexCount;
        chunk.indexCapacity = indexCount;
        chunk.dirty = true;
        if (!ensureIndexCapacity(m_indexUsed)) return false;
    }

    chunk.vertexCount = vertexCount;
    chunk.indexCount = indexCount;
    chunk.live = true;
    chunk.dirty = true;

    VkDeviceSize vBytes = (VkDeviceSize)vertexCount * kVertexStrideBytes;
    VkDeviceSize iBytes = (VkDeviceSize)indexCount * sizeof(uint32_t);
    VkDeviceSize total = vBytes + iBytes;
    if (!ensureStaging(m_stagingUsed + total)) return false;

    auto* dst = reinterpret_cast<uint8_t*>(m_stagingMapped) + m_stagingUsed;
    memcpy(dst, vertices, (size_t)vBytes);
    memcpy(dst + vBytes, indices, (size_t)iBytes);

    m_pendingVertices.push_back({ m_stagingUsed, (VkDeviceSize)chunk.vertexOffset * kVertexStrideBytes, vBytes });
    m_pendingIndices.push_back({ m_stagingUsed + vBytes, (VkDeviceSize)chunk.indexOffset * sizeof(uint32_t), iBytes });
    m_stagingUsed += total;

    ensureChunkTable(slot + 1);
    if (m_chunkTableMapped) {
        auto* table = reinterpret_cast<uint32_t*>(m_chunkTableMapped) + slot * 4;
        table[0] = chunk.vertexOffset;
        table[1] = chunk.indexOffset;
        table[2] = vertexCount;
        table[3] = indexCount;
    }
    return true;
}

void SceneAccel::removeChunk(uint32_t slot) {
    auto it = m_chunks.find(slot);
    if (it == m_chunks.end()) return;
    destroyBLAS(it->second);
    it->second.live = false;
    it->second.vertexCount = 0;
    it->second.indexCount = 0;
}

void SceneAccel::destroyBLAS(Chunk& chunk) {
    if (chunk.blas) {
        auto destroyFn = (PFN_vkDestroyAccelerationStructureKHR)
            vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        if (destroyFn) destroyFn(m_device, chunk.blas, nullptr);
        chunk.blas = VK_NULL_HANDLE;
    }
    destroyBuffer(chunk.blasBuffer, chunk.blasMemory);
    chunk.blasAddress = 0;
}

bool SceneAccel::createBLAS(Chunk& chunk, VkCommandBuffer cmd, VkBuffer scratch, VkDeviceSize scratchSize) {
    auto getSizesFn = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
    auto createFn = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
    auto buildFn = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");
    if (!getSizesFn || !createFn || !buildFn) return false;
    if (chunk.vertexCount == 0 || chunk.indexCount < 3) return false;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = m_vertexAddress + (VkDeviceAddress)chunk.vertexOffset * kVertexStrideBytes;
    triangles.vertexStride = kVertexStrideBytes;
    triangles.maxVertex = chunk.vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = m_indexAddress + (VkDeviceAddress)chunk.indexOffset * sizeof(uint32_t);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = 0;
    geometry.geometry.triangles = triangles;

    uint32_t primCount = chunk.indexCount / 3;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    getSizesFn(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);
    std::cout << "[SA] blas sizes prims=" << primCount
              << " as=" << (unsigned long long)sizeInfo.accelerationStructureSize
              << " scratch=" << (unsigned long long)sizeInfo.buildScratchSize
              << " haveScratch=" << (unsigned long long)scratchSize << std::endl;
    if (sizeInfo.accelerationStructureSize == 0) return false;
    if (sizeInfo.buildScratchSize > m_scratchSize) {
        if (!ensureScratch(sizeInfo.buildScratchSize)) return false;
    }
    scratch = m_scratchBuffer;

    destroyBLAS(chunk);

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    createBuffer(sizeInfo.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 newBuffer, newMemory);
    if (!newBuffer) return false;

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = newBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (createFn(m_device, &createInfo, nullptr, &chunk.blas) != VK_SUCCESS) {
        destroyBuffer(newBuffer, newMemory);
        return false;
    }
    chunk.blasBuffer = newBuffer;
    chunk.blasMemory = newMemory;
    chunk.blasAddress = getBufferAddress(newBuffer);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primCount;
    range.primitiveOffset = 0;
    range.firstVertex = 0;
    range.transformOffset = 0;

    buildInfo.dstAccelerationStructure = chunk.blas;
    buildInfo.scratchData.deviceAddress = getBufferAddress(scratch);

    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
    buildFn(cmd, 1, &buildInfo, &pRange);
    return true;
}

bool SceneAccel::ensureScratch(VkDeviceSize size) {
    if (size <= m_scratchSize && m_scratchBuffer) return true;
    VkDeviceSize target = std::max<VkDeviceSize>(size, 1u << 20);
    destroyBuffer(m_scratchBuffer, m_scratchMemory);
    createBuffer(target,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_scratchBuffer, m_scratchMemory);
    m_scratchSize = m_scratchBuffer ? target : 0;
    return m_scratchBuffer != VK_NULL_HANDLE;
}

bool SceneAccel::buildTLAS(VkCommandBuffer cmd, VkBuffer scratch, VkDeviceSize scratchSize) {
    auto getSizesFn = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
    auto createFn = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
    auto buildFn = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");
    if (!getSizesFn || !createFn || !buildFn) return false;

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(m_chunks.size());
    for (auto& entry : m_chunks) {
        Chunk& chunk = entry.second;
        if (!chunk.live || chunk.blasAddress == 0) continue;
        VkAccelerationStructureInstanceKHR inst{};
        inst.transform.matrix[0][0] = 1.0f;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][2] = 1.0f;
        inst.instanceCustomIndex = chunk.slot & 0x00FFFFFFu;
        inst.mask = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = chunk.blasAddress;
        instances.push_back(inst);
    }
    m_chunkCount = (uint32_t)instances.size();
    if (instances.empty()) return false;

    VkDeviceSize bytes = (VkDeviceSize)instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    if (m_instanceCapacity < bytes || m_instanceBuffer == VK_NULL_HANDLE) {
        destroyBuffer(m_instanceBuffer, m_instanceMemory);
        createBuffer(bytes,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_instanceBuffer, m_instanceMemory);
        if (!m_instanceBuffer) return false;
        m_instanceCapacity = (uint32_t)bytes;
        m_instanceAddress = getBufferAddress(m_instanceBuffer);
        void* mapped = nullptr;
        vkMapMemory(m_device, m_instanceMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        memcpy(mapped, instances.data(), (size_t)bytes);
        vkUnmapMemory(m_device, m_instanceMemory);
    } else {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_instanceMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        memcpy(mapped, instances.data(), (size_t)bytes);
        vkUnmapMemory(m_device, m_instanceMemory);
    }

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = m_instanceAddress;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primCount = (uint32_t)instances.size();
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    getSizesFn(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);
    if (sizeInfo.accelerationStructureSize == 0) return false;
    if (sizeInfo.buildScratchSize > scratchSize) return false;

    if (m_tlas && m_tlasSize >= sizeInfo.accelerationStructureSize) {
        // reuse
    } else {
        if (m_tlas) {
            auto destroyFn = (PFN_vkDestroyAccelerationStructureKHR)
                vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
            if (destroyFn) destroyFn(m_device, m_tlas, nullptr);
            m_tlas = VK_NULL_HANDLE;
        }
        destroyBuffer(m_tlasBuffer, m_tlasMemory);
        createBuffer(sizeInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_tlasBuffer, m_tlasMemory);
        if (!m_tlasBuffer) return false;

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_tlasBuffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (createFn(m_device, &createInfo, nullptr, &m_tlas) != VK_SUCCESS) return false;
        m_tlasSize = sizeInfo.accelerationStructureSize;
        m_tlasAddress = getBufferAddress(m_tlasBuffer);
    }

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primCount;

    buildInfo.dstAccelerationStructure = m_tlas;
    buildInfo.scratchData.deviceAddress = getBufferAddress(scratch);

    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
    buildFn(cmd, 1, &buildInfo, &pRange);
    return true;
}

void SceneAccel::commit() {
    if (!m_initialized) { std::cout << "[SA] commit skipped: not initialized" << std::endl; return; }
    std::cout << "[SA] commit enter pendingV=" << m_pendingVertices.size()
              << " pendingI=" << m_pendingIndices.size() << " allDirty=" << (m_allDirty ? 1 : 0) << std::endl;
    if (m_pendingVertices.empty() && !m_allDirty && m_tlas != VK_NULL_HANDLE) {
        bool anyDirty = false;
        for (auto& entry : m_chunks) if (entry.second.dirty) { anyDirty = true; break; }
        if (!anyDirty) return;
    }
    if (m_vertexBuffer == VK_NULL_HANDLE || m_indexBuffer == VK_NULL_HANDLE) return;

    VkCommandBufferAllocateInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = m_commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    if (!m_pendingVertices.empty()) {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);

        for (const auto& c : m_pendingVertices) {
            VkBufferCopy copy{c.srcOffset, c.dstOffset, c.size};
            vkCmdCopyBuffer(cmd, m_stagingBuffer, m_vertexBuffer, 1, &copy);
        }
        for (const auto& c : m_pendingIndices) {
            VkBufferCopy copy{c.srcOffset, c.dstOffset, c.size};
            vkCmdCopyBuffer(cmd, m_stagingBuffer, m_indexBuffer, 1, &copy);
        }
        m_pendingVertices.clear();
        m_pendingIndices.clear();

        VkMemoryBarrier post{};
        post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &post, 0, nullptr, 0, nullptr);
    }

    VkDeviceSize needScratch = 1u << 20;
    {
        auto getSizesFn = (PFN_vkGetAccelerationStructureBuildSizesKHR)
            vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
        for (auto& entry : m_chunks) {
            Chunk& chunk = entry.second;
            if (!chunk.dirty || !chunk.live) continue;
            VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
            triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            triangles.vertexData.deviceAddress = m_vertexAddress + (VkDeviceAddress)chunk.vertexOffset * kVertexStrideBytes;
            triangles.vertexStride = kVertexStrideBytes;
            triangles.maxVertex = chunk.vertexCount - 1;
            triangles.indexType = VK_INDEX_TYPE_UINT32;
            triangles.indexData.deviceAddress = m_indexAddress + (VkDeviceAddress)chunk.indexOffset * sizeof(uint32_t);
            VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles = triangles;
            VkAccelerationStructureBuildGeometryInfoKHR bi{};
            bi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            bi.geometryCount = 1;
            bi.pGeometries = &geometry;
            uint32_t pc = chunk.indexCount / 3;
            VkAccelerationStructureBuildSizesInfoKHR si{};
            si.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
            getSizesFn(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &pc, &si);
            needScratch = std::max(needScratch, si.buildScratchSize);
        }
    }
    if (!ensureScratch(needScratch)) {
        vkEndCommandBuffer(cmd);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        return;
    }

    for (auto& entry : m_chunks) {
        Chunk& chunk = entry.second;
        if (!chunk.dirty || !chunk.live) continue;
        if (!createBLAS(chunk, cmd, m_scratchBuffer, m_scratchSize)) continue;
        VkMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &b, 0, nullptr, 0, nullptr);
        chunk.dirty = false;
    }

    bool tlasOk = buildTLAS(cmd, m_scratchBuffer, m_scratchSize);
    std::cout << "[SA] commit chunks=" << m_chunks.size()
              << " vtx=" << m_vertexUsed << " idx=" << m_indexUsed
              << " vtxAddr=" << (void*)m_vertexAddress
              << " idxAddr=" << (void*)m_indexAddress
              << " tlas=" << (void*)m_tlas << " ok=" << (tlasOk ? 1 : 0) << std::endl;
    if (!tlasOk) {
        vkEndCommandBuffer(cmd);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        return;
    }

    vkEndCommandBuffer(cmd);
    submitAndWait(cmd);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    m_allDirty = false;
}

void SceneAccel::submitAndWait(VkCommandBuffer cmd) {
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
    vkQueueSubmit(m_queue, 1, &info, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
}
