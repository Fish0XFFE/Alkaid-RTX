#include "VulkanContext.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData) {
    (void)type;
    (void)userData;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[VK Validation] " << data->pMessage << std::endl;
    }
    return VK_FALSE;
}

VkResult createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* messenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!func) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return func(instance, createInfo, allocator, messenger);
}

void destroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* allocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) func(instance, messenger, allocator);
}

} // namespace

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    shutdown();
}

bool VulkanContext::initialize() {
    std::cout << "[DBG] VK 1 - createInstance" << std::endl;
    if (!createInstance()) return false;
    std::cout << "[DBG] VK 2 - pickPhysicalDevice" << std::endl;
    if (!pickPhysicalDevice()) return false;
    std::cout << "[DBG] VK 3 - createLogicalDevice" << std::endl;
    if (!createLogicalDevice()) return false;
    std::cout << "[DBG] VK 4 - done" << std::endl;
    return true;
}

void VulkanContext::shutdown() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        if (m_virtualVramMapped != nullptr) {
            vkUnmapMemory(m_device, m_virtualVramMemory);
            m_virtualVramMapped = nullptr;
        }
        if (m_virtualVramBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_virtualVramBuffer, nullptr);
            m_virtualVramBuffer = VK_NULL_HANDLE;
        }
        if (m_virtualVramMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_virtualVramMemory, nullptr);
            m_virtualVramMemory = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
        m_queue = VK_NULL_HANDLE;
    }
    if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        destroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

void VulkanContext::reserveVirtualVram(bool enabled, uint32_t gigabytes) {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    if (m_virtualVramMapped != nullptr) {
        vkUnmapMemory(m_device, m_virtualVramMemory);
        m_virtualVramMapped = nullptr;
    }
    if (m_virtualVramBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_virtualVramBuffer, nullptr);
        m_virtualVramBuffer = VK_NULL_HANDLE;
    }
    if (m_virtualVramMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_virtualVramMemory, nullptr);
        m_virtualVramMemory = VK_NULL_HANDLE;
    }
    m_virtualVramSize = 0;

    if (!enabled || gigabytes == 0) return;

    VkDeviceSize requested = static_cast<VkDeviceSize>(gigabytes) * 1024ULL * 1024ULL * 1024ULL;
    VkDeviceSize size = std::min<VkDeviceSize>(requested, 4ULL * 1024ULL * 1024ULL * 1024ULL);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_virtualVramBuffer) != VK_SUCCESS) {
        m_virtualVramBuffer = VK_NULL_HANDLE;
        std::cerr << "[Yaoguang] virtual VRAM buffer creation failed" << std::endl;
        return;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_virtualVramBuffer, &requirements);

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &properties);

    uint32_t memoryType = UINT32_MAX;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) {
        vkDestroyBuffer(m_device, m_virtualVramBuffer, nullptr);
        m_virtualVramBuffer = VK_NULL_HANDLE;
        return;
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocate, nullptr, &m_virtualVramMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, m_virtualVramBuffer, nullptr);
        m_virtualVramBuffer = VK_NULL_HANDLE;
        m_virtualVramMemory = VK_NULL_HANDLE;
        std::cerr << "[Yaoguang] virtual VRAM allocation failed" << std::endl;
        return;
    }
    vkBindBufferMemory(m_device, m_virtualVramBuffer, m_virtualVramMemory, 0);
    if (vkMapMemory(m_device, m_virtualVramMemory, 0, size, 0, &m_virtualVramMapped) != VK_SUCCESS) {
        m_virtualVramMapped = nullptr;
        return;
    }

    VkDeviceSize touch = std::min<VkDeviceSize>(size, 64ULL * 1024ULL * 1024ULL);
    std::memset(m_virtualVramMapped, 0, static_cast<size_t>(touch));
    m_virtualVramSize = size;
    std::cout << "[Yaoguang] virtual VRAM reserved: " << (size / (1024 * 1024)) << " MB" << std::endl;
}

