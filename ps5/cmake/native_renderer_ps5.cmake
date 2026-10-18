# [ps5 port] The PC port's native renderer (cmake/native_renderer.cmake, adapted from re:Blue,
# BSD-3-Clause; see src/reblue/LICENSE.reblue) for the PS5: Vulkan only, through plume's PS5
# backend (third_party/plume: zolaware/plume + patches/plume.patch + ps5/patches/plume-ps5.patch).
# The game's shaders come from generated/native/shader_cache.cpp (ps5/tools/extract_shader_cache.py).

set(SVR_NATIVE_ROOT "${SVR_ROOT}")
set(SVR_XENOS_DIR "${SVR_NATIVE_ROOT}/third_party/XenosRecomp")
set(SVR_NATIVE_GEN "${CMAKE_BINARY_DIR}/native_gen")

set(PLUME_D3D12_ENABLED OFF CACHE BOOL "" FORCE)
add_subdirectory("${SVR_NATIVE_ROOT}/third_party/plume" "${CMAKE_BINARY_DIR}/plume" EXCLUDE_FROM_ALL)
if(REX_PS5)
    # plume uses the runtime's VMA (linked once, with its implementation) rather than its own copy:
    # both end up in the one static program (as the SvR 2011 port does).
    target_include_directories(plume BEFORE PUBLIC ${REXSDK_DIR}/thirdparty/vulkan-memory-allocator/include)
endif()

# Small third-party pieces the renderer uses.
add_library(svr_native_thirdparty STATIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd/zstddeclib.c"
    "${SVR_XENOS_DIR}/thirdparty/xxHash/xxhash.c"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source/smolv.cpp"
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz/miniz.cpp")
target_include_directories(svr_native_thirdparty PUBLIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd"
    "${SVR_XENOS_DIR}/thirdparty/xxHash"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source"
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz")

# Host shaders (copy, ImGui overlay, MSAA resolve, gamma) compiled to SPIR-V headers with DXC on
# the build machine.
set(SVR_DXC_DIR "${SVR_XENOS_DIR}/thirdparty/dxc-bin")
set(SVR_DXC ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${SVR_DXC_DIR}/lib/x64" "${SVR_DXC_DIR}/bin/x64/dxc-linux")
set(SVR_HLSL_DIR "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/hlsl")
set(SVR_HOST_SHADER_HEADERS "")
function(svr_host_shader STEM PROFILE)
    file(GLOB hlsl_includes "${SVR_HLSL_DIR}/*.hlsli")
    set(ext spirv)
    # Keep cbuffer byte offsets identical to D3D, and match D3D clip space.
    set(args -spirv -fvk-use-dx-layout)
    if(PROFILE MATCHES "^vs")
        list(APPEND args -fvk-invert-y)
    endif()
    set(out "${SVR_NATIVE_GEN}/src/gpu/shaders/hlsl/${STEM}.hlsl.${ext}.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${SVR_DXC} -T ${PROFILE} -HV 2021 -all-resources-bound -Wno-ignored-attributes
                ${args} -I "${SVR_NATIVE_ROOT}" -Fh "${out}" "${SVR_HLSL_DIR}/${STEM}.hlsl"
                -Vn g_${STEM}_${ext}
        DEPENDS "${SVR_HLSL_DIR}/${STEM}.hlsl" ${hlsl_includes}
        COMMENT "Compiling ${STEM}.hlsl (${PROFILE}, ${ext})"
        VERBATIM)
    set(SVR_HOST_SHADER_HEADERS ${SVR_HOST_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()
foreach(shader IN ITEMS copy_vs imgui_vs)
    svr_host_shader(${shader} vs_6_0)
endforeach()
foreach(shader IN ITEMS copy_color_ps copy_depth_ps gamma_correction_ps pfx_occlusion_count_ps
        imgui_ps resolve_msaa_color_2x resolve_msaa_color_4x resolve_msaa_color_8x
        resolve_msaa_depth_2x resolve_msaa_depth_4x resolve_msaa_depth_8x)
    svr_host_shader(${shader} ps_6_0)
endforeach()

file(GLOB_RECURSE SVR_NATIVE_SOURCES CONFIGURE_DEPENDS
    "${SVR_NATIVE_ROOT}/src/reblue/*.cpp" "${SVR_NATIVE_ROOT}/src/reblue/*.h"
    "${SVR_NATIVE_ROOT}/src/native/*.cpp" "${SVR_NATIVE_ROOT}/src/native/*.h")
# Direct3D 12 only: the runtime DXC shader linker; and the Windows-only DRED diagnostics.
list(REMOVE_ITEM SVR_NATIVE_SOURCES
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.cpp"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.h"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_linker.cpp"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_linker.h")

function(svr_add_native_renderer target)
    target_sources(${target} PRIVATE
        ${SVR_NATIVE_SOURCES}
        ${SVR_HOST_SHADER_HEADERS}
        "${SVR_NATIVE_ROOT}/generated/native/shader_cache.cpp")
    target_include_directories(${target} PRIVATE
        "${SVR_NATIVE_ROOT}/src/reblue"
        "${SVR_NATIVE_ROOT}/src/native"
        "${SVR_NATIVE_GEN}"
        "${SVR_NATIVE_ROOT}/third_party/plume"
        "${SVR_XENOS_DIR}/XenosRecomp")
    target_compile_definitions(${target} PRIVATE SVR_NATIVE_RENDERER=1)
    # SvR's addresses for the D3D functions re:Blue's hooks name (D3DDevice_Clear -> sub_...).
    set_source_files_properties(${SVR_NATIVE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${SVR_NATIVE_ROOT}/src/native/svr_d3d_names.h")
    if(TARGET rex::o1heap)
        target_link_libraries(${target} PRIVATE rex::o1heap)
    else()
        target_link_libraries(${target} PRIVATE o1heap)  # (the SDK source tree)
    endif()
    target_link_libraries(${target} PRIVATE plume svr_native_thirdparty)
endfunction()
