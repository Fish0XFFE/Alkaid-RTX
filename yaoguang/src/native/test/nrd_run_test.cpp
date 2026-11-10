#include "VulkanContext.h"
#include "NrdDenoiser.h"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

struct TestImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    VkDeviceSize bytes = 0;
};

static uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    return UINT32_MAX;
}

static bool createImage(VkDevice device, VkPhysicalDevice physical, TestImage& target,
                        uint32_t width, uint32_t height, VkFormat format, uint32_t channels) {
    target.width = width;
    target.height = height;
    target.format = format;
    target.channels = channels;
    uint32_t bytesPerPixel = format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8u
            : format == VK_FORMAT_R16G16_SFLOAT ? 4u
            : channels == 4 ? 4u : 4u;
    target.bytes = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &target.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, target.image, &requirements);
    uint32_t type = findMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return false;

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &allocate, nullptr, &target.memory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(device, target.image, target.memory, 0) != VK_SUCCESS) return false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = target.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &viewInfo, nullptr, &target.view) == VK_SUCCESS;
}

static void uploadImage(VkDevice device, VkPhysicalDevice physical, VkCommandPool pool, VkQueue queue,
                        TestImage& target, const void* data) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = target.bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    vkCreateBuffer(device, &bufferInfo, nullptr, &staging);
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, staging, &requirements);
    uint32_t type = findMemoryType(physical, requirements.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device, &allocate, nullptr, &memory);
    vkBindBufferMemory(device, staging, memory, 0);
    void* mapped = nullptr;
    vkMapMemory(device, memory, 0, target.bytes, 0, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(target.bytes));
    vkUnmapMemory(device, memory);

    VkCommandBufferAllocateInfo commandAllocate{};
    commandAllocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocate.commandPool = pool;
    commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocate.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &commandAllocate, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {target.width, target.height, 1};
    vkCmdCopyBufferToImage(commandBuffer, staging, target.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, memory, nullptr);
}


static uint16_t floatToHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) return static_cast<uint16_t>(sign);
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

