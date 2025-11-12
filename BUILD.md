# Nova Compute Library - Build Instructions

## Prerequisites

### System Requirements
- **OS**: Arch Linux (or compatible)
- **GPU**: AMD GPU with Vulkan support
- **CMake**: >= 3.20
- **Compiler**: GCC 11+ or Clang 14+ with C++20 support
- **Vulkan SDK**: Latest version (1.3+)

### Required Packages

Install dependencies using pacman:

```bash
# Core build tools
sudo pacman -S cmake gcc make

# Vulkan development
sudo pacman -S vulkan-devel vulkan-headers vulkan-validation-layers

# Vulkan Memory Allocator
sudo pacman -S vulkan-memory-allocator

# SDL2 (optional, for windowing support)
sudo pacman -S sdl2

# Shader compiler (included in vulkan-devel)
# Verify glslc is available:
which glslc
```

### Verify Vulkan Setup

```bash
# Check Vulkan installation
vulkaninfo | head -20

# Verify GPU support
vkcube  # Should display a spinning cube
```

## Build Steps

### Quick Build (Release)

```bash
cd /home/persist/alembic/Nova
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Development Build (Debug)

```bash
cd /home/persist/alembic/Nova
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### Build Output

The build process will:
1. Compile compute shaders to SPIR-V (`.spv` files)
2. Build the `libnova_compute.so` shared library
3. Place compiled shaders in `build/shaders/`

Expected output:
```
build/
├── libnova_compute.so       # Main shared library
├── libnova_compute.so.1     # Versioned symlink
└── shaders/
    ├── pattern_similarity.comp.spv
    ├── topk_selection.comp.spv
    └── ml_processing.comp.spv
```

## Installation

### System-Wide Installation

```bash
cd build
sudo make install
```

This installs:
- **Library**: `/usr/local/lib/libnova_compute.so`
- **Headers**: `/usr/local/include/nova/*.h`
- **Shaders**: `/usr/local/share/nova/shaders/*.spv`
- **CMake Config**: `/usr/local/lib/cmake/Nova/`

### Custom Install Prefix

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
make install
```

### Uninstall

```bash
cd build
sudo make uninstall  # If supported by CMake
# Or manually remove files from install manifest
```

## Build Configuration Options

### CMake Variables

```bash
# Build type (Release, Debug, RelWithDebInfo)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Custom install prefix
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..

# Specify Vulkan SDK path
cmake -DVULKAN_SDK=/path/to/vulkan/sdk ..

# Enable verbose build output
cmake -DCMAKE_VERBOSE_MAKEFILE=ON ..
make VERBOSE=1
```

### Compiler Selection

```bash
# Use Clang instead of GCC
cmake -DCMAKE_CXX_COMPILER=clang++ ..

# Use specific GCC version
cmake -DCMAKE_CXX_COMPILER=g++-13 ..
```

## Linking Against Nova Compute

### Using CMake (Recommended)

```cmake
find_package(Nova REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Nova::nova_compute)
```

### Using pkg-config

```bash
# Compile
g++ -std=c++20 main.cpp -o my_app \
    $(pkg-config --cflags vulkan) \
    -I/usr/local/include/nova \
    -L/usr/local/lib -lnova_compute \
    $(pkg-config --libs vulkan)
```

### Manual Linking

```bash
g++ -std=c++20 main.cpp -o my_app \
    -I/usr/local/include/nova \
    -L/usr/local/lib \
    -lnova_compute \
    -lvulkan \
    -lpthread
```

## Troubleshooting

### Vulkan SDK Not Found

```bash
# Set VULKAN_SDK environment variable
export VULKAN_SDK=/usr/share/vulkan
cmake ..
```

### VMA Header Not Found

```bash
# Install VMA
sudo pacman -S vulkan-memory-allocator

# Or specify custom path
cmake -DVMA_INCLUDE_DIR=/path/to/vma/include ..
```

### Shader Compilation Fails

```bash
# Verify glslc is in PATH
which glslc

# Test shader compilation manually
glslc -fshader-stage=compute \
    Core/components/shaders/pattern_similarity.comp \
    -o test.spv
```

### Library Not Found at Runtime

```bash
# Add to LD_LIBRARY_PATH (temporary)
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Or update ldconfig (permanent)
sudo ldconfig /usr/local/lib
```

### Missing SDL2 (Optional)

If SDL2 is not needed (compute-only use):
```bash
# Build will succeed without SDL2
# Windowing features will be disabled
cmake ..
make
```

To enable SDL2 support:
```bash
sudo pacman -S sdl2
cmake ..
make
```

## Performance Optimization

### Release Build with Native Optimizations

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native" ..
make -j$(nproc)
```

### Link-Time Optimization (LTO)

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ..
make -j$(nproc)
```

## Verification

### Run Library Tests

```bash
# Check library symbols
nm -D build/libnova_compute.so | grep -i pattern

# Verify Vulkan linkage
ldd build/libnova_compute.so | grep vulkan

# Check shader compilation
ls -lh build/shaders/*.spv
```

### Example Usage

```cpp
#include <NovaPatternCatalog.h>
#include <iostream>

int main() {
    NovaPatternCatalog::PatternSimilarityConfig config;
    config.embedding_dim = 512;
    config.max_patterns = 1000;

    NovaPatternCatalog catalog(config);
    if (catalog.initialize()) {
        std::cout << "Nova Compute Library initialized successfully!" << std::endl;
        catalog.shutdown();
        return 0;
    }
    return 1;
}
```

Compile example:
```bash
g++ -std=c++20 example.cpp -o example \
    -I/usr/local/include/nova \
    -L/usr/local/lib -lnova_compute -lvulkan
./example
```

## Clean Build

```bash
# Remove build directory
rm -rf build

# Full rebuild
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make clean
make -j$(nproc)
```

## Build Artifacts

After successful build:
- **Source**: `/home/persist/alembic/Nova/`
- **Build**: `/home/persist/alembic/Nova/build/`
- **Library**: `build/libnova_compute.so`
- **Shaders**: `build/shaders/*.spv`
- **Headers**: Source tree (installed to `/usr/local/include/nova/`)

## Next Steps

1. Build the library: `cd Nova && mkdir build && cd build && cmake .. && make -j$(nproc)`
2. Run tests: `./run_tests` (if available)
3. Install: `sudo make install`
4. Integrate into Prima B2 MFN system

For Prima B2 integration, see `B2_MFN_INTEGRATION.md`.
