#include "NrdDenoiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

NrdDenoiser::NrdDenoiser() = default;

NrdDenoiser::~NrdDenoiser() {
    shutdown();
}

uint32_t NrdDenoiser::alignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}

VkFormat NrdDenoiser::toVkFormat(nrd::Format format) {
    switch (format) {
        case nrd::Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case nrd::Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
        case nrd::Format::R8_UINT: return VK_FORMAT_R8_UINT;
        case nrd::Format::R8_SINT: return VK_FORMAT_R8_SINT;
        case nrd::Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case nrd::Format::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
        case nrd::Format::RG8_UINT: return VK_FORMAT_R8G8_UINT;
        case nrd::Format::RG8_SINT: return VK_FORMAT_R8G8_SINT;
        case nrd::Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case nrd::Format::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
        case nrd::Format::RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
        case nrd::Format::RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
        case nrd::Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case nrd::Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
        case nrd::Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
        case nrd::Format::R16_UINT: return VK_FORMAT_R16_UINT;
        case nrd::Format::R16_SINT: return VK_FORMAT_R16_SINT;
        case nrd::Format::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
        case nrd::Format::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
        case nrd::Format::RG16_SNORM: return VK_FORMAT_R16G16_SNORM;
        case nrd::Format::RG16_UINT: return VK_FORMAT_R16G16_UINT;
        case nrd::Format::RG16_SINT: return VK_FORMAT_R16G16_SINT;
        case nrd::Format::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
        case nrd::Format::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
        case nrd::Format::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        case nrd::Format::RGBA16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
        case nrd::Format::RGBA16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
        case nrd::Format::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case nrd::Format::R32_UINT: return VK_FORMAT_R32_UINT;
        case nrd::Format::R32_SINT: return VK_FORMAT_R32_SINT;
        case nrd::Format::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
        case nrd::Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
        case nrd::Format::RG32_SINT: return VK_FORMAT_R32G32_SINT;
        case nrd::Format::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
        case nrd::Format::RGB32_UINT: return VK_FORMAT_R32G32B32_UINT;
        case nrd::Format::RGB32_SINT: return VK_FORMAT_R32G32B32_SINT;
        case nrd::Format::RGB32_SFLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case nrd::Format::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
        case nrd::Format::RGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
        case nrd::Format::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case nrd::Format::R10_G10_B10_A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case nrd::Format::R10_G10_B10_A2_UINT: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case nrd::Format::R11_G11_B10_UFLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case nrd::Format::R9_G9_B9_E5_UFLOAT: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        default: return VK_FORMAT_UNDEFINED;
    }
}

void NrdDenoiser::destroyImage(Image& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
    }
    if (image.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, image.image, nullptr);
        image.image = VK_NULL_HANDLE;
    }
    if (image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, image.memory, nullptr);
        image.memory = VK_NULL_HANDLE;
    }
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.format = VK_FORMAT_UNDEFINED;
}

void NrdDenoiser::destroyPoolImages() {
    for (Image& image : m_permanent) destroyImage(image);
    for (Image& image : m_transient) destroyImage(image);
    m_permanent.clear();
    m_transient.clear();
}