bool VulkanContext::createInstance() {
    std::cout << "[DBG] C1 - setup appInfo" << std::endl;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Yaoguang Ray Tracing";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Yaoguang";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::cout << "[DBG] C2 - appInfo set, apiVersion=" << appInfo.apiVersion << std::endl;

    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExts(instExtCount);
    if (instExtCount > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExts.data());
    }

    std::set<std::string> supportedInstExts;
    for (auto& e : instExts) {
        supportedInstExts.insert(e.extensionName);
    }

    std::vector<const char*> instanceExts;
    bool validationEnabled = false;
    const char* validationEnv = std::getenv("YAOGUANG_VALIDATION");
    if (validationEnv != nullptr && validationEnv[0] == '1' &&
        supportedInstExts.count(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        if (layerCount > 0) vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        for (const auto& layer : layers) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                validationEnabled = true;
                break;
            }
        }
        if (validationEnabled) {
            instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    if (supportedInstExts.count(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)) {
        instanceExts.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        m_externalMemoryInstanceSupported = true;
    } else {
        std::cerr << "[Yaoguang] VK_KHR_external_memory_capabilities not supported" << std::endl;
    }

    if (supportedInstExts.count(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME)) {
        instanceExts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
        m_externalSemaphoreInstanceSupported = true;
    } else {
        std::cerr << "[Yaoguang] VK_KHR_external_semaphore_capabilities not supported" << std::endl;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = validationEnabled ? 1u : 0u;
    createInfo.ppEnabledLayerNames = validationEnabled ? &validationLayer : nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    createInfo.ppEnabledExtensionNames = instanceExts.empty() ? nullptr : instanceExts.data();
    createInfo.pNext = validationEnabled ? &debugInfo : nullptr;

    std::cout << "[DBG] C3 - createInfo set, instExtCount=" << instanceExts.size() << std::endl;
    std::cout << "[DBG] C4 - calling vkCreateInstance" << std::endl;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);

    std::cout << "[DBG] C5 - vkCreateInstance returned code=" << (int)result << std::endl;

    if (result != VK_SUCCESS) {
        std::cerr << "[Yaoguang] vkCreateInstance failed: " << result << std::endl;
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    std::cout << "[DBG] C6 - instance created OK" << std::endl;
    if (validationEnabled) {
        if (createDebugUtilsMessengerEXT(m_instance, &debugInfo, nullptr, &m_debugMessenger) == VK_SUCCESS) {
            std::cout << "[Yaoguang] validation enabled" << std::endl;
        }
    }
    return true;
}

bool VulkanContext::deviceHasRtExtensions(VkPhysicalDevice dev) {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    if (extCount == 0) return false;
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());

    bool hasAS = false, hasRT = false;
    for (auto& e : exts) {
        if (strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0) hasAS = true;
        if (strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0) hasRT = true;
    }
    return hasAS && hasRT;
}

GpuRayTracingUnit VulkanContext::detectRayTracingUnit(VkPhysicalDevice dev, bool hasAS, bool hasRT) {
    if (!hasAS || !hasRT) return GpuRayTracingUnit::NONE;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
    asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    asProps.pNext = &rtProps;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &asProps;
    vkGetPhysicalDeviceProperties2(dev, &props2);

    if (rtProps.shaderGroupHandleSize == 0 || asProps.maxGeometryCount == 0) {
        return GpuRayTracingUnit::NONE;
    }

    switch (props.vendorID) {
        case 0x10DE: return GpuRayTracingUnit::RT_CORE;
        case 0x1002:
        case 0x1022: return GpuRayTracingUnit::RAY_ACCELERATOR;
        case 0x8086: return GpuRayTracingUnit::RTU;
        default: return GpuRayTracingUnit::NONE;
    }
}

bool VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    VkResult enumResult = vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (enumResult != VK_SUCCESS || count == 0) return false;

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    struct Candidate {
        VkPhysicalDevice device;
        std::string name;
        GpuRayTracingUnit unit;
        bool hasRt;
        int score;
        bool discrete;
    };

    std::vector<Candidate> candidates;

    for (auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        bool hasRt = deviceHasRtExtensions(dev);
        GpuRayTracingUnit unit = detectRayTracingUnit(dev, hasRt, hasRt);

        bool discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        bool integrated = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);

        int score = 0;
        if (discrete) score += 1000;
        else if (integrated) score += 100;
        if (unit != GpuRayTracingUnit::NONE) score += 5000;

        Candidate c;
        c.device = dev;
        c.name = props.deviceName;
        c.unit = unit;
        c.hasRt = hasRt;
        c.score = score;
        c.discrete = discrete;
        candidates.push_back(c);
    }

    Candidate* best = nullptr;

    for (auto& c : candidates) {
        if (c.discrete && c.unit != GpuRayTracingUnit::NONE) {
            if (!best || c.score > best->score) best = &c;
        }
    }

    if (!best) {
        for (auto& c : candidates) {
            if (!c.discrete && c.unit != GpuRayTracingUnit::NONE) {
                if (!best || c.score > best->score) best = &c;
            }
        }
    }

    if (!best) {
        for (auto& c : candidates) {
            if (c.discrete) {
                if (!best || c.score > best->score) best = &c;
            }
        }
    }

    if (!best) {
        for (auto& c : candidates) {
            if (!best || c.score > best->score) best = &c;
        }
    }

    if (!best) return false;

    m_physicalDevice = best->device;
    m_deviceName = best->name;
    m_rayTracingUnit = best->unit;
    m_rayTracingSupported = (best->unit != GpuRayTracingUnit::NONE) && best->hasRt;

    std::cout << "[Yaoguang] Selected GPU: " << m_deviceName
              << " | Unit: " << static_cast<int>(m_rayTracingUnit)
              << " | RT: " << (m_rayTracingSupported ? "YES" : "NO") << std::endl;

    return true;
}

