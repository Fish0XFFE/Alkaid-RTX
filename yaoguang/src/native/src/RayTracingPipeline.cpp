#include "RayTracingPipeline.h"
#include "VulkanContext.h"
#include "VulkanOpenGLInterop.h"
#include "SceneAccel.h"

#include <fstream>
#include <vector>
#include <cstring>
#include <iostream>

RayTracingPipeline::RayTracingPipeline() = default;

RayTracingPipeline::~RayTracingPipeline() {
    shutdown();
}

VkDeviceAddress RayTracingPipeline::getBufferAddress(VkBuffer buffer) {
    if (buffer == VK_NULL_HANDLE) return 0;
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device, &info);
}

VkShaderModule RayTracingPipeline::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Yaoguang] Cannot open shader: " << path << std::endl;
        return VK_NULL_HANDLE;
    }
    size_t size = (size_t)file.tellg();
    file.seekg(0);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    std::cout << "[Yaoguang] Shader loaded: " << path << " (" << size << " bytes)" << std::endl;
    return module;
}

void RayTracingPipeline::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) return;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer);
}

void RayTracingPipeline::createSyncObjects() {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence);
}

void RayTracingPipeline::ensureDynamicBuffer(uint32_t count) {
    if (count <= m_dynamicCapacity && m_dynamicBuffer != VK_NULL_HANDLE) return;

    uint32_t newCap = count < 256 ? 256 : count * 2;
    if (newCap > 4096) newCap = 4096;

    if (m_dynamicMapped) {
        vkUnmapMemory(m_device, m_dynamicMemory);
        m_dynamicMapped = nullptr;
    }
    if (m_dynamicBuffer) {
        vkDestroyBuffer(m_device, m_dynamicBuffer, nullptr);
        m_dynamicBuffer = VK_NULL_HANDLE;
    }
    if (m_dynamicMemory) {
        vkFreeMemory(m_device, m_dynamicMemory, nullptr);
        m_dynamicMemory = VK_NULL_HANDLE;
    }

    VkDeviceSize size = (VkDeviceSize)newCap * 8 * sizeof(float);

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &m_dynamicBuffer) != VK_SUCCESS) return;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, m_dynamicBuffer, &mr);

    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mprops);

    uint32_t mt = UINT32_MAX;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & flags) == flags) {
            mt = i; break;
        }
    }
    if (mt == UINT32_MAX) return;

    VkMemoryAllocateFlagsInfo af{};
    af.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.pNext = &af;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (vkAllocateMemory(m_device, &ma, nullptr, &m_dynamicMemory) != VK_SUCCESS) return;

    vkBindBufferMemory(m_device, m_dynamicBuffer, m_dynamicMemory, 0);
    if (vkMapMemory(m_device, m_dynamicMemory, 0, size, 0, &m_dynamicMapped) != VK_SUCCESS ||
        !m_dynamicMapped) {
        std::cerr << "[Yaoguang] ensureDynamicBuffer: failed to map dynamic buffer" << std::endl;
        vkFreeMemory(m_device, m_dynamicMemory, nullptr);
        m_dynamicMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(m_device, m_dynamicBuffer, nullptr);
        m_dynamicBuffer = VK_NULL_HANDLE;
        m_dynamicMapped = nullptr;
        return;
    }
    m_dynamicAddress = getBufferAddress(m_dynamicBuffer);
    m_dynamicCapacity = newCap;

    updateDescriptorSet();
}

