#include "VulkanContext.h"
#include "DlssUpscaler.h"

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <cstring>

namespace {

struct TestImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0;
}

bool createImage(VkDevice device, VkPhysicalDevice physical, TestImage& out, uint32_t width, uint32_t height,
                 VkFormat format, bool storage) {
    out.format = format;
    out.width = width;
    out.height = height;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (storage) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, out.image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(device, out.image, out.memory, 0) != VK_SUCCESS) return false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(device, &viewInfo, nullptr, &out.view) == VK_SUCCESS;
}

void destroyImage(VkDevice device, TestImage& image) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

}

int main(int argc, char** argv) {
    std::string dllDir = argc > 1 ? argv[1]
        : "C:/Users/fish/Desktop/yaoguang/src/main/resources/native/windows/x86_64";
    uint32_t inWidth = argc > 2 ? static_cast<uint32_t>(std::stoul(argv[2])) : 1280;
    uint32_t inHeight = argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 720;
    uint32_t outWidth = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 1920;
    uint32_t outHeight = argc > 5 ? static_cast<uint32_t>(std::stoul(argv[5])) : 1080;
    int quality = argc > 6 ? std::stoi(argv[6]) : 0;

    VulkanContext context;
    if (!context.initialize()) {
        std::cout << "vulkan-init-failed" << std::endl;
        return 2;
    }
    std::cout << "device-ready" << std::endl;

    DlssUpscaler dlss;
    bool ok = dlss.initialize(context.getInstance(), context.getPhysicalDevice(), context.getDevice(),
                              context.getQueue(), context.getQueueFamilyIndex(), dllDir);
    std::cout << "init=" << ok << " status=" << dlss.status() << std::endl;
    if (!ok) {
        context.shutdown();
        return 1;
    }
    dlss.setQuality(quality);
    dlss.resize(inWidth, inHeight, outWidth, outHeight);

    VkDevice device = context.getDevice();
    TestImage color, depth, motion, output;
    if (!createImage(device, context.getPhysicalDevice(), color, inWidth, inHeight, VK_FORMAT_R16G16B16A16_SFLOAT, false) ||
        !createImage(device, context.getPhysicalDevice(), depth, inWidth, inHeight, VK_FORMAT_R32_SFLOAT, false) ||
        !createImage(device, context.getPhysicalDevice(), motion, inWidth, inHeight, VK_FORMAT_R16G16_SFLOAT, false) ||
        !createImage(device, context.getPhysicalDevice(), output, outWidth, outHeight, VK_FORMAT_R16G16B16A16_SFLOAT, true)) {
        std::cout << "image-failed" << std::endl;
        return 3;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.getQueueFamilyIndex();
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &poolInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    {
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        auto clear = [&](TestImage& image, VkClearColorValue value) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        };
        VkClearColorValue colorValue{};
        colorValue.float32[0] = 0.5f;
        colorValue.float32[1] = 0.4f;
        colorValue.float32[2] = 0.3f;
        colorValue.float32[3] = 1.0f;
        VkClearColorValue depthValue{};
        depthValue.float32[0] = 0.5f;
        VkClearColorValue motionValue{};
        clear(color, colorValue);
        clear(depth, depthValue);
        clear(motion, motionValue);
        VkClearColorValue outputValue{};
        outputValue.float32[3] = 1.0f;
        clear(output, outputValue);
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(context.getQueue(), 1, &submitInfo, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
        vkResetFences(device, 1, &fence);
    }

    for (int frame = 0; frame < 3; ++frame) {
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        auto transition = [&](TestImage& image) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        };

        bool evaluated = dlss.evaluate(commandBuffer,
                                      color.image, color.view,
                                      depth.image, depth.view,
                                      motion.image, motion.view,
                                      output.image, output.view,
                                      0.5f, -0.25f, frame == 0);
        std::cout << "frame=" << frame << " evaluate=" << evaluated << " status=" << dlss.status() << std::endl;

        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        VkResult submit = vkQueueSubmit(context.getQueue(), 1, &submitInfo, fence);
        if (submit != VK_SUCCESS) {
            std::cout << "submit-failed " << submit << std::endl;
            break;
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
        vkResetFences(device, 1, &fence);
    }

    vkDeviceWaitIdle(device);
    std::cout << "final status=" << dlss.status() << std::endl;

    {
        VkDeviceSize bytes = static_cast<VkDeviceSize>(outWidth) * outHeight * 8;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer readback = VK_NULL_HANDLE;
        vkCreateBuffer(device, &bufferInfo, nullptr, &readback);
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, readback, &requirements);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = findMemoryType(context.getPhysicalDevice(), requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        vkAllocateMemory(device, &alloc, nullptr, &memory);
        vkBindBufferMemory(device, readback, memory, 0);

        vkResetCommandBuffer(commandBuffer, 0);
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
        barrier.image = output.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {outWidth, outHeight, 1};
        vkCmdCopyImageToBuffer(commandBuffer, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(context.getQueue(), 1, &submitInfo, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
        vkResetFences(device, 1, &fence);

        void* mapped = nullptr;
        vkMapMemory(device, memory, 0, bytes, 0, &mapped);
        const uint16_t* halves = static_cast<const uint16_t*>(mapped);
        double sum = 0.0;
        float maxValue = 0.0f;
        uint32_t nonfinite = 0;
        for (uint32_t i = 0; i < outWidth * outHeight * 4; i++) {
            uint16_t h = halves[i];
            uint32_t sign = (h >> 15) & 1u;
            uint32_t exponent = (h >> 10) & 0x1Fu;
            uint32_t mantissa = h & 0x3FFu;
            uint32_t bits = 0;
            if (exponent == 0) {
                bits = sign << 31;
            } else if (exponent == 31) {
                bits = (sign << 31) | 0x7F800000u;
            } else {
                bits = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
            }
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            if (!(value == value) || value > 1.0e30f) nonfinite++;
            sum += value;
            if (value > maxValue) maxValue = value;
        }
        vkUnmapMemory(device, memory);
        std::cout << "dlss-output mean=" << (sum / (outWidth * outHeight * 4))
                  << " max=" << maxValue << " nonfinite=" << nonfinite << std::endl;
        vkDestroyBuffer(device, readback, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }

    destroyImage(device, color);
    destroyImage(device, depth);
    destroyImage(device, motion);
    destroyImage(device, output);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    std::cout << "resources-destroyed" << std::endl;

    dlss.shutdown();
    std::cout << "dlss-shutdown-ok" << std::endl;
    context.shutdown();
    std::cout << "done" << std::endl;
    return 0;
}