bool NrdDenoiser::createPoolImage(const nrd::TextureDesc& desc, Image& image) {
    VkFormat format = toVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED) return false;

    uint32_t down = std::max<uint16_t>(1, desc.downsampleFactor);
    uint32_t width = std::max(1u, (m_width + down - 1) / down);
    uint32_t height = std::max(1u, (m_height + down - 1) / down);

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &info, nullptr, &image.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image.image, &requirements);

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &properties);

    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) {
        destroyImage(image);
        return false;
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocate, nullptr, &image.memory) != VK_SUCCESS) {
        destroyImage(image);
        return false;
    }
    if (vkBindImageMemory(m_device, image.image, image.memory, 0) != VK_SUCCESS) {
        destroyImage(image);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &image.view) != VK_SUCCESS) {
        destroyImage(image);
        return false;
    }

    image.format = format;
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_layouts[image.image] = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool NrdDenoiser::createOutputImage(Image& target) {
    destroyImage(target);

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.extent = {m_width, m_height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &info, nullptr, &target.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, target.image, &requirements);

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &properties);

    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) {
        destroyImage(target);
        return false;
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocate, nullptr, &target.memory) != VK_SUCCESS) {
        destroyImage(target);
        return false;
    }
    if (vkBindImageMemory(m_device, target.image, target.memory, 0) != VK_SUCCESS) {
        destroyImage(target);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = target.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &target.view) != VK_SUCCESS) {
        destroyImage(target);
        return false;
    }

    target.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    target.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_layouts[target.image] = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool NrdDenoiser::createImages() {
    m_permanent.resize(m_instanceDesc->permanentPoolSize);
    m_transient.resize(m_instanceDesc->transientPoolSize);

    for (uint32_t i = 0; i < m_instanceDesc->permanentPoolSize; i++) {
        if (!createPoolImage(m_instanceDesc->permanentPool[i], m_permanent[i])) {
            std::cerr << "[NRD] failed to create permanent pool image " << i << std::endl;
            return false;
        }
    }
    for (uint32_t i = 0; i < m_instanceDesc->transientPoolSize; i++) {
        if (!createPoolImage(m_instanceDesc->transientPool[i], m_transient[i])) {
            std::cerr << "[NRD] failed to create transient pool image " << i << std::endl;
            return false;
        }
    }
    if (!createOutputImage(m_outputDiffuse)) return false;
    return createOutputImage(m_outputSpecular);
}

bool NrdDenoiser::createConstantBuffer() {
    uint32_t minDataSize = std::max<uint32_t>(64, m_instanceDesc->constantBufferMaxDataSize);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    uint32_t alignment = std::max<uint32_t>(4, static_cast<uint32_t>(properties.limits.minUniformBufferOffsetAlignment));
    m_constantStride = alignUp(minDataSize, alignment);
    m_constantCapacity = m_constantStride * 128;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = m_constantCapacity;
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &info, nullptr, &m_constantBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_constantBuffer, &requirements);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

    uint32_t memoryType = UINT32_MAX;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) return false;

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocate, nullptr, &m_constantMemory) != VK_SUCCESS) return false;
    if (vkBindBufferMemory(m_device, m_constantBuffer, m_constantMemory, 0) != VK_SUCCESS) return false;
    if (vkMapMemory(m_device, m_constantMemory, 0, m_constantCapacity, 0, &m_constantMapped) != VK_SUCCESS) {
        m_constantMapped = nullptr;
        return false;
    }
    return true;
}

