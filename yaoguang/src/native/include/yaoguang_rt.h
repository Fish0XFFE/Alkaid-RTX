#pragma once

#include <jni.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setAssetDir(JNIEnv*, jclass, jstring);
JNIEXPORT jboolean JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_init(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_shutdown(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setRayTracingEnabled(JNIEnv*, jclass, jboolean);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_applyPreset(JNIEnv*, jclass, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setMaxBounces(JNIEnv*, jclass, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setSamplesPerPixel(JNIEnv*, jclass, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setRayLength(JNIEnv*, jclass, jfloat);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setDenoiserEnabled(JNIEnv*, jclass, jboolean);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setExposure(JNIEnv*, jclass, jfloat);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setPathTracingEnabled(JNIEnv*, jclass, jboolean);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setVramExtension(JNIEnv*, jclass, jboolean, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_onFrameBegin(JNIEnv*, jclass, jlong, jint, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_onFrameEnd(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_rebuildSceneAccel(JNIEnv*, jclass, jlong, jlong, jint, jint);
JNIEXPORT jboolean JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_isRayTracingSupported(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getRayTracingUnit(JNIEnv*, jclass);
JNIEXPORT jstring JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getDeviceName(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setCamera(JNIEnv*, jclass, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_renderFrame(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputTexture(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputWidth(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputHeight(JNIEnv*, jclass);
JNIEXPORT jlong JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getOutputNativePtr(JNIEnv*, jclass);
JNIEXPORT jlong JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getSharedHandle(JNIEnv*, jclass);
JNIEXPORT jlong JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getVkSignalSemaphoreHandle(JNIEnv*, jclass);
JNIEXPORT jlong JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_getGlSignalSemaphoreHandle(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadSceneGeometry(JNIEnv*, jclass, jfloatArray, jintArray);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setSun(JNIEnv*, jclass, jfloat, jfloat, jfloat, jfloat);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_setWeather(JNIEnv*, jclass, jfloat, jfloat);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadBlockAtlas(JNIEnv*, jclass, jint, jint, jobject);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadDynamic(JNIEnv*, jclass, jobject, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_clearDynamic(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadParticleAtlas(JNIEnv*, jclass, jint, jint, jobject);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_uploadParticles(JNIEnv*, jclass, jobject, jint);
JNIEXPORT void JNICALL Java_com_fish_yaoguang_nativebridge_YaoguangNative_clearParticles(JNIEnv*, jclass);

#ifdef __cplusplus
}
#endif