void RayTracingPipeline::ensureParticleBuffer(uint32_t count) {
    if (count <= m_particleCapacity && m_particleBuffer != VK_NULL_HANDLE) return;

    uint32_t newCap = count < 1024 ? 1024 : count * 2;
    if (newCap > 16384) newCap = 16384;

    if (m_particleMapped) {
        vkUnmapMemory(m_device, m_particleMemory);
        m_particleMapped = nullptr;
    }
    if (m_particleBuffer) {
        vkDestroyBuffer(m_device, m_particleBuffer, nullptr);
        m_particleBuffer = VK_NULL_HANDLE;
    }
    if (m_particleMemory) {
        vkFreeMemory(m_device, m_particleMemory, nullptr);
        m_particleMemory = VK_NULL_HANDLE;
    }

    VkDeviceSize size = (VkDeviceSize)newCap * 8 * sizeof(float);

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &m_particleBuffer) != VK_SUCCESS) return;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, m_particleBuffer, &mr);

    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mprops);

    uint32_t mt = UINT32_MAX;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & flags) == flags) {
            mt = i; break;
        }
    }
    if (mt == UINT32_MAX) return;

    VkMemoryAllocateFlagsInfo af{};
    af.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.pNext = &af;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (vkAllocateMemory(m_device, &ma, nullptr, &m_particleMemory) != VK_SUCCESS) return;

    vkBindBufferMemory(m_device, m_particleBuffer, m_particleMemory, 0);
    if (vkMapMemory(m_device, m_particleMemory, 0, size, 0, &m_particleMapped) != VK_SUCCESS ||
        !m_particleMapped) {
        std::cerr << "[Yaoguang] ensureParticleBuffer: failed to map particle buffer" << std::endl;
        vkFreeMemory(m_device, m_particleMemory, nullptr);
        m_particleMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(m_device, m_particleBuffer, nullptr);
        m_particleBuffer = VK_NULL_HANDLE;
        m_particleMapped = nullptr;
        return;
    }
    m_particleAddress = getBufferAddress(m_particleBuffer);
    m_particleCapacity = newCap;

    updateDescriptorSet();
}

void RayTracingPipeline::uploadDynamic(const void* data, uint32_t count) {
    if (count == 0) { clearDynamic(); return; }
    ensureDynamicBuffer(count);
    if (m_dynamicBuffer == VK_NULL_HANDLE || !m_dynamicMapped) return;
    uint32_t n = count > m_dynamicCapacity ? m_dynamicCapacity : count;
    std::memcpy(m_dynamicMapped, data, (size_t)n * 8 * sizeof(float));
    m_dynamicCount = n;
}

void RayTracingPipeline::clearDynamic() {
    m_dynamicCount = 0;
}

void RayTracingPipeline::uploadParticles(const void* data, uint32_t count) {
    if (count == 0) { m_particleCount = 0; return; }
    ensureParticleBuffer(count);
    if (m_particleBuffer == VK_NULL_HANDLE || !m_particleMapped) return;
    uint32_t n = count > m_particleCapacity ? m_particleCapacity : count;
    std::memcpy(m_particleMapped, data, (size_t)n * 8 * sizeof(float));
    m_particleCount = n;
}

void RayTracingPipeline::clearParticles() { m_particleCount = 0; }

void RayTracingPipeline::uploadParticleAtlas(const void*, uint32_t, uint32_t) {}

void RayTracingPipeline::destroyParticleAtlas() {
    destroyImageInternal(m_particleAtlasImage, m_particleAtlasMemory,
                          m_particleAtlasImageView, m_particleAtlasReady);
}

void RayTracingPipeline::initialize(VulkanContext& ctx, SceneAccel& scene,
                                     VulkanOpenGLInterop& interop) {
    m_device = ctx.getDevice();
    m_physicalDevice = ctx.getPhysicalDevice();
    m_queue = ctx.getQueue();
    m_queueFamilyIndex = ctx.getQueueFamilyIndex();
    m_scene = &scene;
    m_interop = &interop;

    std::cout << "[Yaoguang] Shader asset dir: " << m_assetDir << std::endl;

    m_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR");
    if (!m_vkCmdTraceRaysKHR) {
        std::cerr << "[Yaoguang] vkCmdTraceRaysKHR not available" << std::endl;
        return;
    }

    createCommandResources();
    createSyncObjects();

    if (!ctx.supportsRayTracing()) return;

    ensureDynamicBuffer(512);
    ensureParticleBuffer(1024);

    VkDescriptorSetLayoutBinding bindings[7] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                             VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                             VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) return;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    pushRange.offset = 0;
    pushRange.size = sizeof(RtPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) return;

    VkDescriptorPoolSize poolSizes[4] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 4;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[3].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) return;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) return;

    VkShaderModule rgen = loadShader(m_assetDir + "/shaders/ray_gen.rgen.spv");
    VkShaderModule rchit = loadShader(m_assetDir + "/shaders/closest_hit.rchit.spv");
    VkShaderModule rmiss = loadShader(m_assetDir + "/shaders/miss.rmiss.spv");

    if (rgen == VK_NULL_HANDLE || rchit == VK_NULL_HANDLE || rmiss == VK_NULL_HANDLE) {
        std::cerr << "[Yaoguang] Missing shaders" << std::endl;
        return;
    }

    VkPipelineShaderStageCreateInfo stages[3]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[0].module = rgen;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[1].module = rchit;
    stages[1].pName = "main";

    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[2].module = rmiss;
    stages[2].pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
    groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0;
    groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[1].generalShader = VK_SHADER_UNUSED_KHR;
    groups[1].closestHitShader = 1;
    groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[2].generalShader = 2;
    groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = stages;
    pipelineInfo.groupCount = 3;
    pipelineInfo.pGroups = groups;
    pipelineInfo.maxPipelineRayRecursionDepth = 4;
    pipelineInfo.layout = m_pipelineLayout;

    auto createFn = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR");
    if (createFn) {
        VkResult res = createFn(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
        if (res != VK_SUCCESS) {
            std::cerr << "[Yaoguang] Failed to create RT pipeline: " << res << std::endl;
            m_pipeline = VK_NULL_HANDLE;
        } else {
            std::cout << "[Yaoguang] RT pipeline created OK" << std::endl;
        }
    }

    vkDestroyShaderModule(m_device, rgen, nullptr);
    vkDestroyShaderModule(m_device, rchit, nullptr);
    vkDestroyShaderModule(m_device, rmiss, nullptr);

    buildShaderBindingTable();
    createAtlasSampler();
}