bool NrdDenoiser::createDescriptorLayouts() {
    m_maxTextures = std::max<uint32_t>(32, m_instanceDesc->descriptorPoolDesc.perSetTexturesMaxNum);
    m_maxStorageTextures = std::max<uint32_t>(16, m_instanceDesc->descriptorPoolDesc.perSetStorageTexturesMaxNum);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerNearest) != VK_SUCCESS) return false;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerLinear) != VK_SUCCESS) return false;

    std::vector<VkDescriptorSetLayoutBinding> resourceBindings;
    uint32_t textureOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.textureOffset;
    uint32_t storageOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.storageTextureAndBufferOffset;

    for (uint32_t i = 0; i < m_maxTextures; i++) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = textureOffset + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        resourceBindings.push_back(binding);
    }
    for (uint32_t i = 0; i < m_maxStorageTextures; i++) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = storageOffset + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        resourceBindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo resourceLayoutInfo{};
    resourceLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    resourceLayoutInfo.bindingCount = static_cast<uint32_t>(resourceBindings.size());
    resourceLayoutInfo.pBindings = resourceBindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &resourceLayoutInfo, nullptr, &m_resourcesLayout) != VK_SUCCESS) {
        return false;
    }

    uint32_t constantOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.constantBufferOffset;
    uint32_t samplerOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.samplerOffset;

    VkDescriptorSetLayoutBinding constants[3]{};
    constants[0].binding = samplerOffset + 0;
    constants[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    constants[0].descriptorCount = 1;
    constants[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    constants[0].pImmutableSamplers = &m_samplerNearest;

    constants[1].binding = samplerOffset + 1;
    constants[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    constants[1].descriptorCount = 1;
    constants[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    constants[1].pImmutableSamplers = &m_samplerLinear;

    constants[2].binding = constantOffset;
    constants[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constants[2].descriptorCount = 1;
    constants[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo constantLayoutInfo{};
    constantLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    constantLayoutInfo.bindingCount = 3;
    constantLayoutInfo.pBindings = constants;
    if (vkCreateDescriptorSetLayout(m_device, &constantLayoutInfo, nullptr, &m_constantsLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayout layouts[2] = {m_resourcesLayout, m_constantsLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSizes[4]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = m_maxTextures * 128;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = m_maxStorageTextures * 128;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 128;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[3].descriptorCount = 256;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool NrdDenoiser::createPipelines() {
    m_pipelines.resize(m_instanceDesc->pipelinesNum, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < m_instanceDesc->pipelinesNum; i++) {
        const nrd::PipelineDesc& desc = m_instanceDesc->pipelines[i];
        if (desc.computeShaderSPIRV.bytecode == nullptr || desc.computeShaderSPIRV.size == 0) {
            std::cerr << "[NRD] missing SPIR-V for pipeline " << i << std::endl;
            return false;
        }

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = static_cast<size_t>(desc.computeShaderSPIRV.size);
        moduleInfo.pCode = static_cast<const uint32_t*>(desc.computeShaderSPIRV.bytecode);

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
            std::cerr << "[NRD] failed to create shader module " << i << std::endl;
            return false;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = m_instanceDesc->shaderEntryPoint;
        pipelineInfo.layout = m_pipelineLayout;

        VkResult result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[i]);
        vkDestroyShaderModule(m_device, module, nullptr);
        if (result != VK_SUCCESS) {
            std::cerr << "[NRD] failed to create compute pipeline " << i << " result=" << result << std::endl;
            return false;
        }
    }
    return true;
}

bool NrdDenoiser::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                             VkQueue queue, uint32_t queueFamilyIndex,
                             uint32_t width, uint32_t height) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_queue = queue;
    m_queueFamilyIndex = queueFamilyIndex;
    m_width = width;
    m_height = height;

    nrd::DenoiserDesc denoiserDesc{};
    denoiserDesc.identifier = 0;
    denoiserDesc.denoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;

    nrd::InstanceCreationDesc creationDesc{};
    creationDesc.denoisers = &denoiserDesc;
    creationDesc.denoisersNum = 1;

    nrd::Result result = nrd::CreateInstance(creationDesc, m_instance);
    if (result != nrd::Result::SUCCESS || m_instance == nullptr) {
        std::cerr << "[NRD] CreateInstance failed result=" << static_cast<int>(result) << std::endl;
        m_instance = nullptr;
        return false;
    }

    m_instanceDesc = nrd::GetInstanceDesc(*m_instance);
    if (m_instanceDesc == nullptr) return false;

    nrd::RelaxSettings settings{};
    settings.diffuseMaxAccumulatedFrameNum = 6;
    settings.specularMaxAccumulatedFrameNum = 5;
    settings.diffuseMaxFastAccumulatedFrameNum = 3;
    settings.specularMaxFastAccumulatedFrameNum = 3;
    settings.historyFixFrameNum = 3;
    settings.historyFixBasePixelStride = 14;
    settings.historyFixAlternatePixelStride = 14;
    settings.fastHistoryClampingSigmaScale = 2.0f;
    settings.diffusePrepassBlurRadius = 0.0f;
    settings.specularPrepassBlurRadius = 0.0f;
    settings.minHitDistanceWeight = 0.1f;
    settings.spatialVarianceEstimationHistoryThreshold = 3;
    settings.diffusePhiLuminance = 2.0f;
    settings.specularPhiLuminance = 1.0f;
    settings.lobeAngleFraction = 0.5f;
    settings.roughnessFraction = 0.15f;
    settings.atrousIterationNum = 2;
    settings.depthThreshold = 0.003f;
    settings.enableRoughnessEdgeStopping = true;
    settings.checkerboardMode = nrd::CheckerboardMode::OFF;
    settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
    result = nrd::SetDenoiserSettings(*m_instance, 0, &settings);
    if (result != nrd::Result::SUCCESS) {
        std::cerr << "[NRD] SetDenoiserSettings failed result=" << static_cast<int>(result) << std::endl;
    }

    if (!createDescriptorLayouts()) return false;
    if (!createPipelines()) return false;
    if (!createConstantBuffer()) return false;
    if (!createImages()) return false;

    std::cout << "[NRD] initialized " << static_cast<int>(nrd::GetLibraryDesc()->versionMajor) << "."
              << static_cast<int>(nrd::GetLibraryDesc()->versionMinor) << "."
              << static_cast<int>(nrd::GetLibraryDesc()->versionBuild)
              << " pipelines=" << m_pipelines.size()
              << " permanent=" << m_permanent.size()
              << " transient=" << m_transient.size() << std::endl;
    return true;
}

void NrdDenoiser::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    std::cout << "[NRD] shutdown permanent=" << m_permanent.size()
              << " transient=" << m_transient.size() << std::endl;
    destroyPoolImages();
    destroyImage(m_outputDiffuse);
    destroyImage(m_outputSpecular);
    std::cout << "[NRD] images destroyed" << std::endl;

    for (VkPipeline pipeline : m_pipelines) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, pipeline, nullptr);
    }
    m_pipelines.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_resourcesLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_resourcesLayout, nullptr);
        m_resourcesLayout = VK_NULL_HANDLE;
    }
    if (m_constantsLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_constantsLayout, nullptr);
        m_constantsLayout = VK_NULL_HANDLE;
    }
    if (m_samplerNearest != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_samplerNearest, nullptr);
        m_samplerNearest = VK_NULL_HANDLE;
    }
    if (m_samplerLinear != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_samplerLinear, nullptr);
        m_samplerLinear = VK_NULL_HANDLE;
    }
    if (m_constantMapped) {
        vkUnmapMemory(m_device, m_constantMemory);
        m_constantMapped = nullptr;
    }
    if (m_constantBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_constantBuffer, nullptr);
        m_constantBuffer = VK_NULL_HANDLE;
    }
    if (m_constantMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_constantMemory, nullptr);
        m_constantMemory = VK_NULL_HANDLE;
    }

    if (m_instance != nullptr) {
        nrd::DestroyInstance(*m_instance);
        m_instance = nullptr;
    }
    m_instanceDesc = nullptr;
    m_layouts.clear();
    m_restart = true;
    m_prevWidth = 0;
    m_prevHeight = 0;
    m_device = VK_NULL_HANDLE;
}

