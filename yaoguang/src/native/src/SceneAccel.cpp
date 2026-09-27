#include "SceneAccel.h"
#include "VulkanContext.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

SceneAccel::SceneAccel() = default;

SceneAccel::~SceneAccel() {
    shutdown();
}

void SceneAccel::initialize(VulkanContext& ctx) {
    m_device = ctx.getDevice();
    m_physicalDevice = ctx.getPhysicalDevice();
    m_queue = ctx.getQueue();
    m_queueFamilyIndex = ctx.getQueueFamilyIndex();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "[Yaoguang] SceneAccel: failed to create command pool" << std::endl;
        return;
    }

    m_initialized = true;
}

void SceneAccel::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    destroyTLAS();
    destroyBLAS();
    destroyGeometryBuffers();

    if (m_commandPool) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_initialized = false;
}

VkDeviceAddress SceneAccel::getBufferAddress(VkBuffer buffer) {
    if (buffer == VK_NULL_HANDLE) return 0;
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device, &info);
}

void SceneAccel::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props,
                               VkBuffer& buffer, VkDeviceMemory& memory) {
    if (size == 0) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &info, nullptr, &buffer) != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buffer, &req);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memType = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            memType = i;
            break;
        }
    }
    if (memType == UINT32_MAX) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }

    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);
}

void SceneAccel::destroyGeometryBuffers() {
    if (m_vertexBuffer) { vkDestroyBuffer(m_device, m_vertexBuffer, nullptr); m_vertexBuffer = VK_NULL_HANDLE; }
    if (m_vertexMemory) { vkFreeMemory(m_device, m_vertexMemory, nullptr); m_vertexMemory = VK_NULL_HANDLE; }
    if (m_indexBuffer) { vkDestroyBuffer(m_device, m_indexBuffer, nullptr); m_indexBuffer = VK_NULL_HANDLE; }
    if (m_indexMemory) { vkFreeMemory(m_device, m_indexMemory, nullptr); m_indexMemory = VK_NULL_HANDLE; }
    m_vertexAddress = 0;
    m_indexAddress = 0;
    m_vertexCount = 0;
    m_indexCount = 0;
}

void SceneAccel::destroyBLAS() {
    if (m_blas) {
        auto destroyFn = (PFN_vkDestroyAccelerationStructureKHR)
            vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        if (destroyFn) destroyFn(m_device, m_blas, nullptr);
        m_blas = VK_NULL_HANDLE;
    }
    if (m_blasBuffer) { vkDestroyBuffer(m_device, m_blasBuffer, nullptr); m_blasBuffer = VK_NULL_HANDLE; }
    if (m_blasMemory) { vkFreeMemory(m_device, m_blasMemory, nullptr); m_blasMemory = VK_NULL_HANDLE; }
    m_blasAddress = 0;
}

void SceneAccel::destroyTLAS() {
    if (m_tlas) {
        auto destroyFn = (PFN_vkDestroyAccelerationStructureKHR)
            vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        if (destroyFn) destroyFn(m_device, m_tlas, nullptr);
        m_tlas = VK_NULL_HANDLE;
    }
    if (m_tlasBuffer) { vkDestroyBuffer(m_device, m_tlasBuffer, nullptr); m_tlasBuffer = VK_NULL_HANDLE; }
    if (m_tlasMemory) { vkFreeMemory(m_device, m_tlasMemory, nullptr); m_tlasMemory = VK_NULL_HANDLE; }
    if (m_instanceBuffer) { vkDestroyBuffer(m_device, m_instanceBuffer, nullptr); m_instanceBuffer = VK_NULL_HANDLE; }
    if (m_instanceMemory) { vkFreeMemory(m_device, m_instanceMemory, nullptr); m_instanceMemory = VK_NULL_HANDLE; }
    m_tlasAddress = 0;
    m_instanceAddress = 0;
}

void SceneAccel::submitAndWait(VkCommandBuffer cmd) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);

    vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
}