void RayTracingPipeline::buildShaderBindingTable() {
    if (m_pipeline == VK_NULL_HANDLE) return;

    auto getHandlesFn = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesKHR");
    if (!getHandlesFn) return;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &rtProps;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    uint32_t handleAlignment = rtProps.shaderGroupBaseAlignment;
    if (handleSize == 0 || handleAlignment == 0) {
        std::cerr << "[Yaoguang] buildShaderBindingTable: invalid RT handle size/alignment"
                  << std::endl;
        return;
    }
    uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    uint32_t groupCount = 3;
    std::vector<uint8_t> handles(groupCount * handleSize);
    VkResult res = getHandlesFn(m_device, m_pipeline, 0, groupCount, groupCount * handleSize, handles.data());
    if (res != VK_SUCCESS) return;

    VkDeviceSize sbtSize = (VkDeviceSize)handleSizeAligned * groupCount;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sbtSize;
    bufInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(m_device, &bufInfo, nullptr, &m_sbtBuffer) != VK_SUCCESS) return;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, m_sbtBuffer, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memType = UINT32_MAX;
    VkMemoryPropertyFlags wantFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & wantFlags) == wantFlags) {
            memType = i; break;
        }
    }
    if (memType == UINT32_MAX) return;

    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_sbtMemory) != VK_SUCCESS) return;
    vkBindBufferMemory(m_device, m_sbtBuffer, m_sbtMemory, 0);

    void* data = nullptr;
    if (vkMapMemory(m_device, m_sbtMemory, 0, sbtSize, 0, &data) != VK_SUCCESS || !data) {
        std::cerr << "[Yaoguang] buildShaderBindingTable: failed to map SBT memory" << std::endl;
        destroySBT();
        return;
    }
    for (uint32_t i = 0; i < groupCount; i++) {
        std::memcpy((uint8_t*)data + i * handleSizeAligned, handles.data() + i * handleSize, handleSize);
    }
    vkUnmapMemory(m_device, m_sbtMemory);

    VkDeviceAddress sbtAddr = getBufferAddress(m_sbtBuffer);

    m_sbtRayGen.deviceAddress = sbtAddr + 0 * handleSizeAligned;
    m_sbtRayGen.stride = handleSizeAligned;
    m_sbtRayGen.size = handleSizeAligned;

    m_sbtHit.deviceAddress = sbtAddr + 1 * handleSizeAligned;
    m_sbtHit.stride = handleSizeAligned;
    m_sbtHit.size = handleSizeAligned;

    m_sbtMiss.deviceAddress = sbtAddr + 2 * handleSizeAligned;
    m_sbtMiss.stride = handleSizeAligned;
    m_sbtMiss.size = handleSizeAligned;

    m_sbtCallable = {};
    std::cout << "[Yaoguang] SBT built OK, handleSize=" << handleSize << " aligned=" << handleSizeAligned << std::endl;
}

void RayTracingPipeline::destroySBT() {
    if (m_sbtBuffer) { vkDestroyBuffer(m_device, m_sbtBuffer, nullptr); m_sbtBuffer = VK_NULL_HANDLE; }
    if (m_sbtMemory) { vkFreeMemory(m_device, m_sbtMemory, nullptr); m_sbtMemory = VK_NULL_HANDLE; }
    m_sbtRayGen = {}; m_sbtMiss = {}; m_sbtHit = {}; m_sbtCallable = {};
}