bool NrdDenoiser::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (width == m_width && height == m_height) return true;
    if (m_instance == nullptr) return false;

    vkDeviceWaitIdle(m_device);
    m_prevWidth = 0;
    m_prevHeight = 0;
    m_width = width;
    m_height = height;
    m_restart = true;

    destroyPoolImages();
    destroyImage(m_outputDiffuse);
    destroyImage(m_outputSpecular);
    m_layouts.clear();
    return createImages();
}

void NrdDenoiser::buildMatrices(const NrdCamera& camera,
                                float* viewToClip, float* viewToClipPrev,
                                float* worldToView, float* worldToViewPrev) const {
    auto normalize = [](float x, float y, float z, float& ox, float& oy, float& oz) {
        float length = std::sqrt(x * x + y * y + z * z);
        if (length < 1.0e-6f) length = 1.0f;
        ox = x / length;
        oy = y / length;
        oz = z / length;
    };

    auto buildView = [&](float px, float py, float pz,
                         float dx, float dy, float dz,
                         float ux, float uy, float uz,
                         float fov, float aspect, float* out) {
        float fx, fy, fz;
        normalize(dx, dy, dz, fx, fy, fz);
        float rx = fy * uz - fz * uy;
        float ry = fz * ux - fx * uz;
        float rz = fx * uy - fy * ux;
        float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (rl < 1.0e-6f) {
            rx = 1.0f;
            ry = 0.0f;
            rz = 0.0f;
            rl = 1.0f;
        }
        rx /= rl;
        ry /= rl;
        rz /= rl;
        float tx = ry * fz - rz * fy;
        float ty = rz * fx - rx * fz;
        float tz = rx * fy - ry * fx;

        std::memset(out, 0, sizeof(float) * 16);
        out[0] = rx;
        out[1] = tx;
        out[2] = -fx;
        out[4] = ry;
        out[5] = ty;
        out[6] = -fy;
        out[8] = rz;
        out[9] = tz;
        out[10] = -fz;
        out[15] = 1.0f;
        out[12] = -(rx * px + ry * py + rz * pz);
        out[13] = -(tx * px + ty * py + tz * pz);
        out[14] = (fx * px + fy * py + fz * pz);
    };

    auto buildProjection = [&](float fov, float aspect, float* out) {
        float f = 1.0f / std::tan(fov * 0.01745329252f * 0.5f);
        std::memset(out, 0, sizeof(float) * 16);
        out[0] = f / std::max(0.001f, aspect);
        out[5] = f;
        out[10] = 10000.0f / (0.05f - 10000.0f);
        out[11] = -1.0f;
        out[14] = 10000.0f * 0.05f / (0.05f - 10000.0f);
    };

    buildView(camera.posX, camera.posY, camera.posZ,
              camera.dirX, camera.dirY, camera.dirZ,
              camera.upX, camera.upY, camera.upZ,
              camera.fov, camera.aspect, worldToView);
    buildView(camera.prevPosX, camera.prevPosY, camera.prevPosZ,
              camera.prevDirX, camera.prevDirY, camera.prevDirZ,
              camera.prevUpX, camera.prevUpY, camera.prevUpZ,
              camera.prevFov, camera.prevAspect, worldToViewPrev);
    buildProjection(camera.fov, camera.aspect, viewToClip);
    buildProjection(camera.prevFov, camera.prevAspect, viewToClipPrev);
}

