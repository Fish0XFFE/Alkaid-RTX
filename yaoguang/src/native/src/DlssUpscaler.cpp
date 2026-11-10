#include "DlssUpscaler.h"

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace {
constexpr unsigned long long APP_ID = 231313132ull;
constexpr const char* PROJECT_ID = "8f2c1a9e-4b3d-4f6a-8c21-7d5e9b0a3c64";

using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using InitProjectFn = NVSDK_NGX_Result(NVSDK_CONV*)(const char*, NVSDK_NGX_EngineType, const char*,
    const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr,
    PFN_vkGetDeviceProcAddr, const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version);
using InitSimpleFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, NVSDK_NGX_Version);
using GetParamsFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
using CreateFn = NVSDK_NGX_Result(NVSDK_CONV*)(VkDevice, VkCommandBuffer, NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFn = NVSDK_NGX_Result(NVSDK_CONV*)(VkCommandBuffer, const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(VkDevice);
using DestroyParamsFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);

NVSDK_NGX_Resource_VK makeResource(VkImage image, VkImageView view, VkFormat format,
                                   uint32_t width, uint32_t height, bool readWrite) {
    NVSDK_NGX_Resource_VK resource{};
    resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    resource.ReadWrite = readWrite;
    resource.Resource.ImageViewInfo.ImageView = view;
    resource.Resource.ImageViewInfo.Image = image;
    resource.Resource.ImageViewInfo.SubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    resource.Resource.ImageViewInfo.SubresourceRange.baseMipLevel = 0;
    resource.Resource.ImageViewInfo.SubresourceRange.levelCount = 1;
    resource.Resource.ImageViewInfo.SubresourceRange.baseArrayLayer = 0;
    resource.Resource.ImageViewInfo.SubresourceRange.layerCount = 1;
    resource.Resource.ImageViewInfo.Format = format;
    resource.Resource.ImageViewInfo.Width = width;
    resource.Resource.ImageViewInfo.Height = height;
    return resource;
}
}

DlssUpscaler::DlssUpscaler() = default;

DlssUpscaler::~DlssUpscaler() {
    shutdown();
}

bool DlssUpscaler::loadCore(const std::string& dllDir) {
    std::vector<std::wstring> candidates;
    std::wstring dir(dllDir.begin(), dllDir.end());
    candidates.push_back(dir + L"\\_nvngx.dll");
    candidates.push_back(dir + L"\\nvngx.dll");

    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0) {
        candidates.emplace_back(std::wstring(systemDir) + L"\\_nvngx.dll");
        candidates.emplace_back(std::wstring(systemDir) + L"\\nvngx.dll");
    }

    try {
        const std::filesystem::path repository = L"C:\\Windows\\System32\\DriverStore\\FileRepository";
        if (std::filesystem::exists(repository)) {
            for (const auto& entry : std::filesystem::directory_iterator(repository)) {
                if (!entry.is_directory()) continue;
                const std::wstring name = entry.path().filename().wstring();
                if (name.rfind(L"nvami.inf_amd64", 0) != 0) continue;
                candidates.push_back((entry.path() / L"_nvngx.dll").wstring());
                candidates.push_back((entry.path() / L"nvngx.dll").wstring());
            }
        }
    } catch (...) {
    }

    for (const std::wstring& path : candidates) {
        m_coreModule = LoadLibraryW(path.c_str());
        if (m_coreModule != nullptr) {
            std::wcout << L"[Yaoguang] NGX core loaded: " << path << std::endl;
            return true;
        }
    }
    return false;
}

bool DlssUpscaler::loadFeatureDll(const std::string& dllDir) {
    std::wstring path(dllDir.begin(), dllDir.end());
    path += L"\\nvngx_dlss.dll";
    m_featureModule = LoadLibraryW(path.c_str());
    if (m_featureModule == nullptr) return false;
    std::wcout << L"[Yaoguang] DLSS feature loaded: " << path << std::endl;
    return true;
}

