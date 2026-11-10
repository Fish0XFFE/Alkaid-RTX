#include <vulkan/vulkan.h>
#include <NRD.h>
#include <iostream>
#include <vector>

static const char* encName(nrd::NormalEncoding e) {
    switch (e) {
    case nrd::NormalEncoding::RGBA8_UNORM: return "RGBA8_UNORM";
    case nrd::NormalEncoding::RGBA8_SNORM: return "RGBA8_SNORM";
    case nrd::NormalEncoding::R10_G10_B10_A2_UNORM: return "R10_G10_B10_A2_UNORM";
    case nrd::NormalEncoding::RGBA16_UNORM: return "RGBA16_UNORM";
    case nrd::NormalEncoding::RGBA16_SNORM: return "RGBA16_SNORM";
    default: return "?";
    }
}

static const char* roughName(nrd::RoughnessEncoding e) {
    switch (e) {
    case nrd::RoughnessEncoding::SQ_LINEAR: return "SQ_LINEAR";
    case nrd::RoughnessEncoding::LINEAR: return "LINEAR";
    case nrd::RoughnessEncoding::SQRT_LINEAR: return "SQRT_LINEAR";
    default: return "?";
    }
}

int main() {
    const nrd::LibraryDesc* lib = nrd::GetLibraryDesc();
    std::cout << "nrd " << int(lib->versionMajor) << "." << int(lib->versionMinor) << "." << int(lib->versionBuild) << "\n";
    std::cout << "normalEncoding=" << encName(lib->normalEncoding) << " (" << int(lib->normalEncoding) << ")\n";
    std::cout << "roughnessEncoding=" << roughName(lib->roughnessEncoding) << " (" << int(lib->roughnessEncoding) << ")\n";
    std::cout << "denoisers=";
    for (uint32_t i = 0; i < lib->supportedDenoisersNum; i++) std::cout << int(lib->supportedDenoisers[i]) << " ";
    std::cout << "\n";

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        std::cout << "instance failed\n";
        return 1;
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    VkPhysicalDevice phys = devices.empty() ? VK_NULL_HANDLE : devices[0];
    float prio = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &prio;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(phys, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        std::cout << "device failed\n";
        return 1;
    }

    VkFormat formats[] = {
        VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R8G8_UNORM
    };
    for (VkFormat f : formats) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(phys, f, &props);
        bool storage = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
        std::cout << "format " << f << " storage=" << storage << " sampled="
                  << ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0) << "\n";
    }

    nrd::DenoiserDesc desc{};
    desc.identifier = 0;
    desc.denoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
    nrd::InstanceCreationDesc creation{};
    creation.denoisers = &desc;
    creation.denoisersNum = 1;
    nrd::Instance* instanceHandle = nullptr;
    nrd::Result res = nrd::CreateInstance(creation, instanceHandle);
    std::cout << "CreateInstance RELAX_DIFFUSE_SPECULAR result=" << int(res) << "\n";
    if (instanceHandle) {
        const nrd::InstanceDesc* inst = nrd::GetInstanceDesc(*instanceHandle);
        std::cout << "pipelines=" << inst->pipelinesNum << " permanent=" << inst->permanentPoolSize
                  << " transient=" << inst->transientPoolSize << "\n";
        nrd::RelaxSettings settings{};
        std::cout << "SetDenoiserSettings=" << int(nrd::SetDenoiserSettings(*instanceHandle, 0, &settings)) << "\n";
        nrd::DestroyInstance(*instanceHandle);
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::cout << "done\n";
    return 0;
}