VkImage NrdDenoiser::resolveImage(const nrd::ResourceDesc& resource, const NrdInputs& inputs,
                                  VkImageView& view) const {
    view = VK_NULL_HANDLE;
    switch (resource.type) {
        case nrd::ResourceType::IN_MV:
            view = inputs.motionView;
            return inputs.motion;
        case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
            view = inputs.normalView;
            return inputs.normal;
        case nrd::ResourceType::IN_VIEWZ:
            view = inputs.viewZView;
            return inputs.viewZ;
        case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
            view = inputs.diffuseView;
            return inputs.diffuse;
        case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
            view = inputs.specularView;
            return inputs.specular;
        case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
            view = m_outputDiffuse.view;
            return m_outputDiffuse.image;
        case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
            view = m_outputSpecular.view;
            return m_outputSpecular.image;
        case nrd::ResourceType::PERMANENT_POOL:
            if (resource.indexInPool < m_permanent.size()) {
                view = m_permanent[resource.indexInPool].view;
                return m_permanent[resource.indexInPool].image;
            }
            return VK_NULL_HANDLE;
        case nrd::ResourceType::TRANSIENT_POOL:
            if (resource.indexInPool < m_transient.size()) {
                view = m_transient[resource.indexInPool].view;
                return m_transient[resource.indexInPool].image;
            }
            return VK_NULL_HANDLE;
        default:
            return VK_NULL_HANDLE;
    }
}

void NrdDenoiser::barrierForCompute(VkCommandBuffer commandBuffer, VkImage image) {
    auto it = m_layouts.find(image);
    VkImageLayout oldLayout = it == m_layouts.end() ? VK_IMAGE_LAYOUT_UNDEFINED : it->second;
    if (oldLayout == VK_IMAGE_LAYOUT_GENERAL) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    m_layouts[image] = VK_IMAGE_LAYOUT_GENERAL;
}