bool VulkanContext::createLogicalDevice() {
    if (m_physicalDevice == VK_NULL_HANDLE) return false;

    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, queues.data());

    bool foundQueue = false;
    for (uint32_t i = 0; i < queueCount; i++) {
        if ((queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            m_queueFamilyIndex = i;
            foundQueue = true;
            break;
        }
    }
    if (!foundQueue) {
        for (uint32_t i = 0; i < queueCount; i++) {
            if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                m_queueFamilyIndex = i;
                foundQueue = true;
                break;
            }
        }
    }
    if (!foundQueue) return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, availableExts.data());

    std::set<std::string> supportedExtNames;
    for (auto& e : availableExts) supportedExtNames.insert(e.extensionName);

    std::vector<const char*> deviceExts;
    auto addExt = [&](const char* name) {
        if (supportedExtNames.count(name)) deviceExts.push_back(name);
    };

    bool derivativeSupported = supportedExtNames.count(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) != 0;

    if (m_rayTracingSupported) {
        addExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        addExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        addExt(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        addExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        addExt(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        addExt(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        addExt(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        addExt(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
        addExt(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        addExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        addExt(VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME);
        addExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        addExt(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
        addExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        if (derivativeSupported) {
            addExt(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        }
    }

    if (m_externalMemoryInstanceSupported) {
        addExt(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        addExt(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        addExt(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
        addExt(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    }

    if (m_externalSemaphoreInstanceSupported) {
        addExt(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        addExt(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
    }

    addExt(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
    addExt(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
    addExt(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures supportedCore{};
    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 supportQuery{};
    supportQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportQuery.pNext = &supported12;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportQuery);
    supportedCore = supportQuery.features;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.shaderInt64 = supportedCore.shaderInt64;
    features2.features.shaderInt16 = supportedCore.shaderInt16;
    features2.features.shaderImageGatherExtended = supportedCore.shaderImageGatherExtended;
    features2.features.shaderStorageImageExtendedFormats = supportedCore.shaderStorageImageExtendedFormats;
    features2.features.shaderStorageImageWriteWithoutFormat = supportedCore.shaderStorageImageWriteWithoutFormat;
    features2.features.shaderStorageImageReadWithoutFormat = supportedCore.shaderStorageImageReadWithoutFormat;
    features2.features.fragmentStoresAndAtomics = supportedCore.fragmentStoresAndAtomics;
    features2.features.vertexPipelineStoresAndAtomics = supportedCore.vertexPipelineStoresAndAtomics;
    features2.features.shaderSampledImageArrayDynamicIndexing = supportedCore.shaderSampledImageArrayDynamicIndexing;
    features2.features.shaderStorageBufferArrayDynamicIndexing = supportedCore.shaderStorageBufferArrayDynamicIndexing;
    features2.features.shaderUniformBufferArrayDynamicIndexing = supportedCore.shaderUniformBufferArrayDynamicIndexing;

    VkPhysicalDeviceVulkan12Features vk12Features{};
    vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12Features.timelineSemaphore = supported12.timelineSemaphore;
    vk12Features.descriptorIndexing = supported12.descriptorIndexing;
    vk12Features.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
    vk12Features.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
    vk12Features.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
    vk12Features.shaderStorageBufferArrayNonUniformIndexing = supported12.shaderStorageBufferArrayNonUniformIndexing;
    vk12Features.shaderUniformBufferArrayNonUniformIndexing = supported12.shaderUniformBufferArrayNonUniformIndexing;
    vk12Features.storageBuffer8BitAccess = supported12.storageBuffer8BitAccess;
    vk12Features.uniformAndStorageBuffer8BitAccess = supported12.uniformAndStorageBuffer8BitAccess;
    vk12Features.scalarBlockLayout = supported12.scalarBlockLayout;
    vk12Features.hostQueryReset = supported12.hostQueryReset;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{};
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeFeatures{};

    void** ppNext = &features2.pNext;
    *ppNext = &vk12Features; ppNext = &vk12Features.pNext;

    if (m_rayTracingSupported) {
        rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
        rtPipelineFeatures.pNext = nullptr;

        accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelFeatures.accelerationStructure = VK_TRUE;
        accelFeatures.pNext = nullptr;

        bdaFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bdaFeatures.bufferDeviceAddress = VK_TRUE;
        bdaFeatures.pNext = nullptr;

        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.rayQuery = VK_TRUE;
        rayQueryFeatures.pNext = nullptr;

        *ppNext = &rtPipelineFeatures; ppNext = &rtPipelineFeatures.pNext;
        *ppNext = &accelFeatures;     ppNext = &accelFeatures.pNext;
        *ppNext = &bdaFeatures;       ppNext = &bdaFeatures.pNext;
        *ppNext = &rayQueryFeatures;

        if (derivativeSupported) {
            derivativeFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
            derivativeFeatures.computeDerivativeGroupQuads = VK_TRUE;
            *ppNext = &derivativeFeatures;
        }
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &features2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    deviceInfo.ppEnabledExtensionNames = deviceExts.data();

    VkResult result = vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        std::cerr << "[Yaoguang] vkCreateDevice failed: " << result << std::endl;
        m_device = VK_NULL_HANDLE;
        return false;
    }

    vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);

    return true;
}