static float halfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x1u;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign << 31;
        } else {
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FFu;
            bits = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    } else {
        bits = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

static std::vector<float> readback(VkDevice device, VkPhysicalDevice physical, VkCommandPool pool, VkQueue queue,
                                   VkImage image, uint32_t width, uint32_t height, uint32_t channels) {
    VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * channels * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer = VK_NULL_HANDLE;
    vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    uint32_t type = findMemoryType(physical, requirements.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device, &allocate, nullptr, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);

    VkCommandBufferAllocateInfo commandAllocate{};
    commandAllocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocate.commandPool = pool;
    commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocate.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &commandAllocate, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    std::vector<float> result(static_cast<size_t>(width) * height * channels);
    void* mapped = nullptr;
    vkMapMemory(device, memory, 0, bytes, 0, &mapped);
    std::memcpy(result.data(), mapped, static_cast<size_t>(bytes));
    vkUnmapMemory(device, memory);

    vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    return result;
}

int main() {
    VulkanContext context;
    if (!context.initialize()) {
        std::cerr << "vulkan init failed" << std::endl;
        return 1;
    }
    VkDevice device = context.getDevice();
    VkPhysicalDevice physical = context.getPhysicalDevice();
    VkQueue queue = context.getQueue();
    uint32_t family = context.getQueueFamilyIndex();

    const uint32_t W = 320;
    const uint32_t H = 180;

    NrdDenoiser denoiser;
    if (!denoiser.initialize(device, physical, queue, family, W, H)) {
        std::cerr << "nrd init failed" << std::endl;
        return 2;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &poolInfo, nullptr, &pool);

    TestImage diffuse;
    TestImage specular;
    TestImage normal;
    TestImage viewZ;
    TestImage motion;
    createImage(device, physical, diffuse, W, H, VK_FORMAT_R16G16B16A16_SFLOAT, 4);
    createImage(device, physical, specular, W, H, VK_FORMAT_R16G16B16A16_SFLOAT, 4);
    createImage(device, physical, normal, W, H, VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4);
    createImage(device, physical, viewZ, W, H, VK_FORMAT_R32_SFLOAT, 1);
    createImage(device, physical, motion, W, H, VK_FORMAT_R16G16_SFLOAT, 2);

    std::vector<uint16_t> diffuseData(static_cast<size_t>(W) * H * 4);
    std::vector<uint16_t> specularData(static_cast<size_t>(W) * H * 4);
    std::vector<float> viewZData(static_cast<size_t>(W) * H);
    std::vector<uint32_t> normalData(static_cast<size_t>(W) * H);
    std::vector<uint16_t> motionData(static_cast<size_t>(W) * H * 2);
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            size_t i = static_cast<size_t>(y) * W + x;
            float value = 0.4f + 0.3f * std::sin(x * 0.15f) * std::cos(y * 0.19f);
            diffuseData[i * 4 + 0] = floatToHalf(value);
            diffuseData[i * 4 + 1] = floatToHalf(value * 0.9f);
            diffuseData[i * 4 + 2] = floatToHalf(value * 0.8f);
            diffuseData[i * 4 + 3] = floatToHalf(20.0f);
            specularData[i * 4 + 0] = floatToHalf(value * 0.5f);
            specularData[i * 4 + 1] = floatToHalf(value * 0.5f);
            specularData[i * 4 + 2] = floatToHalf(value * 0.5f);
            specularData[i * 4 + 3] = floatToHalf(30.0f);
            viewZData[i] = 20.0f;
            uint32_t nx = 512u;
            uint32_t ny = 512u;
            uint32_t nz = 1023u;
            uint32_t material = 0u;
            normalData[i] = (material << 30) | (nz << 20) | (ny << 10) | nx;
            motionData[i * 2 + 0] = floatToHalf(0.0f);
            motionData[i * 2 + 1] = floatToHalf(0.0f);
        }
    }
    uploadImage(device, physical, pool, queue, diffuse, diffuseData.data());
    uploadImage(device, physical, pool, queue, specular, specularData.data());
    uploadImage(device, physical, pool, queue, normal, normalData.data());
    uploadImage(device, physical, pool, queue, viewZ, viewZData.data());
    uploadImage(device, physical, pool, queue, motion, motionData.data());

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);

    NrdInputs inputs{};
    inputs.diffuse = diffuse.image;
    inputs.diffuseView = diffuse.view;
    inputs.specular = specular.image;
    inputs.specularView = specular.view;
    inputs.normal = normal.image;
    inputs.normalView = normal.view;
    inputs.viewZ = viewZ.image;
    inputs.viewZView = viewZ.view;
    inputs.motion = motion.image;
    inputs.motionView = motion.view;

    NrdCamera camera{};
    camera.posX = 0.0f;
    camera.posY = 10.0f;
    camera.posZ = 0.0f;
    camera.dirX = 0.0f;
    camera.dirY = 0.0f;
    camera.dirZ = 1.0f;
    camera.prevDirZ = 1.0f;
    camera.timeDelta = 1.0f / 60.0f;

    bool ok = true;
    for (uint32_t frame = 0; frame < 4; frame++) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        camera.frameIndex = frame;
        if (!denoiser.denoise(commandBuffer, inputs, camera)) {
            std::cerr << "denoise failed frame " << frame << std::endl;
            ok = false;
        }
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            std::cerr << "submit failed" << std::endl;
            ok = false;
        }
        vkQueueWaitIdle(queue);
        vkResetCommandBuffer(commandBuffer, 0);
    }

    for (int which = 0; which < 2; which++) {
        VkImage image = which == 0 ? denoiser.getDiffuseImage() : denoiser.getSpecularImage();
        std::vector<float> raw = readback(device, physical, pool, queue, image, W, H, 4);
        std::vector<float> data(static_cast<size_t>(W) * H * 4);
        const uint16_t* halves = reinterpret_cast<const uint16_t*>(raw.data());
        for (size_t i = 0; i < data.size(); i++) data[i] = halfToFloat(halves[i]);
        double sum = 0.0;
        float minValue = 1.0e30f;
        float maxValue = -1.0e30f;
        uint32_t bad = 0;
        for (size_t i = 0; i < static_cast<size_t>(W) * H; i++) {
            for (int c = 0; c < 3; c++) {
                float v = data[i * 4 + c];
                if (!std::isfinite(v)) {
                    bad++;
                    continue;
                }
                sum += v;
                minValue = std::min(minValue, v);
                maxValue = std::max(maxValue, v);
            }
        }
        double count = static_cast<double>(W) * H * 3;
        std::cout << (which == 0 ? "diffuse" : "specular")
                  << " mean=" << (sum / count) << " min=" << minValue << " max=" << maxValue
                  << " nonfinite=" << bad << std::endl;
    }
    std::cout << "nrd run " << (ok ? "ok" : "failed") << std::endl;
    std::cout << "diffuse view=" << (void*)denoiser.getDiffuseView()
              << " specular view=" << (void*)denoiser.getSpecularView() << std::endl;

    vkDeviceWaitIdle(device);
    vkDestroyCommandPool(device, pool, nullptr);
    denoiser.shutdown();
    context.shutdown();
    return ok ? 0 : 3;
}
