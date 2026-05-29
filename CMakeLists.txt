cmake_minimum_required(VERSION 3.25)

project(VulkanSoftShadows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(vt3WarningsAsErrors "Treat compiler warnings as errors." OFF)
option(vt3ValidationDefault "Enable validation by default in debug builds." ON)


# Dependencies
find_package(Vulkan REQUIRED)  # GPU API
find_program(GLSLC NAMES glslc REQUIRED)  # shader compiler

# GLFW - cross-platform interactive windows
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/glfw/CMakeLists.txt")
    message(FATAL_ERROR "GLFW source not found. Expected: third_party/glfw")
endif()

# GLM - matrix operations
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/glm/glm/glm.hpp")
    message(FATAL_ERROR "GLM source not found. Expected: third_party/glm")
endif()

add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)


# Shaders
set(shaderPaths
    shaders/scene.vert
    shaders/scene.frag
    shaders/scene_rt.frag
)

set(compiledShaders)
foreach (shaderPath IN LISTS shaderPaths)
    get_filename_component(shaderName "${shaderPath}" NAME)
    set(shaderFullPath "${CMAKE_SOURCE_DIR}/${shaderPath}")
    set(shaderOutputPath "${CMAKE_BINARY_DIR}/shaders/${shaderName}.spv")

    add_custom_command(
        OUTPUT "${shaderOutputPath}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLC}" --target-env=vulkan1.3 "${shaderFullPath}" -o "${shaderOutputPath}"
        DEPENDS "${shaderFullPath}"
        COMMENT "Compiling ${shaderPath}"
        VERBATIM
    )

    list(APPEND compiledShaders "${shaderOutputPath}")
endforeach()

add_custom_target(vulkanSoftShadowsShaders ALL DEPENDS ${compiledShaders})


# Executable
add_executable(vulkanSoftShadows
    src/App.cpp
    src/SceneRenderer.cpp
    src/SceneRenderer.h
    src/RayTracingSystem.cpp
    src/RayTracingSystem.h
    src/Scene.cpp
    src/Scene.h
    src/SceneSimulator.cpp
    src/SceneSimulator.h
    src/SwapchainSystem.cpp
    src/SwapchainSystem.h
    src/VulkanContext.cpp
    src/VulkanContext.h
    src/WindowSystem.cpp
    src/WindowSystem.h
    src/RenderConstants.h
    src/main.cpp
    src/Header.h
    src/VulkanHelpers.h
    src/VulkanHelpers.cpp
)

add_dependencies(vulkanSoftShadows vulkanSoftShadowsShaders)

target_include_directories(vulkanSoftShadows PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/third_party/glm"
)

target_link_libraries(vulkanSoftShadows PRIVATE glfw Vulkan::Vulkan)


# Debug options
if (vt3ValidationDefault)
    target_compile_definitions(vulkanSoftShadows PRIVATE VSS_VALIDATION=1)
else()
    target_compile_definitions(vulkanSoftShadows PRIVATE VSS_VALIDATION=0)
endif()

target_compile_definitions(vulkanSoftShadows PRIVATE $<$<CONFIG:Debug>:VSS_DEBUG=1>)
