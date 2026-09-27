#include "yaoguang_rt.h"
#include "VulkanContext.h"
#include "VulkanOpenGLInterop.h"
#include "RayTracingPipeline.h"
#include "SceneAccel.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <iostream>
#include <cstring>
#include <cmath>

namespace {

std::unique_ptr<VulkanContext> g_vulkan;
std::unique_ptr<VulkanOpenGLInterop> g_interop;
std::unique_ptr<RayTracingPipeline> g_pipeline;
std::unique_ptr<SceneAccel> g_scene;

std::atomic<bool> g_rtEnabled{false};
std::atomic<int>  g_maxBounces{4};
std::atomic<int>  g_spp{2};
std::atomic<float> g_rayLength{64.0f};
std::atomic<bool> g_denoiser{true};
std::atomic<float> g_exposure{1.2f};
std::atomic<bool> g_pathTracing{true};
std::atomic<bool> g_vramExt{false};
std::atomic<int>  g_vramSize{0};

std::atomic<float> g_sunDirX{0.4879f};
std::atomic<float> g_sunDirY{0.7807f};
std::atomic<float> g_sunDirZ{0.3903f};
std::atomic<float> g_sunIntensity{1.6f};

std::atomic<float> g_rainLevel{0.0f};
std::atomic<float> g_thunderLevel{0.0f};

std::atomic<bool> g_initialized{false};
std::atomic<bool> g_supported{false};

std::string g_assetDir = ".";

jstring toJString(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

std::string deriveAssetDir() {
    char dllPath[MAX_PATH];
    HMODULE hMod = GetModuleHandleA("yaoguang_rt.dll");
    if (hMod == nullptr) {
        hMod = GetModuleHandleA(nullptr);
    }
    DWORD len = GetModuleFileNameA(hMod, dllPath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return ".";

    std::string p(dllPath);
    size_t pos = p.find_last_of("\\/");
    if (pos == std::string::npos) return ".";
    return p.substr(0, pos);
}

} // namespace

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setAssetDir(JNIEnv* env, jclass, jstring path) {
    if (!path) return;
    const char* utf = env->GetStringUTFChars(path, nullptr);
    if (utf) {
        g_assetDir = utf;
        env->ReleaseStringUTFChars(path, utf);
    }
    std::cout << "[Yaoguang] Asset dir set to: " << g_assetDir << std::endl;
}

JNIEXPORT jboolean JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_init(JNIEnv*, jclass) {
    std::cout << "[DBG] init A" << std::endl;
    if (g_initialized.load()) return g_supported.load() ? JNI_TRUE : JNI_FALSE;

    try {
        std::cout << "[DBG] init B - make VulkanContext" << std::endl;
        g_vulkan = std::make_unique<VulkanContext>();
        std::cout << "[DBG] init C - call initialize()" << std::endl;
        if (!g_vulkan->initialize()) {
            std::cerr << "[Yaoguang] VulkanContext init failed" << std::endl;
            g_supported.store(false);
            g_initialized.store(true);
            return JNI_FALSE;
        }
        std::cout << "[DBG] init D - initialize returned" << std::endl;

        g_supported.store(g_vulkan->supportsRayTracing());
        if (!g_supported.load()) {
            std::cerr << "[Yaoguang] RT not supported" << std::endl;
            g_initialized.store(true);
            return JNI_FALSE;
        }

        std::cout << "[DBG] init E - make SceneAccel" << std::endl;
        g_scene = std::make_unique<SceneAccel>();
        std::cout << "[DBG] init F - SceneAccel initialize" << std::endl;
        g_scene->initialize(*g_vulkan);
        std::cout << "[DBG] init G - make Interop" << std::endl;

        g_interop = std::make_unique<VulkanOpenGLInterop>();
        std::cout << "[DBG] init H - Interop initialize" << std::endl;
        if (!g_interop->initialize(g_vulkan->getDevice(),
                                    g_vulkan->getPhysicalDevice(),
                                    g_vulkan->getQueue(),
                                    g_vulkan->getQueueFamilyIndex(),
                                    1280, 720)) {
            std::cerr << "[Yaoguang] Interop init failed" << std::endl;
            g_supported.store(false);
            g_initialized.store(true);
            return JNI_FALSE;
        }
        std::cout << "[DBG] init I - derive asset dir" << std::endl;

        std::string derived = deriveAssetDir();
        std::cout << "[Yaoguang] Derived asset dir: " << derived << std::endl;
        if (g_assetDir == "." || g_assetDir.empty()) {
            g_assetDir = derived;
        }

        std::cout << "[DBG] init J - make Pipeline" << std::endl;
        g_pipeline = std::make_unique<RayTracingPipeline>();
        g_pipeline->setAssetDir(g_assetDir);
        std::cout << "[DBG] init K - Pipeline initialize" << std::endl;
        g_pipeline->initialize(*g_vulkan, *g_scene, *g_interop);
        std::cout << "[DBG] init L - updateParams" << std::endl;

        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(),
                                  g_denoiser.load(), g_exposure.load(),
                                  g_pathTracing.load(), g_rayLength.load());

        std::cout << "[DBG] init M - done" << std::endl;
        g_initialized.store(true);
        return g_supported.load() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        std::cerr << "[Yaoguang] Init exception: " << e.what() << std::endl;
        g_supported.store(false);
        g_initialized.store(true);
        return JNI_FALSE;
    } catch (...) {
        std::cerr << "[Yaoguang] Init unknown exception" << std::endl;
        g_supported.store(false);
        g_initialized.store(true);
        return JNI_FALSE;
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_shutdown(JNIEnv*, jclass) {
    if (!g_initialized.load()) return;

    if (g_vulkan) {
        vkDeviceWaitIdle(g_vulkan->getDevice());
    }

    g_pipeline.reset();
    g_interop.reset();
    g_scene.reset();
    if (g_vulkan) g_vulkan->shutdown();
    g_vulkan.reset();

    g_initialized.store(false);
    g_supported.store(false);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setRayTracingEnabled(JNIEnv*, jclass, jboolean enabled) {
    g_rtEnabled.store(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_applyPreset(JNIEnv*, jclass, jint preset) {
    switch (preset) {
        case 0: g_maxBounces.store(1); g_spp.store(1); g_denoiser.store(true); g_exposure.store(0.8f); g_pathTracing.store(false); break;
        case 1: g_maxBounces.store(2); g_spp.store(2); g_denoiser.store(true); g_exposure.store(1.0f); g_pathTracing.store(true);  break;
        case 2: g_maxBounces.store(4); g_spp.store(2); g_denoiser.store(true); g_exposure.store(1.2f); g_pathTracing.store(true);  break;
        case 3: g_maxBounces.store(6); g_spp.store(4); g_denoiser.store(false); g_exposure.store(1.3f); g_pathTracing.store(true); break;
        case 4: g_maxBounces.store(8); g_spp.store(8); g_denoiser.store(false); g_exposure.store(1.5f); g_pathTracing.store(true); break;
        default: break;
    }
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setMaxBounces(JNIEnv*, jclass, jint v) {
    g_maxBounces.store(v);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setSamplesPerPixel(JNIEnv*, jclass, jint v) {
    g_spp.store(v);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setRayLength(JNIEnv*, jclass, jfloat v) {
    g_rayLength.store(v);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setDenoiserEnabled(JNIEnv*, jclass, jboolean v) {
    g_denoiser.store(v == JNI_TRUE);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setExposure(JNIEnv*, jclass, jfloat v) {
    g_exposure.store(v);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setPathTracingEnabled(JNIEnv*, jclass, jboolean v) {
    g_pathTracing.store(v == JNI_TRUE);
    if (g_pipeline) {
        g_pipeline->updateParams(g_maxBounces.load(), g_spp.load(), g_denoiser.load(),
                                  g_exposure.load(), g_pathTracing.load(), g_rayLength.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setVramExtension(JNIEnv*, jclass, jboolean enabled, jint gigabytes) {
    g_vramExt.store(enabled == JNI_TRUE);
    g_vramSize.store(gigabytes);
    if (g_vulkan) {
        g_vulkan->reserveVirtualVram(g_vramExt.load(),
                                      static_cast<uint32_t>(g_vramSize.load()));
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_onFrameBegin(JNIEnv*, jclass, jlong windowHandle, jint width, jint height) {
    (void)windowHandle;
    if (!g_rtEnabled.load() || !g_initialized.load() || !g_supported.load()) return;
    if (g_interop) g_interop->resize((uint32_t)width, (uint32_t)height);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_onFrameEnd(JNIEnv*, jclass) {
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_rebuildSceneAccel(JNIEnv*, jclass,
                                                                      jlong vertexPtr, jlong indexPtr,
                                                                      jint vertexCount, jint indexCount) {
    if (!g_scene) return;
    g_scene->rebuild(reinterpret_cast<const float*>(vertexPtr),
                     reinterpret_cast<const uint32_t*>(indexPtr),
                     (uint32_t)vertexCount,
                     (uint32_t)indexCount);
}

JNIEXPORT jboolean JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_isRayTracingSupported(JNIEnv*, jclass) {
    return g_supported.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getRayTracingUnit(JNIEnv*, jclass) {
    if (!g_vulkan) return 0;
    return static_cast<jint>(g_vulkan->getRayTracingUnit());
}

JNIEXPORT jstring JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getDeviceName(JNIEnv* env, jclass) {
    if (!g_vulkan) return toJString(env, "Unknown");
    return toJString(env, g_vulkan->getDeviceName());
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setCamera(
    JNIEnv*, jclass,
    jfloat px, jfloat py, jfloat pz,
    jfloat dx, jfloat dy, jfloat dz,
    jfloat ux, jfloat uy, jfloat uz,
    jfloat fov, jfloat aspect) {
    if (!g_pipeline) return;
    g_pipeline->updateCameraRaw(px, py, pz, dx, dy, dz, ux, uy, uz, fov, aspect);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_renderFrame(JNIEnv*, jclass) {
    if (!g_rtEnabled.load()) return;
    if (!g_pipeline || !g_pipeline->isInitialized()) return;
    g_pipeline->renderFrame();
}

JNIEXPORT jint JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputTexture(JNIEnv*, jclass) {
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputWidth(JNIEnv*, jclass) {
    if (!g_interop) return 0;
    return static_cast<jint>(g_interop->getWidth());
}

JNIEXPORT jint JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputHeight(JNIEnv*, jclass) {
    if (!g_interop) return 0;
    return static_cast<jint>(g_interop->getHeight());
}

JNIEXPORT jlong JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputNativePtr(JNIEnv*, jclass) {
    return 0L;
}

JNIEXPORT jlong JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getSharedHandle(JNIEnv*, jclass) {
    if (!g_interop) return 0L;
    if (!g_interop->isValid()) return 0L;
    return reinterpret_cast<jlong>(g_interop->getSharedImageHandle());
}

JNIEXPORT jlong JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getVkSignalSemaphoreHandle(JNIEnv*, jclass) {
    if (!g_interop) return 0L;
    if (!g_interop->isValid()) return 0L;
    return reinterpret_cast<jlong>(g_interop->getVkSignalSemaphoreHandle());
}

JNIEXPORT jlong JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_getGlSignalSemaphoreHandle(JNIEnv*, jclass) {
    if (!g_interop) return 0L;
    if (!g_interop->isValid()) return 0L;
    return reinterpret_cast<jlong>(g_interop->getGlSignalSemaphoreHandle());
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadSceneGeometry(
    JNIEnv* env, jclass, jfloatArray vertices, jintArray indices) {
    if (!g_scene) return;

    jsize vLen = env->GetArrayLength(vertices);
    jsize iLen = env->GetArrayLength(indices);
    if (vLen == 0 || iLen == 0) return;
    if (vLen % 12 != 0) {
        std::cerr << "[Yaoguang] uploadSceneGeometry: vLen not multiple of 12 ("
                  << vLen << ")" << std::endl;
        return;
    }

    jfloat* vData = env->GetFloatArrayElements(vertices, nullptr);
    jint* iData = env->GetIntArrayElements(indices, nullptr);

    if (vData && iData) {
        g_scene->rebuild(reinterpret_cast<const float*>(vData),
                         reinterpret_cast<const uint32_t*>(iData),
                         (uint32_t)(vLen / 12),
                         (uint32_t)iLen);
    }

    if (vData) env->ReleaseFloatArrayElements(vertices, vData, JNI_ABORT);
    if (iData) env->ReleaseIntArrayElements(indices, iData, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setSun(
    JNIEnv*, jclass, jfloat dx, jfloat dy, jfloat dz, jfloat intensity) {
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) len = 1.0f;
    g_sunDirX.store(dx / len);
    g_sunDirY.store(dy / len);
    g_sunDirZ.store(dz / len);
    g_sunIntensity.store(intensity);
    if (g_pipeline) {
        g_pipeline->setSun(g_sunDirX.load(), g_sunDirY.load(),
                            g_sunDirZ.load(), g_sunIntensity.load());
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_setWeather(
    JNIEnv*, jclass, jfloat rain, jfloat thunder) {
    g_rainLevel.store(rain);
    g_thunderLevel.store(thunder);
    if (g_pipeline) {
        g_pipeline->setWeather(rain, thunder);
    }
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadBlockAtlas(
    JNIEnv* env, jclass, jint width, jint height, jobject buffer) {
    if (!g_pipeline) return;
    if (width <= 0 || height <= 0) return;
    void* ptr = env->GetDirectBufferAddress(buffer);
    if (!ptr) return;
    g_pipeline->uploadAtlas(ptr, (uint32_t)width, (uint32_t)height);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadDynamic(
    JNIEnv* env, jclass, jobject buffer, jint count) {
    if (!g_pipeline) return;
    if (count <= 0) { g_pipeline->clearDynamic(); return; }
    void* ptr = env->GetDirectBufferAddress(buffer);
    if (!ptr) return;
    g_pipeline->uploadDynamic(ptr, (uint32_t)count);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_clearDynamic(JNIEnv*, jclass) {
    if (!g_pipeline) return;
    g_pipeline->clearDynamic();
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadParticleAtlas(
    JNIEnv* env, jclass, jint width, jint height, jobject buffer) {
    if (!g_pipeline) return;
    if (width <= 0 || height <= 0) return;
    void* ptr = env->GetDirectBufferAddress(buffer);
    if (!ptr) return;
    g_pipeline->uploadParticleAtlas(ptr, (uint32_t)width, (uint32_t)height);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadParticles(
    JNIEnv* env, jclass, jobject buffer, jint count) {
    if (!g_pipeline) return;
    if (count <= 0) { g_pipeline->clearParticles(); return; }
    void* ptr = env->GetDirectBufferAddress(buffer);
    if (!ptr) return;
    g_pipeline->uploadParticles(ptr, (uint32_t)count);
}

JNIEXPORT void JNICALL
Java_com_fish_yaoguang_nativebridge_YaoguangNative_clearParticles(JNIEnv*, jclass) {
    if (!g_pipeline) return;
    g_pipeline->clearParticles();
}