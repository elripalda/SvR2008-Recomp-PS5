# WWE SmackDown vs. Raw 2008 - PS5 port (nefariousjosiah's WWE-SVR2008-RECOMP and its native
# renderer on the ReXGlue runtime's PS5 platform layer). Build:
#   cmake -S ps5 -B ps5/out/ps5 -G Ninja -DCMAKE_TOOLCHAIN_FILE=ps5/cmake/ps5-toolchain.cmake \
#         -DPS5_PAYLOAD_SDK=<ps5-payload-sdk> -DREXSDK_DIR=<rexglue-sdk, the PS5 runtime branch>
# Code: run 'rexglue codegen svr2008_manifest.toml' in the repository first (it reads
# assets/default.xex - your own disc's, never in this repository), then
# 'ps5/tools/ps5_patch_generated.py ps5/generated/rexglue.cmake'. The game's shaders:
# 'ps5/tools/extract_shader_cache.py <SVR2008-NATIVE.zip> generated/native/shader_cache.cpp'.
cmake_minimum_required(VERSION 3.25)

project(svr2008 VERSION 1.0.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# The PS5: no profiler, and none of SDL's desktop backends (the runtime's PS5 platform layer has
# the display, audio and the DualSense) - as SvR 2011 and 2007 build.
if(REX_PS5)
    set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
    foreach(opt SDL_DBUS SDL_HIDAPI SDL_IBUS SDL_JACK SDL_KMSDRM SDL_LIBUDEV SDL_OFFSCREEN SDL_OSS SDL_SNDIO)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()

include(generated/rexglue.cmake)

add_executable(svr2008
    src/main.cpp
    src/svr2008_app.cpp
    src/crash_report_posix.cpp
    # the PC port's game hooks (60 fps mode, frame counting, scripted input for tests)
    ${SVR_ROOT}/src/fps_hooks.cpp
    ${SVR_ROOT}/src/input_script.cpp)
# The hub: the screen the app opens on (THE HUB's Hub Kit, from the DBZ Budokai 3 port); its fonts
# are built into the app (fonts/, SIL Open Font License).
set(SVR2008_PS5_VERSION "0.0.8 Test")
target_sources(svr2008 PRIVATE src/hub/hub_ui.cpp src/hub/ps5_hub.cpp src/hub/hub_sfx.cpp src/hub/hub_install.cpp
                               src/ps5/sound.cpp)
set_source_files_properties(src/hub/ps5_hub.cpp PROPERTIES
    COMPILE_DEFINITIONS "SVR2008_FONTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fonts\";SVR2008_PS5_VERSION=\"${SVR2008_PS5_VERSION}\""
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/oswald-latin-700-normal.ttf")
target_include_directories(svr2008 PRIVATE src ${SVR_ROOT}/src)
rexglue_setup_target(svr2008 GPU_PLUGINS xenos)
target_compile_definitions(svr2008 PRIVATE SVR_VERSION="${PROJECT_VERSION}")
if(REXSDK_DIR AND TARGET imgui)
    # SDK source tree: ImGui (built into the runtime) is not on the public include path the way
    # the installed SDK bundles it.
    target_include_directories(svr2008 PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
                                               ${REXSDK_DIR}/thirdparty/stb)
endif()

# The native renderer (the PC port's cmake/native_renderer.cmake, for the PS5: Vulkan through
# plume's PS5 backend).
include(cmake/native_renderer_ps5.cmake)
svr_add_native_renderer(svr2008)