bool DlssUpscaler::initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                              VkQueue queue, uint32_t queueFamily, const std::string& dllDir) {
    m_instance = instance;
    m_physicalDevice = physicalDevice;
    m_device = device;
    m_queue = queue;
    m_queueFamily = queueFamily;

    SetDllDirectoryW(std::wstring(dllDir.begin(), dllDir.end()).c_str());

    if (!loadCore(dllDir) || !loadFeatureDll(dllDir)) {
        m_status = "NGX core or DLSS feature DLL not found";
        return false;
    }

    m_initPath = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_Init"));
    m_init = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_Init_Ext2"));
    m_initProject = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_Init_with_ProjectID"));
    m_getParams = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_GetCapabilityParameters"));
    m_createFeature = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_CreateFeature1"));
    m_evaluateFeature = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_EvaluateFeature"));
    m_releaseFeature = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_ReleaseFeature"));
    m_shutdown = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_Shutdown1"));
    m_destroyParams = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_coreModule), "NVSDK_NGX_VULKAN_DestroyParameters"));

    if (!m_getParams || !m_createFeature || !m_evaluateFeature || !m_releaseFeature) {
        m_status = "NGX Vulkan entry points missing";
        return false;
    }

    std::wstring appData(dllDir.begin(), dllDir.end());
    wchar_t const* paths[1] = {appData.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;

    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    if (m_initProject != nullptr) {
        result = reinterpret_cast<InitProjectFn>(m_initProject)(
            PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", appData.c_str(),
            instance, physicalDevice, device,
            vkGetInstanceProcAddr, vkGetDeviceProcAddr,
            &info, NVSDK_NGX_Version_API);
        std::cout << "[Yaoguang] NGX project init result="
                  << static_cast<unsigned int>(result) << std::endl;
    }
    if (NVSDK_NGX_FAILED(result) && m_init != nullptr) {
        result = reinterpret_cast<InitFn>(m_init)(
            APP_ID, appData.c_str(), instance, physicalDevice, device,
            vkGetInstanceProcAddr, vkGetDeviceProcAddr,
            NVSDK_NGX_Version_API, nullptr);
        std::cout << "[Yaoguang] NGX ext init result="
                  << static_cast<unsigned int>(result) << std::endl;
    }
    if (NVSDK_NGX_FAILED(result) && m_initPath != nullptr) {
        result = reinterpret_cast<InitSimpleFn>(m_initPath)(
            APP_ID, appData.c_str(), instance, physicalDevice, device,
            NVSDK_NGX_Version_API);
        std::cout << "[Yaoguang] NGX plain init result="
                  << static_cast<unsigned int>(result) << std::endl;
    }
    if (NVSDK_NGX_FAILED(result)) {
        m_status = "NGX init failed " + std::to_string(static_cast<unsigned int>(result));
        SetDllDirectoryW(nullptr);
        return false;
    }
    m_initialized = true;

    result = reinterpret_cast<GetParamsFn>(m_getParams)(reinterpret_cast<NVSDK_NGX_Parameter**>(&m_params));
    if (NVSDK_NGX_FAILED(result) || m_params == nullptr) {
        m_params = nullptr;
        m_initialized = false;
        m_status = "NGX capability parameters unavailable";
        return false;
    }

    int available = 0;
    if (NVSDK_NGX_FAILED(reinterpret_cast<NVSDK_NGX_Parameter*>(m_params)->Get(
            NVSDK_NGX_Parameter_SuperSampling_Available, &available)) || available == 0) {
        m_params = nullptr;
        m_initialized = false;
        m_status = "DLSS unsupported on this GPU or driver";
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        m_status = "NGX command pool creation failed";
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        m_status = "NGX command buffer allocation failed";
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        m_status = "NGX fence creation failed";
        return false;
    }

    m_available = true;
    m_status = "DLSS ready";
    std::cout << "[Yaoguang] DLSS available" << std::endl;
    return true;
}

void DlssUpscaler::setQuality(int quality) {
    if (quality < 0 || quality > 5) quality = 0;
    if (quality == m_quality) return;
    m_quality = quality;
    if (m_feature != nullptr) {
        vkDeviceWaitIdle(m_device);
        closeFeature();
    }
}

bool DlssUpscaler::runCreateCommands() {
    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) return false;

    NVSDK_NGX_Parameter* params = reinterpret_cast<NVSDK_NGX_Parameter*>(m_params);
    params->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_Width, m_inputWidth);
    params->Set(NVSDK_NGX_Parameter_Height, m_inputHeight);
    params->Set(NVSDK_NGX_Parameter_OutWidth, m_outputWidth);
    params->Set(NVSDK_NGX_Parameter_OutHeight, m_outputHeight);
    float ratio = m_inputWidth > 0 ? static_cast<float>(m_outputWidth) / static_cast<float>(m_inputWidth) : 2.0f;
    NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxPerf;
    if (m_quality == 1) quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    else if (m_quality == 2) quality = NVSDK_NGX_PerfQuality_Value_Balanced;
    else if (m_quality == 3) quality = NVSDK_NGX_PerfQuality_Value_MaxPerf;
    else if (m_quality == 4) quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    else if (m_quality == 5) quality = NVSDK_NGX_PerfQuality_Value_DLAA;
    else if (ratio < 1.3f) quality = NVSDK_NGX_PerfQuality_Value_DLAA;
    else if (ratio < 1.6f) quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    else if (ratio < 1.9f) quality = NVSDK_NGX_PerfQuality_Value_Balanced;
    params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(quality));
    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_IsHDR));
    params->Set("DLSS.Use.HW.Depth", 0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);

    NVSDK_NGX_Result result = reinterpret_cast<CreateFn>(m_createFeature)(
        m_device, m_commandBuffer, NVSDK_NGX_Feature_SuperSampling,
        params, reinterpret_cast<NVSDK_NGX_Handle**>(&m_feature));

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    if (vkQueueSubmit(m_queue, 1, &submitInfo, m_fence) != VK_SUCCESS) return false;
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);

    if (NVSDK_NGX_FAILED(result) || m_feature == nullptr) {
        m_status = "DLSS feature create failed " + std::to_string(static_cast<unsigned int>(result));
        return false;
    }
    return true;
}

