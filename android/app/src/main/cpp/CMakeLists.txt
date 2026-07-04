cmake_minimum_required(VERSION 3.22.1)
project(vlither C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)

file(GLOB_RECURSE THERMITE_SRC
    "${ROOT}/thermite/src/*.c"
    "${ROOT}/thermite/src/*.cpp"
)

file(GLOB_RECURSE APP_SRC
    "${ROOT}/app/src/*.c"
    "${ROOT}/app/src/*.cpp"
)

# Exclude PC-only files — main.c is kept because it defines tlaunch/tinit/tinput/trender/tresize
# The TDEF_ENTRY() macro at the bottom of main.c is guarded with #ifndef ANDROID
list(REMOVE_ITEM APP_SRC
    "${ROOT}/app/src/cimgui/imgui/imgui_impl_glfw.cpp"
    "${ROOT}/app/src/imgui_setup.c"
)

set(VMA_SRC "${ROOT}/thermite/src/external/vma/vk_mem_alloc.cpp")

add_library(android_native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(android_native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(vlither SHARED
    ${THERMITE_SRC}
    ${APP_SRC}
    ${VMA_SRC}
)

target_include_directories(vlither PRIVATE
    ${ROOT}/thermite/include
    ${ROOT}/thermite/src
    ${ROOT}/app/src
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_compile_definitions(vlither PRIVATE
    ANDROID
    MG_TLS=3
    CGLM_FORCE_DEPTH_ZERO_TO_ONE
    CIMGUI_NO_EXPORT
    APP_VERSION="3.7"
    SETTINGS_VERSION="1.8"
    VK_USE_PLATFORM_ANDROID_KHR
)

find_library(log-lib     log)
find_library(android-lib android)
find_library(vulkan-lib  vulkan)

target_link_libraries(vlither
    android_native_app_glue
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
    m
)

# Force android_native_app_glue symbols to be included (prevents stripping)
target_link_options(vlither PRIVATE
    -Wl,--whole-archive
    $<TARGET_FILE:android_native_app_glue>
    -Wl,--no-whole-archive
    -Wl,-z,max-page-size=16384
)