void RayTracingPipeline::createAtlasSampler() {
    if (m_atlasSampler != VK_NULL_HANDLE) return;
    if (m_device == VK_NULL_HANDLE) return;
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = 0.0f;
    if (vkCreateSampler(m_device, &si, nullptr, &m_atlasSampler) != VK_SUCCESS) m_atlasSampler = VK_NULL_HANDLE;
}

void RayTracingPipeline::destroyAtlas() {
    destroyImageInternal(m_atlasImage, m_atlasMemory, m_atlasImageView, m_atlasReady);
}

void RayTracingPipeline::uploadImageInternal(
        const void* pixels, uint32_t w, uint32_t h,
        VkImage& image, VkDeviceMemory& memory, VkImageView& view, bool& ready) {

    if (w == 0 || h == 0) return;

    vkDeviceWaitIdle(m_device);
    destroyImageInternal(image, memory, view, ready);

    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width = w;
    ii.extent.height = h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &ii, nullptr, &image) != VK_SUCCESS) return;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, image, &mr);

    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mprops);

    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mt = i; break;
        }
    }
    if (mt == UINT32_MAX) { destroyImageInternal(image, memory, view, ready); return; }

    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (vkAllocateMemory(m_device, &ma, nullptr, &memory) != VK_SUCCESS) {
        destroyImageInternal(image, memory, view, ready); return;
    }
    vkBindImageMemory(m_device, image, memory, 0);

    VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(m_device, &bi, nullptr, &staging) != VK_SUCCESS) {
        destroyImageInternal(image, memory, view, ready); return;
    }
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(m_device, staging, &bmr);
    uint32_t bmt = UINT32_MAX;
    VkMemoryPropertyFlags hflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((bmr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & hflags) == hflags) {
            bmt = i; break;
        }
    }
    if (bmt == UINT32_MAX) {
        vkDestroyBuffer(m_device, staging, nullptr);
        destroyImageInternal(image, memory, view, ready); return;
    }
    VkMemoryAllocateInfo bma{};
    bma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bma.allocationSize = bmr.size;
    bma.memoryTypeIndex = bmt;
    if (vkAllocateMemory(m_device, &bma, nullptr, &stagingMem) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, staging, nullptr);
        destroyImageInternal(image, memory, view, ready); return;
    }
    vkBindBufferMemory(m_device, staging, stagingMem, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMem, 0, bytes, 0, &mapped);
    std::memcpy(mapped, pixels, (size_t)bytes);
    vkUnmapMemory(m_device, stagingMem);

    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = m_commandPool;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cba, &cmd);

    VkCommandBufferBeginInfo cbb{};
    cbb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbb);

    VkImageMemoryBarrier ib{};
    ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ib.subresourceRange.levelCount = 1;
    ib.subresourceRange.layerCount = 1;
    ib.srcAccessMask = 0;
    ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &ib);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier ib2 = ib;
    ib2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ib2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ib2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                          0, 0, nullptr, 0, nullptr, 1, &ib2);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &vi, nullptr, &view) != VK_SUCCESS) {
        destroyImageInternal(image, memory, view, ready); return;
    }
    ready = true;
}

void RayTracingPipeline::destroyImageInternal(
        VkImage& image, VkDeviceMemory& memory, VkImageView& view, bool& ready) {
    if (view) { vkDestroyImageView(m_device, view, nullptr); view = VK_NULL_HANDLE; }
    if (image) { vkDestroyImage(m_device, image, nullptr); image = VK_NULL_HANDLE; }
    if (memory) { vkFreeMemory(m_device, memory, nullptr); memory = VK_NULL_HANDLE; }
    ready = false;
}

void RayTracingPipeline::uploadAtlas(const void* pixels, uint32_t w, uint32_t h) {
    uploadImageInternal(pixels, w, h,
                        m_atlasImage, m_atlasMemory,
                        m_atlasImageView, m_atlasReady);
    updateDescriptorSet();
    std::cout << "[Yaoguang] Atlas uploaded to Vulkan: " << w << "x" << h << std::endl;
}