bool NrdDenoiser::denoise(VkCommandBuffer commandBuffer, const NrdInputs& inputs, const NrdCamera& camera) {
    if (m_instance == nullptr || commandBuffer == VK_NULL_HANDLE) return false;
    if (inputs.diffuse == VK_NULL_HANDLE || inputs.specular == VK_NULL_HANDLE ||
        inputs.normal == VK_NULL_HANDLE ||
        inputs.viewZ == VK_NULL_HANDLE || inputs.motion == VK_NULL_HANDLE) {
        return false;
    }

    m_layouts[inputs.diffuse] = VK_IMAGE_LAYOUT_GENERAL;
    m_layouts[inputs.specular] = VK_IMAGE_LAYOUT_GENERAL;
    m_layouts[inputs.normal] = VK_IMAGE_LAYOUT_GENERAL;
    m_layouts[inputs.viewZ] = VK_IMAGE_LAYOUT_GENERAL;
    m_layouts[inputs.motion] = VK_IMAGE_LAYOUT_GENERAL;

    float viewToClip[16]{};
    float viewToClipPrev[16]{};
    float worldToView[16]{};
    float worldToViewPrev[16]{};
    buildMatrices(camera, viewToClip, viewToClipPrev, worldToView, worldToViewPrev);

    nrd::CommonSettings common{};
    std::memcpy(common.viewToClipMatrix, viewToClip, sizeof(viewToClip));
    std::memcpy(common.viewToClipMatrixPrev, viewToClipPrev, sizeof(viewToClipPrev));
    std::memcpy(common.worldToViewMatrix, worldToView, sizeof(worldToView));
    std::memcpy(common.worldToViewMatrixPrev, worldToViewPrev, sizeof(worldToViewPrev));
    common.motionVectorScale[0] = m_width > 0 ? 1.0f / static_cast<float>(m_width) : 1.0f;
    common.motionVectorScale[1] = m_height > 0 ? 1.0f / static_cast<float>(m_height) : 1.0f;
    common.motionVectorScale[2] = 0.0f;
    common.cameraJitter[0] = camera.jitterX;
    common.cameraJitter[1] = camera.jitterY;
    common.cameraJitterPrev[0] = camera.prevJitterX;
    common.cameraJitterPrev[1] = camera.prevJitterY;
    common.resourceSize[0] = static_cast<uint16_t>(std::min<uint32_t>(m_width, 65535));
    common.resourceSize[1] = static_cast<uint16_t>(std::min<uint32_t>(m_height, 65535));
    uint32_t prevWidth = m_prevWidth > 0 ? m_prevWidth : m_width;
    uint32_t prevHeight = m_prevHeight > 0 ? m_prevHeight : m_height;
    common.resourceSizePrev[0] = static_cast<uint16_t>(std::min<uint32_t>(prevWidth, 65535));
    common.resourceSizePrev[1] = static_cast<uint16_t>(std::min<uint32_t>(prevHeight, 65535));
    common.rectSize[0] = common.resourceSize[0];
    common.rectSize[1] = common.resourceSize[1];
    common.rectSizePrev[0] = common.resourceSizePrev[0];
    common.rectSizePrev[1] = common.resourceSizePrev[1];
    common.viewZScale = 1.0f;
    common.timeDeltaBetweenFrames = camera.timeDelta;
    common.denoisingRange = 50000.0f;
    common.disocclusionThreshold = 0.022f;
    common.disocclusionThresholdAlternate = 0.075f;
    common.frameIndex = camera.frameIndex;
    common.accumulationMode = m_restart ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    common.isMotionVectorInWorldSpace = false;
    common.isHistoryConfidenceAvailable = false;
    common.isDisocclusionThresholdMixAvailable = false;
    common.enableValidation = false;

    nrd::Result result = nrd::SetCommonSettings(*m_instance, common);
    if (result != nrd::Result::SUCCESS) {
        std::cerr << "[NRD] SetCommonSettings failed result=" << static_cast<int>(result) << std::endl;
        return false;
    }

    const nrd::Identifier identifier = 0;
    const nrd::DispatchDesc* dispatches = nullptr;
    uint32_t dispatchCount = 0;
    result = nrd::GetComputeDispatches(*m_instance, &identifier, 1, dispatches, dispatchCount);
    if (result != nrd::Result::SUCCESS || dispatchCount == 0 || dispatches == nullptr) {
        std::cerr << "[NRD] GetComputeDispatches failed result=" << static_cast<int>(result)
                  << " count=" << dispatchCount << std::endl;
        return false;
    }

    if (camera.frameIndex < 2) {
        std::cout << "[NRD] denoise dispatches=" << dispatchCount << std::endl;
    }

    vkResetDescriptorPool(m_device, m_descriptorPool, 0);

    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    uint32_t constantOffset = 0;
    for (uint32_t d = 0; d < dispatchCount; d++) {
        const nrd::DispatchDesc& dispatch = dispatches[d];
        const nrd::PipelineDesc& pipelineDesc = m_instanceDesc->pipelines[dispatch.pipelineIndex];

        VkDescriptorSet sets[2]{};
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &m_resourcesLayout;
        if (vkAllocateDescriptorSets(m_device, &allocateInfo, &sets[0]) != VK_SUCCESS) return false;
        allocateInfo.pSetLayouts = &m_constantsLayout;
        if (vkAllocateDescriptorSets(m_device, &allocateInfo, &sets[1]) != VK_SUCCESS) return false;

        std::vector<VkDescriptorImageInfo> sampled;
        std::vector<VkDescriptorImageInfo> storage;
        sampled.reserve(dispatch.resourcesNum);
        storage.reserve(dispatch.resourcesNum);

        for (uint32_t i = 0; i < dispatch.resourcesNum; i++) {
            const nrd::ResourceDesc& resource = dispatch.resources[i];
            VkImageView view = VK_NULL_HANDLE;
            VkImage image = resolveImage(resource, inputs, view);
            if (image == VK_NULL_HANDLE || view == VK_NULL_HANDLE) {
                std::cerr << "[NRD] unresolved resource type="
                          << static_cast<int>(resource.type) << " dispatch=" << d << std::endl;
                return false;
            }
            barrierForCompute(commandBuffer, image);

            VkDescriptorImageInfo info{};
            info.sampler = VK_NULL_HANDLE;
            info.imageView = view;
            info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (resource.descriptorType == nrd::DescriptorType::TEXTURE) {
                sampled.push_back(info);
            } else {
                storage.push_back(info);
            }
        }

        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(sampled.size() + storage.size());
        uint32_t textureOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.textureOffset;
        uint32_t storageOffset = nrd::GetLibraryDesc()->spirvBindingOffsets.storageTextureAndBufferOffset;

        for (uint32_t i = 0; i < sampled.size(); i++) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sets[0];
            write.dstBinding = textureOffset + i;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.pImageInfo = &sampled[i];
            writes.push_back(write);
        }
        for (uint32_t i = 0; i < storage.size(); i++) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sets[0];
            write.dstBinding = storageOffset + i;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo = &storage[i];
            writes.push_back(write);
        }

        if (!writes.empty()) {
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_constantBuffer;
        bufferInfo.offset = constantOffset;
        bufferInfo.range = std::max<uint32_t>(1, dispatch.constantBufferDataSize);

        VkWriteDescriptorSet constantsWrite{};
        constantsWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        constantsWrite.dstSet = sets[1];
        constantsWrite.dstBinding = nrd::GetLibraryDesc()->spirvBindingOffsets.constantBufferOffset;
        constantsWrite.descriptorCount = 1;
        constantsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        constantsWrite.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device, 1, &constantsWrite, 0, nullptr);

        if (dispatch.constantBufferData != nullptr && dispatch.constantBufferDataSize > 0) {
            uint32_t size = alignUp(dispatch.constantBufferDataSize, 4);
            if (constantOffset + size <= m_constantCapacity) {
                vkCmdUpdateBuffer(commandBuffer, m_constantBuffer, constantOffset, size,
                                  dispatch.constantBufferData);
            }
        }

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[dispatch.pipelineIndex]);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipelineLayout, 0, 2, sets, 0, nullptr);
        vkCmdDispatch(commandBuffer, dispatch.gridWidth, dispatch.gridHeight, 1);

        constantOffset += static_cast<uint32_t>(m_constantStride);
        if (constantOffset + m_constantStride > m_constantCapacity) {
            constantOffset = 0;
        }
    }

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    m_prevWidth = m_width;
    m_prevHeight = m_height;
    m_restart = false;
    return true;
}