void SceneAccel::rebuild(const float* vertices, const uint32_t* indices,
                          uint32_t vertexCount, uint32_t indexCount) {
    if (!m_initialized) {
        std::cerr << "[Yaoguang] SceneAccel not initialized" << std::endl;
        return;
    }
    if (vertexCount == 0 || indexCount == 0) return;
    if (vertices == nullptr || indices == nullptr) {
        std::cerr << "[Yaoguang] SceneAccel: null vertex/index pointer" << std::endl;
        return;
    }
    if (indexCount % 3 != 0) {
        std::cerr << "[Yaoguang] SceneAccel: indexCount not multiple of 3" << std::endl;
        return;
    }

    vkDeviceWaitIdle(m_device);

    destroyTLAS();
    destroyBLAS();
    destroyGeometryBuffers();

    m_vertexCount = vertexCount;
    m_indexCount = indexCount;

    VkDeviceSize vertexSize = (VkDeviceSize)vertexCount * sizeof(float) * 12;
    VkDeviceSize indexSize = (VkDeviceSize)indexCount * sizeof(uint32_t);

    createBuffer(vertexSize,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_vertexBuffer, m_vertexMemory);

    createBuffer(indexSize,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_indexBuffer, m_indexMemory);

    if (m_vertexBuffer == VK_NULL_HANDLE || m_indexBuffer == VK_NULL_HANDLE) {
        std::cerr << "[Yaoguang] SceneAccel: failed to create geometry buffers" << std::endl;
        destroyGeometryBuffers();
        return;
    }

    VkBuffer stagingV = VK_NULL_HANDLE;
    VkDeviceMemory stagingVMem = VK_NULL_HANDLE;
    createBuffer(vertexSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingV, stagingVMem);

    VkBuffer stagingI = VK_NULL_HANDLE;
    VkDeviceMemory stagingIMem = VK_NULL_HANDLE;
    createBuffer(indexSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingI, stagingIMem);

    if (stagingV == VK_NULL_HANDLE || stagingI == VK_NULL_HANDLE) {
        if (stagingV) vkDestroyBuffer(m_device, stagingV, nullptr);
        if (stagingVMem) vkFreeMemory(m_device, stagingVMem, nullptr);
        if (stagingI) vkDestroyBuffer(m_device, stagingI, nullptr);
        if (stagingIMem) vkFreeMemory(m_device, stagingIMem, nullptr);
        destroyGeometryBuffers();
        return;
    }

    void* data = nullptr;
    vkMapMemory(m_device, stagingVMem, 0, vertexSize, 0, &data);
    memcpy(data, vertices, (size_t)vertexSize);
    vkUnmapMemory(m_device, stagingVMem);

    vkMapMemory(m_device, stagingIMem, 0, indexSize, 0, &data);
    memcpy(data, indices, (size_t)indexSize);
    vkUnmapMemory(m_device, stagingIMem);

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

    VkBufferCopy copyRegion{};
    copyRegion.size = vertexSize;
    vkCmdCopyBuffer(cmd, stagingV, m_vertexBuffer, 1, &copyRegion);

    copyRegion.size = indexSize;
    vkCmdCopyBuffer(cmd, stagingI, m_indexBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);
    submitAndWait(cmd);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    vkDestroyBuffer(m_device, stagingV, nullptr);
    vkFreeMemory(m_device, stagingVMem, nullptr);
    vkDestroyBuffer(m_device, stagingI, nullptr);
    vkFreeMemory(m_device, stagingIMem, nullptr);

    m_vertexAddress = getBufferAddress(m_vertexBuffer);
    m_indexAddress = getBufferAddress(m_indexBuffer);

    if (m_vertexAddress == 0 || m_indexAddress == 0) {
        std::cerr << "[Yaoguang] SceneAccel: buffer device address is 0" << std::endl;
        destroyGeometryBuffers();
        return;
    }

    if (!buildBLAS()) {
        std::cerr << "[Yaoguang] SceneAccel: BLAS build failed" << std::endl;
        destroyBLAS();
        destroyGeometryBuffers();
        return;
    }

    if (!buildTLAS()) {
        std::cerr << "[Yaoguang] SceneAccel: TLAS build failed" << std::endl;
        destroyTLAS();
        destroyBLAS();
        destroyGeometryBuffers();
        return;
    }

    std::cout << "[Yaoguang] Scene rebuilt: " << vertexCount << " verts, "
              << indexCount << " indices" << std::endl;
}

