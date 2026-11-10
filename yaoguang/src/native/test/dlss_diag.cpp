#include "VulkanContext.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_vk.h>

namespace {

using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using InitProjectFn = NVSDK_NGX_Result(NVSDK_CONV*)(const char*, NVSDK_NGX_EngineType, const char*,
    const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr,
    PFN_vkGetDeviceProcAddr, const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version);
using GetParamsFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(VkDevice);

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void dumpExports(const wchar_t* path) {
    HMODULE module = LoadLibraryW(path);
    if (module == nullptr) {
        std::cout << "  load failed " << GetLastError() << std::endl;
        return;
    }
    auto bytes = reinterpret_cast<const unsigned char*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes + dos->e_lfanew);
    auto dir = reinterpret_cast<const IMAGE_DATA_DIRECTORY*>(
        nt->OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_EXPORT);
    if (dir->VirtualAddress == 0) {
        std::cout << "  no export directory" << std::endl;
        return;
    }
    auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes + dir->VirtualAddress);
    auto names = reinterpret_cast<const DWORD*>(bytes + exp->AddressOfNames);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(bytes + names[i]);
        if (std::string(name).rfind("NVSDK_NGX_VULKAN", 0) == 0) {
            std::cout << "  " << name << std::endl;
        }
    }
    FreeLibrary(module);
}

}