bool DlssUpscaler::resize(uint32_t inputWidth, uint32_t inputHeight,
                          uint32_t outputWidth, uint32_t outputHeight) {
    if (!m_available) return false;
    if (inputWidth == 0 || inputHeight == 0 || outputWidth == 0 || outputHeight == 0) return false;
    if (m_feature != nullptr && inputWidth == m_inputWidth && inputHeight == m_inputHeight &&
        outputWidth == m_outputWidth && outputHeight == m_outputHeight) {
        return true;
    }

    if (m_feature != nullptr) {
        vkDeviceWaitIdle(m_device);
    }
    closeFeature();
    m_inputWidth = inputWidth;
    m_inputHeight = inputHeight;
    m_outputWidth = outputWidth;
    m_outputHeight = outputHeight;
    return true;
}

bool DlssUpscaler::ensureFeature(uint32_t inputWidth, uint32_t inputHeight,
                                 uint32_t outputWidth, uint32_t outputHeight) {
    if (!m_available) return false;
    if (m_feature == nullptr || inputWidth != m_inputWidth || inputHeight != m_inputHeight ||
        outputWidth != m_outputWidth || outputHeight != m_outputHeight) {
        if (!resize(inputWidth, inputHeight, outputWidth, outputHeight)) return false;
        if (!runCreateCommands()) return false;
    }
    return true;
}

bool DlssUpscaler::evaluate(VkCommandBuffer commandBuffer,
                            VkImage color, VkImageView colorView,
                            VkImage depth, VkImageView depthView,
                            VkImage motion, VkImageView motionView,
                            VkImage output, VkImageView outputView,
                            float jitterX, float jitterY, bool reset) {
    if (!ensureFeature(m_inputWidth, m_inputHeight, m_outputWidth, m_outputHeight)) return false;

    NVSDK_NGX_Parameter* params = reinterpret_cast<NVSDK_NGX_Parameter*>(m_params);
    NVSDK_NGX_Resource_VK colorResource = makeResource(color, colorView,
        VK_FORMAT_R16G16B16A16_SFLOAT, m_inputWidth, m_inputHeight, false);
    NVSDK_NGX_Resource_VK depthResource = makeResource(depth, depthView,
        VK_FORMAT_R32_SFLOAT, m_inputWidth, m_inputHeight, false);
    NVSDK_NGX_Resource_VK motionResource = makeResource(motion, motionView,
        VK_FORMAT_R16G16_SFLOAT, m_inputWidth, m_inputHeight, false);
    NVSDK_NGX_Resource_VK outputResource = makeResource(output, outputView,
        VK_FORMAT_R16G16B16A16_SFLOAT, m_outputWidth, m_outputHeight, true);

    params->Set(NVSDK_NGX_Parameter_Color, static_cast<void*>(&colorResource));
    params->Set(NVSDK_NGX_Parameter_Output, static_cast<void*>(&outputResource));
    params->Set(NVSDK_NGX_Parameter_Depth, static_cast<void*>(&depthResource));
    params->Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<void*>(&motionResource));
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitterX);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, jitterY);
    params->Set(NVSDK_NGX_Parameter_Reset, reset ? 1 : 0);
    float mvScaleX = m_inputWidth > 0 ? 1.0f / static_cast<float>(m_inputWidth) : 1.0f;
    float mvScaleY = m_inputHeight > 0 ? 1.0f / static_cast<float>(m_inputHeight) : 1.0f;
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, mvScaleX);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, mvScaleY);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, m_inputWidth);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, m_inputHeight);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);

    NVSDK_NGX_Result result = reinterpret_cast<EvaluateFn>(m_evaluateFeature)(
        commandBuffer, reinterpret_cast<NVSDK_NGX_Handle*>(m_feature), params, nullptr);
    if (NVSDK_NGX_FAILED(result)) {
        m_status = "DLSS evaluate failed " + std::to_string(static_cast<unsigned int>(result));
        return false;
    }
    return true;
}

void DlssUpscaler::closeFeature() {
    if (m_feature != nullptr && m_releaseFeature != nullptr &&
        GetEnvironmentVariableW(L"DLSS_NO_RELEASE_FEATURE", nullptr, 0) == 0) {
        reinterpret_cast<ReleaseFn>(m_releaseFeature)(reinterpret_cast<NVSDK_NGX_Handle*>(m_feature));
    }
    m_feature = nullptr;
}

void DlssUpscaler::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    closeFeature();
    if (m_params != nullptr && m_destroyParams != nullptr && GetEnvironmentVariableW(L"DLSS_NO_DESTROY_PARAMS", nullptr, 0) == 0) {
        reinterpret_cast<DestroyParamsFn>(m_destroyParams)(reinterpret_cast<NVSDK_NGX_Parameter*>(m_params));
        m_params = nullptr;
    }
    if (m_initialized && m_shutdown != nullptr &&
        GetEnvironmentVariableW(L"DLSS_CALL_SHUTDOWN", nullptr, 0) != 0) {
        reinterpret_cast<ShutdownFn>(m_shutdown)(m_device);
        m_initialized = false;
    }
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_fence, nullptr);
        m_fence = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
    }
    m_featureModule = nullptr;
    m_coreModule = nullptr;
    m_available = false;
}
