cmake_minimum_required(VERSION 3.20)
project(Nova VERSION 1.0.0 LANGUAGES CXX)

# C++20 required (for std::span and designated initializers)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags for release
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Export compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Vulkan REQUIRED)

# Find glslc compiler for shader compilation
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc shader compiler not found. Please install Vulkan SDK.")
endif()

# Find VMA (Vulkan Memory Allocator)
# Try to find VMA header in standard locations
find_path(VMA_INCLUDE_DIR
    NAMES vk_mem_alloc.h
    PATHS
        /usr/include
        /usr/local/include
        ${VULKAN_SDK}/include
        $ENV{VULKAN_SDK}/include
    PATH_SUFFIXES vma vulkan
)

if(NOT VMA_INCLUDE_DIR)
    message(WARNING "VMA header not found in system. Using local copy if available.")
    # Fallback to local copy in Core/components if present
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Core/components/vk_memory.h")
        set(VMA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Core/components")
    else()
        message(FATAL_ERROR "VMA (Vulkan Memory Allocator) not found. Install via: sudo pacman -S vulkan-memory-allocator")
    endif()
endif()

message(STATUS "VMA Include Directory: ${VMA_INCLUDE_DIR}")

# SDL2 for window management (optional for compute-only builds)
find_package(SDL2)
if(SDL2_FOUND)
    message(STATUS "SDL2 found, including windowing support")
else()
    message(STATUS "SDL2 not found, building compute-only library")
endif()

# Shader compilation
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Core/components/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Create shader output directory
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Define compute shaders to compile
set(COMPUTE_SHADERS
    pattern_similarity.comp
    topk_selection.comp
    ml_processing.comp
)

# Compile each shader to SPIR-V
set(COMPILED_SHADERS)
foreach(SHADER ${COMPUTE_SHADERS})
    set(SHADER_SOURCE "${SHADER_SOURCE_DIR}/${SHADER}")
    set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER}.spv")

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} -fshader-stage=compute ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
endforeach()

# Create custom target for shader compilation
add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})

# Source files for libnova_compute.so
set(NOVA_COMPUTE_SOURCES
    NovaPatternCatalog.cpp
    NovaMLManager.cpp
    Nova.cpp
    Core/core.cpp
    Core/modules/device.cpp
    Core/modules/render.cpp
    Core/modules/management.cpp
    Core/modules/presentation.cpp
    Core/modules/pipeline/pipeline.cpp
    Core/modules/pipeline/scene.cpp
    Core/modules/camera/camera.cpp
    Core/modules/camera/perspective.cpp
    Core/modules/atomic/atomic.cpp
    Core/components/genesis.cpp
    Core/components/logger.cpp
    # Core/components/obj_loader.cpp  # Excluded: requires fastgltf (not needed for compute)
)

# Header files
set(NOVA_COMPUTE_HEADERS
    Nova.h
    NovaPatternCatalog.h
    NovaMLManager.h
    Core/core.h
    Core/config.h
    Core/components/genesis.h
    Core/components/logger.h
    Core/components/lexicon.h
    Core/components/vertex.h
    Core/components/debug_level.h
    Core/components/obj_loader.h
    Core/components/vk_memory.h
    Core/modules/atomic/atomic.h
    Core/modules/pipeline/pipeline.h
    Core/modules/camera/camera.h
    Core/modules/camera/perspective.h
)

# Build shared library
add_library(nova_compute SHARED ${NOVA_COMPUTE_SOURCES})

# Set library version
set_target_properties(nova_compute PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${NOVA_COMPUTE_HEADERS}"
)

# Include directories
target_include_directories(nova_compute
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/components>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/components/extern>
        $<INSTALL_INTERFACE:include/nova>
    PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        ${VMA_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(nova_compute
    PUBLIC
        Vulkan::Vulkan
    PRIVATE
        ${CMAKE_DL_LIBS}
        pthread
)

# Add SDL2 if available
if(SDL2_FOUND)
    target_include_directories(nova_compute PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nova_compute PRIVATE ${SDL2_LIBRARIES})
    target_compile_definitions(nova_compute PRIVATE NOVA_SDL2_ENABLED)
endif()

# Compiler options
target_compile_options(nova_compute PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<CONFIG:Debug>:-g -O0>
)

# Export C API symbols
target_compile_definitions(nova_compute PRIVATE
    NOVA_EXPORT_SYMBOLS
    VK_USE_PLATFORM_XCB_KHR  # Linux/X11 platform
)

# Ensure shaders are compiled before library
add_dependencies(nova_compute CompileShaders)

# Installation rules
include(GNUInstallDirs)

# Install library
install(TARGETS nova_compute
    EXPORT NovaComputeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nova
)

# Install shaders
install(FILES ${COMPILED_SHADERS}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nova/shaders
)

# Install CMake config files for find_package support
install(EXPORT NovaComputeTargets
    FILE NovaComputeTargets.cmake
    NAMESPACE Nova::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
)

# Create config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NovaComputeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/NovaComputeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/NovaComputeConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
)

# Only install config if the .in file exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/NovaComputeConfig.cmake.in")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NovaComputeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NovaComputeConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
    )
endif()

# Print build configuration
message(STATUS "===============================================")
message(STATUS "Nova Compute Library Configuration")
message(STATUS "===============================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "VMA Include: ${VMA_INCLUDE_DIR}")
message(STATUS "SDL2 Support: ${SDL2_FOUND}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Shader Compiler: ${GLSLC}")
message(STATUS "===============================================")