void RayTracingPipeline::updateParams(int maxBounces, int spp, bool denoiser,
                                       float exposure, bool pathTracing, float rayLength) {
    m_maxBounces = maxBounces;
    m_spp = spp;
    m_denoiser = denoiser;
    m_exposure = exposure;
    m_pathTracing = pathTracing;
    m_rayLength = rayLength;
}

void RayTracingPipeline::updateCameraRaw(float px, float py, float pz,
                                          float dx, float dy, float dz,
                                          float ux, float uy, float uz,
                                          float fov, float aspect) {
    m_camPosX = px; m_camPosY = py; m_camPosZ = pz;
    m_camDirX = dx; m_camDirY = dy; m_camDirZ = dz;
    m_camUpX = ux; m_camUpY = uy; m_camUpZ = uz;
    m_fov = fov; m_aspect = aspect;
}

void RayTracingPipeline::updateDescriptorSet() {
    if (!m_scene || !m_interop) return;
    if (m_descriptorSet == VK_NULL_HANDLE) return;
    if (m_scene->getTLAS() == VK_NULL_HANDLE) return;
    if (m_interop->getVulkanImageView() == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = m_interop->getVulkanImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    VkAccelerationStructureKHR tlas = m_scene->getTLAS();
    asInfo.pAccelerationStructures = &tlas;

    VkDescriptorBufferInfo vertexInfo{};
    vertexInfo.buffer = m_scene->getVertexBuffer();
    vertexInfo.offset = 0;
    vertexInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo indexInfo{};
    indexInfo.buffer = m_scene->getIndexBuffer();
    indexInfo.offset = 0;
    indexInfo.range = VK_WHOLE_SIZE;

    VkBuffer safeDyn = m_dynamicBuffer;
    if (safeDyn == VK_NULL_HANDLE) safeDyn = m_scene->getIndexBuffer();
    if (safeDyn == VK_NULL_HANDLE) return;

    VkDescriptorBufferInfo dynInfo{};
    dynInfo.buffer = safeDyn;
    dynInfo.offset = 0;
    dynInfo.range = VK_WHOLE_SIZE;

    VkBuffer safePart = m_particleBuffer;
    if (safePart == VK_NULL_HANDLE) safePart = m_scene->getIndexBuffer();
    if (safePart == VK_NULL_HANDLE) return;

    VkDescriptorBufferInfo partInfo{};
    partInfo.buffer = safePart;
    partInfo.offset = 0;
    partInfo.range = VK_WHOLE_SIZE;

    VkImageView safeAtlasView = m_atlasImageView;
    if (safeAtlasView == VK_NULL_HANDLE) safeAtlasView = m_interop->getVulkanImageView();

    if (m_atlasSampler == VK_NULL_HANDLE) createAtlasSampler();
    if (m_atlasSampler == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo atlasInfo{};
    atlasInfo.imageView = safeAtlasView;
    atlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atlasInfo.sampler = m_atlasSampler;

    VkWriteDescriptorSet writes[7]{};
    uint32_t wc = 0;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 0;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[wc].pImageInfo = &imageInfo;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].pNext = &asInfo;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 1;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 2;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[wc].pBufferInfo = &vertexInfo;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 3;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[wc].pBufferInfo = &indexInfo;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 4;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[wc].pImageInfo = &atlasInfo;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 5;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[wc].pBufferInfo = &dynInfo;
    wc++;

    writes[wc].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[wc].dstSet = m_descriptorSet;
    writes[wc].dstBinding = 6;
    writes[wc].descriptorCount = 1;
    writes[wc].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[wc].pBufferInfo = &partInfo;
    wc++;

    vkUpdateDescriptorSets(m_device, wc, writes, 0, nullptr);
}

void RayTracingPipeline::renderFrame() {
    if (!m_pipeline || !m_scene || !m_interop) return;
    if (m_sbtRayGen.size == 0) return;
    if (m_commandBuffer == VK_NULL_HANDLE) return;
    if (!m_scene->isValid()) return;

    VkImage outImage = m_interop->getVulkanImage();
    VkImageView outView = m_interop->getVulkanImageView();
    if (outImage == VK_NULL_HANDLE || outView == VK_NULL_HANDLE) return;

    uint32_t w = m_interop->getWidth();
    uint32_t h = m_interop->getHeight();
    if (w == 0 || h == 0) return;

    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) return;

    updateDescriptorSet();

    VkImageLayout oldLayout = (outImage == m_lastBoundImage)
                              ? VK_IMAGE_LAYOUT_GENERAL
                              : VK_IMAGE_LAYOUT_UNDEFINED;
    m_lastBoundImage = outImage;

    VkImageMemoryBarrier initialBarrier{};
    initialBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    initialBarrier.oldLayout = oldLayout;
    initialBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    initialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initialBarrier.image = outImage;
    initialBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    initialBarrier.subresourceRange.baseMipLevel = 0;
    initialBarrier.subresourceRange.levelCount = 1;
    initialBarrier.subresourceRange.baseArrayLayer = 0;
    initialBarrier.subresourceRange.layerCount = 1;
    initialBarrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_GENERAL)
                                    ? VK_ACCESS_SHADER_WRITE_BIT : 0;
    initialBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 0, nullptr, 0, nullptr, 1, &initialBarrier);

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    RtPushConstants pc{};
    pc.maxBounces = (uint32_t)m_maxBounces;
    pc.samplesPerPixel = (uint32_t)m_spp;
    pc.frameIndex = m_frameIndex++;
    pc.pathTracingEnabled = m_pathTracing ? 1u : 0u;
    pc.exposure = m_exposure;
    pc.rayLength = m_rayLength;
    pc.sunIntensity = m_sunIntensity;
    pc.ambientIntensity = 0.35f;
    pc.camPosX = m_camPosX;
    pc.camPosY = m_camPosY;
    pc.camPosZ = m_camPosZ;
    pc.camDirX = m_camDirX;
    pc.camDirY = m_camDirY;
    pc.camDirZ = m_camDirZ;
    pc.camUpX = m_camUpX;
    pc.camUpY = m_camUpY;
    pc.camUpZ = m_camUpZ;
    pc.fov = m_fov;
    pc.aspect = m_aspect;
    pc.denoiserEnabled = m_denoiser ? 1u : 0u;
    pc.dynamicCount = m_dynamicCount;
    pc.sunDirX = m_sunDirX;
    pc.sunDirY = m_sunDirY;
    pc.sunDirZ = m_sunDirZ;
    pc.rainLevel = m_rainLevel;
    pc.thunderLevel = m_thunderLevel;
    pc.particleCount = m_particleCount;
    pc._pad2 = 0.0f;

    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                       VK_SHADER_STAGE_MISS_BIT_KHR,
                       0, sizeof(pc), &pc);

    m_vkCmdTraceRaysKHR(m_commandBuffer,
                        &m_sbtRayGen, &m_sbtMiss, &m_sbtHit, &m_sbtCallable,
                        w, h, 1);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(m_commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_queue, 1, &submitInfo, m_fence);
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkQueueWaitIdle(m_queue);
}