bool SceneAccel::buildBLAS() {
    auto getSizesFn = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
    auto createFn = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
    auto buildFn = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");

    if (!getSizesFn || !createFn || !buildFn) {
        std::cerr << "[Yaoguang] Required RT functions not available" << std::endl;
        return false;
    }

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = m_vertexAddress;
    triangles.vertexStride = sizeof(float) * 12;
    triangles.maxVertex = m_vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = m_indexAddress;
    triangles.transformData = {};

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = 0;
    geometry.geometry.triangles = triangles;

    uint32_t primitiveCount = m_indexCount / 3;
    if (primitiveCount == 0) return false;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    getSizesFn(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
               &buildInfo, &primitiveCount, &sizeInfo);

    createBuffer(sizeInfo.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_blasBuffer, m_blasMemory);

    if (m_blasBuffer == VK_NULL_HANDLE) return false;

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_blasBuffer;
    createInfo.offset = 0;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (createFn(m_device, &createInfo, nullptr, &m_blas) != VK_SUCCESS) return false;

    VkBuffer scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory scratchMemory = VK_NULL_HANDLE;
    createBuffer(sizeInfo.buildScratchSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 scratchBuffer, scratchMemory);

    if (scratchBuffer == VK_NULL_HANDLE) return false;

    VkDeviceAddress scratchAddr = getBufferAddress(scratchBuffer);

    buildInfo.dstAccelerationStructure = m_blas;
    buildInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

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

    buildFn(cmd, 1, &buildInfo, &pRangeInfo);

    vkEndCommandBuffer(cmd);
    submitAndWait(cmd);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, scratchBuffer, nullptr);
    vkFreeMemory(m_device, scratchMemory, nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = m_blas;
    auto getAddrFn = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR");
    if (!getAddrFn) return false;
    m_blasAddress = getAddrFn(m_device, &addrInfo);

    return m_blasAddress != 0;
}

bool SceneAccel::buildTLAS() {
    auto getSizesFn = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
    auto createFn = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
    auto buildFn = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");

    if (!getSizesFn || !createFn || !buildFn) return false;

    uint32_t instanceCount = 1;

    VkAccelerationStructureInstanceKHR instance{};
    memset(&instance, 0, sizeof(instance));
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = m_blasAddress;

    if (m_blasAddress == 0) return false;

    VkDeviceSize instanceSize = sizeof(VkAccelerationStructureInstanceKHR);

    createBuffer(instanceSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_instanceBuffer, m_instanceMemory);

    if (m_instanceBuffer == VK_NULL_HANDLE) return false;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createBuffer(instanceSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuf, stagingMem);

    if (stagingBuf == VK_NULL_HANDLE) return false;

    void* data = nullptr;
    vkMapMemory(m_device, stagingMem, 0, instanceSize, 0, &data);
    memcpy(data, &instance, (size_t)instanceSize);
    vkUnmapMemory(m_device, stagingMem);

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

    VkBufferCopy copyRegion{};
    copyRegion.size = instanceSize;
    vkCmdCopyBuffer(cmd, stagingBuf, m_instanceBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);
    submitAndWait(cmd);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    m_instanceAddress = getBufferAddress(m_instanceBuffer);

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = m_instanceAddress;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = 0;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    getSizesFn(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
               &buildInfo, &instanceCount, &sizeInfo);

    createBuffer(sizeInfo.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_tlasBuffer, m_tlasMemory);

    if (m_tlasBuffer == VK_NULL_HANDLE) return false;

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_tlasBuffer;
    createInfo.offset = 0;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (createFn(m_device, &createInfo, nullptr, &m_tlas) != VK_SUCCESS) return false;

    VkBuffer scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory scratchMemory = VK_NULL_HANDLE;
    createBuffer(sizeInfo.buildScratchSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 scratchBuffer, scratchMemory);

    if (scratchBuffer == VK_NULL_HANDLE) return false;

    VkDeviceAddress scratchAddr = getBufferAddress(scratchBuffer);

    buildInfo.dstAccelerationStructure = m_tlas;
    buildInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);

    vkBeginCommandBuffer(cmd, &beginInfo);
    buildFn(cmd, 1, &buildInfo, &pRangeInfo);
    vkEndCommandBuffer(cmd);
    submitAndWait(cmd);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, scratchBuffer, nullptr);
    vkFreeMemory(m_device, scratchMemory, nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = m_tlas;
    auto getAddrFn = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR");
    if (!getAddrFn) return false;
    m_tlasAddress = getAddrFn(m_device, &addrInfo);

    return m_tlasAddress != 0;
}