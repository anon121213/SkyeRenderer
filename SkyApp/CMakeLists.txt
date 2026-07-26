include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG        v2.9.3
        GIT_SHALLOW    TRUE
)
set(TINYGLTF_HEADER_ONLY          ON  CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL              OFF CACHE INTERNAL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE INTERNAL "" FORCE)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(tinygltf)
FetchContent_MakeAvailable(imgui)   # no CMakeLists — just populates ${imgui_SOURCE_DIR}

# ImGui as a static lib: core + GLFW backend + Vulkan backend (via volk, since we use VK_NO_PROTOTYPES)
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC glfw volk)

add_executable(SkyApp
        src/main.cpp
        src/Window.cpp
        src/Window.h
        src/GltfModel.cpp
        src/GltfModel.h
)

# ---------- Компиляция шейдеров ----------
find_program(GLSLC NAMES glslc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADERS
        ${SHADER_SRC_DIR}/triangle.vert
        ${SHADER_SRC_DIR}/triangle.frag
        ${SHADER_SRC_DIR}/skybox.vert
        ${SHADER_SRC_DIR}/skybox.frag
        ${SHADER_SRC_DIR}/brdf.frag
        ${SHADER_SRC_DIR}/irradiance.frag
        ${SHADER_SRC_DIR}/prefilter.frag
        ${SHADER_SRC_DIR}/shadow.vert
        ${SHADER_SRC_DIR}/shadow.frag
)

set(SPIRV_BINARIES "")
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUT ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
            OUTPUT ${SPIRV_OUT}
            COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME} -> SPIR-V"
            VERBATIM
    )
    list(APPEND SPIRV_BINARIES ${SPIRV_OUT})
endforeach()

add_custom_target(CompileShaders DEPENDS ${SPIRV_BINARIES})
add_dependencies(SkyApp CompileShaders)

target_compile_definitions(SkyApp PRIVATE SHADER_DIR="${SHADER_BIN_DIR}")
# ---------- Конец компиляции шейдеров ----------

target_link_libraries(SkyApp PRIVATE SkyRenderer glfw glm::glm imgui)
target_compile_definitions(SkyApp PRIVATE GLFW_INCLUDE_VULKAN)

target_include_directories(SkyApp PRIVATE ${tinygltf_SOURCE_DIR})
target_compile_definitions(SkyApp PRIVATE ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