void RayTracingPipeline::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    destroySBT();
    destroyAtlas();
    destroyParticleAtlas();

    if (m_atlasSampler) { vkDestroySampler(m_device, m_atlasSampler, nullptr); m_atlasSampler = VK_NULL_HANDLE; }

    if (m_dynamicMapped) { vkUnmapMemory(m_device, m_dynamicMemory); m_dynamicMapped = nullptr; }
    if (m_dynamicBuffer) { vkDestroyBuffer(m_device, m_dynamicBuffer, nullptr); m_dynamicBuffer = VK_NULL_HANDLE; }
    if (m_dynamicMemory) { vkFreeMemory(m_device, m_dynamicMemory, nullptr); m_dynamicMemory = VK_NULL_HANDLE; }

    if (m_particleMapped) { vkUnmapMemory(m_device, m_particleMemory); m_particleMapped = nullptr; }
    if (m_particleBuffer) { vkDestroyBuffer(m_device, m_particleBuffer, nullptr); m_particleBuffer = VK_NULL_HANDLE; }
    if (m_particleMemory) { vkFreeMemory(m_device, m_particleMemory, nullptr); m_particleMemory = VK_NULL_HANDLE; }

    if (m_fence) { vkDestroyFence(m_device, m_fence, nullptr); m_fence = VK_NULL_HANDLE; }
    if (m_commandPool) { vkDestroyCommandPool(m_device, m_commandPool, nullptr); m_commandPool = VK_NULL_HANDLE; m_commandBuffer = VK_NULL_HANDLE; }
    if (m_descriptorPool) { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; m_descriptorSet = VK_NULL_HANDLE; }
    if (m_pipeline) { vkDestroyPipeline(m_device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_descriptorSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
}