int main(int argc, char** argv) {
    std::string dllDir = argc > 1 ? argv[1] : "C:/Users/fish/Desktop/yaoguang/src/main/resources/native/windows/x86_64";
    std::wstring wdir(dllDir.begin(), dllDir.end());
    std::wstring featurePath = wdir + L"\\nvngx_dlss.dll";
    std::wstring coreLocal = wdir + L"\\_nvngx.dll";
    std::wstring coreLocalAlt = wdir + L"\\nvngx.dll";

    std::cout << "dir=" << dllDir << std::endl;

    VulkanContext context;
    if (!context.initialize()) {
        std::cout << "vulkan-init-failed" << std::endl;
        return 2;
    }

    std::vector<std::wstring> cores;
    cores.push_back(coreLocal);
    cores.push_back(coreLocalAlt);
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0) {
        cores.push_back(std::wstring(systemDir) + L"\\_nvngx.dll");
    }
    cores.push_back(L"C:\\Windows\\System32\\DriverStore\\FileRepository\\nvami.inf_amd64_00e5dc1fb00a1261\\_nvngx.dll");

    for (const std::wstring& core : cores) {
        std::cout << "--- core: " << narrow(core) << std::endl;
        if (GetFileAttributesW(core.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cout << "  missing" << std::endl;
            continue;
        }
        std::cout << "  exports:" << std::endl;
        dumpExports(core.c_str());
    }

    HMODULE coreModule = nullptr;
    for (const std::wstring& core : cores) {
        coreModule = LoadLibraryW(core.c_str());
        if (coreModule != nullptr) {
            std::cout << "core loaded: " << narrow(core) << std::endl;
            break;
        }
    }
    if (coreModule == nullptr) {
        std::cout << "no-core" << std::endl;
        return 3;
    }

    SetDllDirectoryW(wdir.c_str());
    HMODULE featureModule = nullptr;
    const bool preload = GetEnvironmentVariableW(L"DLSS_DIAG_NO_PRELOAD", nullptr, 0) == 0;
    if (preload) {
        featureModule = LoadLibraryW(featurePath.c_str());
    }
    std::cout << "feature preload=" << preload << " loaded=" << (featureModule != nullptr)
              << " err=" << GetLastError() << std::endl;

    auto initProject = reinterpret_cast<InitProjectFn>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_Init_with_ProjectID"));
    auto initExt = reinterpret_cast<InitFn>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_Init_Ext2"));
    auto initPlain = reinterpret_cast<InitFn>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_Init"));
    auto getParams = reinterpret_cast<GetParamsFn>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_GetCapabilityParameters"));
    auto shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_Shutdown1"));
    std::cout << "ptr project=" << (void*)initProject << " ext=" << (void*)initExt
              << " plain=" << (void*)initPlain << " params=" << (void*)getParams << std::endl;

    const char* projectId = "8f2c1a9e-4b3d-4f6a-8c21-7d5e9b0a3c64";
    wchar_t const* paths[1] = {wdir.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;

    auto getRequirements = reinterpret_cast<NVSDK_NGX_Result(NVSDK_CONV*)(VkInstance, VkPhysicalDevice,
        const NVSDK_NGX_FeatureDiscoveryInfo*, NVSDK_NGX_FeatureRequirement*)>(
        GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_GetFeatureRequirements"));
    if (getRequirements != nullptr) {
        NVSDK_NGX_FeatureDiscoveryInfo discovery{};
        discovery.SDKVersion = NVSDK_NGX_Version_API;
        discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
        discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
        discovery.Identifier.v.ApplicationId = 231313132ull;
        discovery.ApplicationDataPath = wdir.c_str();
        discovery.FeatureInfo = &info;
        NVSDK_NGX_FeatureRequirement requirement{};
        NVSDK_NGX_Result rr = getRequirements(context.getInstance(), context.getPhysicalDevice(),
                                               &discovery, &requirement);
        std::cout << "requirements result=" << static_cast<unsigned int>(rr)
                  << " supported=" << static_cast<int>(requirement.FeatureSupported)
                  << " minArch=" << requirement.MinHWArchitecture
                  << " minOs=" << requirement.MinOSVersion << std::endl;

        auto getDevExt = reinterpret_cast<NVSDK_NGX_Result(NVSDK_CONV*)(VkInstance, VkPhysicalDevice,
            const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t*, VkExtensionProperties**)>(GetProcAddress(coreModule, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements"));
        if (getDevExt != nullptr) {
            uint32_t count = 0;
            VkExtensionProperties* exts = nullptr;
            NVSDK_NGX_Result er = getDevExt(context.getInstance(), context.getPhysicalDevice(), &discovery, &count, &exts);
            std::cout << "device ext req rc=" << static_cast<unsigned int>(er) << " count=" << count << std::endl;
            for (uint32_t i = 0; i < count; ++i) {
                std::cout << "  need device ext: " << exts[i].extensionName << std::endl;
            }
        }
    }

    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    const char* used = "none";
    if (initProject != nullptr) {
        result = initProject(projectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", wdir.c_str(),
                             context.getInstance(), context.getPhysicalDevice(), context.getDevice(),
                             vkGetInstanceProcAddr, vkGetDeviceProcAddr, &info, NVSDK_NGX_Version_API);
        used = "project";
        std::cout << "project init=" << static_cast<unsigned int>(result) << std::endl;
    }
    if (NVSDK_NGX_FAILED(result) && initExt != nullptr) {
        NVSDK_NGX_Result r = initExt(231313132ull, wdir.c_str(), context.getInstance(),
                                     context.getPhysicalDevice(), context.getDevice(),
                                     vkGetInstanceProcAddr, vkGetDeviceProcAddr,
                                     NVSDK_NGX_Version_API, nullptr);
        std::cout << "ext init=" << static_cast<unsigned int>(r) << std::endl;
        if (NVSDK_NGX_SUCCEED(r)) { result = r; used = "ext"; }
    }
    if (NVSDK_NGX_FAILED(result) && initPlain != nullptr) {
        NVSDK_NGX_Result r = initPlain(231313132ull, wdir.c_str(), context.getInstance(),
                                       context.getPhysicalDevice(), context.getDevice(),
                                       vkGetInstanceProcAddr, vkGetDeviceProcAddr,
                                       NVSDK_NGX_Version_API, nullptr);
        std::cout << "plain init=" << static_cast<unsigned int>(r) << std::endl;
        if (NVSDK_NGX_SUCCEED(r)) { result = r; used = "plain"; }
    }
    std::cout << "init used=" << used << " result=" << static_cast<unsigned int>(result) << std::endl;

    if (NVSDK_NGX_SUCCEED(result) && getParams != nullptr) {
        NVSDK_NGX_Parameter* params = nullptr;
        NVSDK_NGX_Result pr = getParams(&params);
        std::cout << "getparams result=" << static_cast<unsigned int>(pr) << " ptr=" << (void*)params << std::endl;
        if (NVSDK_NGX_SUCCEED(pr) && params != nullptr) {
            int available = -1;
            NVSDK_NGX_Result ar = params->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
            std::cout << "available result=" << static_cast<unsigned int>(ar) << " value=" << available << std::endl;
            int initResult = -1;
            NVSDK_NGX_Result ir = params->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
            std::cout << "featureinitresult rc=" << static_cast<unsigned int>(ir) << " value=" << initResult
                      << " hex=0x" << std::hex << static_cast<unsigned int>(initResult) << std::dec << std::endl;
            int featureInit = -1;
            if (NVSDK_NGX_SUCCEED(params->Get(NVSDK_NGX_Parameter_NumFrames, &featureInit))) {
                std::cout << "numframes=" << featureInit << std::endl;
            }
        }
    }

    if (shutdown != nullptr) shutdown(context.getDevice());
    context.shutdown();
    std::cout << "done" << std::endl;
    return 0;
}
