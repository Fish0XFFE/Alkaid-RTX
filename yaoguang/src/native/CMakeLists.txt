cmake_minimum_required(VERSION 3.28)
project(yaoguang_rt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

find_package(Vulkan REQUIRED)

if(DEFINED ENV{JAVA_HOME})
    set(_YAOGUANG_DEFAULT_JAVA_HOME "$ENV{JAVA_HOME}")
else()
    set(_YAOGUANG_DEFAULT_JAVA_HOME "")
endif()
set(JAVA_HOME "${_YAOGUANG_DEFAULT_JAVA_HOME}" CACHE PATH "JDK path (set JAVA_HOME or pass -DJAVA_HOME=...)")

if(NOT EXISTS "${JAVA_HOME}/include/jni.h")
    message(FATAL_ERROR "jni.h not found at ${JAVA_HOME}/include/jni.h")
endif()

set(NRD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/nrd")

if(NOT EXISTS "${NRD_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "NRD not found at ${NRD_DIR}")
endif()

set(NRD_STATIC_LIBRARY        ON  CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS  ON  CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS   OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS   OFF CACHE BOOL "" FORCE)
set(NRD_SAMPLES               OFF CACHE BOOL "" FORCE)
set(NRD_TESTS                 OFF CACHE BOOL "" FORCE)
set(NRD_INTEGRATION           OFF CACHE BOOL "" FORCE)

set(DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/dxc")
set(DXC_EXE  "${DXC_ROOT}/bin/x64/dxc.exe" CACHE FILEPATH "" FORCE)

set(SHADERMAKE_FIND_DXC    OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_DXC_VK OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_FXC    OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_SLANG  OFF CACHE BOOL "" FORCE)

set(SHADERMAKE_DXC_PATH    "${DXC_EXE}" CACHE FILEPATH "" FORCE)
set(SHADERMAKE_DXC_VK_PATH "${DXC_EXE}" CACHE FILEPATH "" FORCE)
set(SHADERMAKE_FXC_PATH    ""           CACHE FILEPATH "" FORCE)

if(NOT EXISTS "${DXC_EXE}")
    message(FATAL_ERROR "dxc.exe not found at ${DXC_EXE}")
endif()
message(STATUS "[Yaoguang] Using local DXC: ${DXC_EXE}")

set(FETCHCONTENT_SOURCE_DIR_SHADERMAKE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/shadermake" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_MATHLIB
    "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/mathlib"    CACHE PATH "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/shadermake/CMakeLists.txt")
    message(FATAL_ERROR "shadermake source missing")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/mathlib/CMakeLists.txt")
    message(FATAL_ERROR "mathlib source missing")
endif()

add_subdirectory("${NRD_DIR}" "${CMAKE_BINARY_DIR}/nrd" EXCLUDE_FROM_ALL)

set(OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../main/resources/native/windows/x86_64")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT "${CMAKE_CURRENT_SOURCE_DIR}/../main/resources/native/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT}")

add_library(yaoguang_rt SHARED
    src/yaoguang_rt.cpp
    src/VulkanContext.cpp
    src/VulkanOpenGLInterop.cpp
    src/SceneAccel.cpp
    src/RayTracingPipeline.cpp
    src/NrdDenoiser.cpp
    src/DlssUpscaler.cpp
)

target_include_directories(yaoguang_rt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Vulkan_INCLUDE_DIRS}
    "${JAVA_HOME}/include"
    "${JAVA_HOME}/include/win32"
    "${NRD_DIR}/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/dlss/include"
)

target_link_libraries(yaoguang_rt PRIVATE
    Vulkan::Vulkan
    opengl32
    gdi32
    user32
    ${CMAKE_DL_LIBS}
    NRD
)

target_compile_definitions(yaoguang_rt PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    YAOGUANG_VERSION="1.0.0"
    VK_USE_PLATFORM_WIN32_KHR=1
    NRD_EMBEDS_SPIRV_SHADERS=1
)

if(MSVC)
    target_compile_options(yaoguang_rt PRIVATE /W3 /wd4996 /wd4267 /O2 /MD /utf-8)
    set_target_properties(yaoguang_rt PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )
endif()

if(WIN32)
    set(SUFFIX_VALUE ".dll")
elseif(APPLE)
    set(SUFFIX_VALUE ".dylib")
else()
    set(SUFFIX_VALUE ".so")
endif()

set_target_properties(yaoguang_rt PROPERTIES
    SUFFIX "${SUFFIX_VALUE}"
    RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${OUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUT_DIR}"
)

add_executable(dlss_diag EXCLUDE_FROM_ALL test/dlss_diag.cpp src/VulkanContext.cpp)
target_include_directories(dlss_diag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/dlss/include"
)
target_compile_definitions(dlss_diag PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VK_USE_PLATFORM_WIN32_KHR=1)
target_link_libraries(dlss_diag PRIVATE Vulkan::Vulkan)
if(MSVC)
    target_compile_options(dlss_diag PRIVATE /W3 /wd4996 /O2 /MD /utf-8)
endif()
set_target_properties(dlss_diag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
)

add_executable(dlss_pipeline_test EXCLUDE_FROM_ALL test/dlss_pipeline_test.cpp src/VulkanContext.cpp src/DlssUpscaler.cpp)
target_include_directories(dlss_pipeline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../externals/dlss/include"
)
target_compile_definitions(dlss_pipeline_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VK_USE_PLATFORM_WIN32_KHR=1)
target_link_libraries(dlss_pipeline_test PRIVATE Vulkan::Vulkan)
if(MSVC)
    target_compile_options(dlss_pipeline_test PRIVATE /W3 /wd4996 /O2 /MD /utf-8)
endif()
set_target_properties(dlss_pipeline_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
)

add_executable(nrd_info EXCLUDE_FROM_ALL test/nrd_info.cpp)
target_include_directories(nrd_info PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    "${NRD_DIR}/Include"
)
target_compile_definitions(nrd_info PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(nrd_info PRIVATE Vulkan::Vulkan NRD)
if(MSVC)
    target_compile_options(nrd_info PRIVATE /W3 /wd4996 /O2 /MD /utf-8)
    set_target_properties(nrd_info PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
set_target_properties(nrd_info PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
)

add_executable(nrd_run_test EXCLUDE_FROM_ALL test/nrd_run_test.cpp src/VulkanContext.cpp src/NrdDenoiser.cpp)
target_include_directories(nrd_run_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Vulkan_INCLUDE_DIRS}
    "${NRD_DIR}/Include"
)
target_compile_definitions(nrd_run_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VK_USE_PLATFORM_WIN32_KHR=1)
target_link_libraries(nrd_run_test PRIVATE Vulkan::Vulkan NRD)
if(MSVC)
    target_compile_options(nrd_run_test PRIVATE /W3 /wd4996 /O2 /MD /utf-8)
    set_target_properties(nrd_run_test PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
set_target_properties(nrd_run_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../build-native"
)

find_program(GLSLC_EXECUTABLE glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})
if(NOT GLSLC_EXECUTABLE)
    find_program(GLSLC_EXECUTABLE glslc PATHS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
endif()

if(GLSLC_EXECUTABLE)
    set(SHADER_FILES
        ${SHADER_DIR}/ray_gen.rgen
        ${SHADER_DIR}/closest_hit.rchit
        ${SHADER_DIR}/alpha_test.rahit
        ${SHADER_DIR}/miss.rmiss
        ${SHADER_DIR}/combine.comp
        ${SHADER_DIR}/composite.comp
        ${SHADER_DIR}/cloud.comp
    )

    set(SPV_OUTPUTS "")

    foreach(SHADER ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_FILE ${SHADER_OUT}/${SHADER_NAME}.spv)

        add_custom_command(
            OUTPUT "${SPV_FILE}"
            COMMAND "${GLSLC_EXECUTABLE}"
            ARGS --target-env=vulkan1.3 -o "${SPV_FILE}" "${SHADER}"
            DEPENDS "${SHADER}"
            COMMENT "Compiling shader ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND SPV_OUTPUTS "${SPV_FILE}")
    endforeach()

    add_custom_target(yaoguang_shaders ALL DEPENDS ${SPV_OUTPUTS})
    add_dependencies(yaoguang_rt yaoguang_shaders)
endif()