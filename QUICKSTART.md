# Nova Compute Library - Quick Start

## Build & Install (One-liner)

```bash
cd /home/persist/alembic/Nova && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc) && sudo make install
```

## Minimal Example

```cpp
// example.cpp
#include <NovaPatternCatalog.h>
#include <iostream>
#include <vector>

int main() {
    // Configure pattern catalog
    NovaPatternCatalog::PatternSimilarityConfig config;
    config.embedding_dim = 512;
    config.max_patterns = 1000;
    config.max_gpu_memory_mb = 2048;
    
    // Initialize
    NovaPatternCatalog catalog(config);
    if (!catalog.initialize()) {
        std::cerr << "Failed to initialize Nova Compute Library" << std::endl;
        return 1;
    }
    
    std::cout << "Nova Compute Library initialized successfully!" << std::endl;
    std::cout << "GPU Memory: " << catalog.getVRAMUsage() << " MB" << std::endl;
    
    // Add pattern
    std::vector<float> embedding(512, 0.1f);
    catalog.addPattern("pattern_001", embedding);
    
    // Search for similar patterns
    std::vector<float> query(512, 0.1f);
    auto results = catalog.findSimilar(query, 10);
    
    std::cout << "Found " << results.size() << " similar patterns" << std::endl;
    for (const auto& [id, score] : results) {
        std::cout << "  " << id << ": " << score << std::endl;
    }
    
    catalog.shutdown();
    return 0;
}
```

## Compile Example

```bash
g++ -std=c++20 example.cpp -o example \
    -I/usr/local/include/nova \
    -L/usr/local/lib -lnova_compute -lvulkan
    
LD_LIBRARY_PATH=/usr/local/lib ./example
```

## CMake Integration

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(MyApp)

set(CMAKE_CXX_STANDARD 20)

find_package(Nova REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE Nova::nova_compute)
```

## Library Features

### NovaPatternCatalog
- GPU-accelerated cosine similarity search
- Top-K pattern selection
- Batch operations
- Memory-safe buffer management (VMA)
- Target: 2-3x faster than PyTorch

### NovaMLManager
- GPU-accelerated CNN operations
- Screen analysis
- Action generation
- Model training/inference

## Performance Tips

1. **Pre-allocate patterns**: Add all patterns before searching
2. **Batch queries**: Use `findSimilarBatch()` for multiple queries
3. **Reuse catalog**: Initialize once, query many times
4. **Monitor VRAM**: Call `checkGPUMemory()` periodically

## Troubleshooting

**Library not found:**
```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
# Or
sudo ldconfig /usr/local/lib
```

**Vulkan error:**
```bash
# Check Vulkan
vulkaninfo | head -20
# Verify GPU
lspci | grep VGA
```

**Build failed:**
```bash
# Clean rebuild
cd build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
```

## File Locations

- **Library**: `/usr/local/lib/libnova_compute.so`
- **Headers**: `/usr/local/include/nova/*.h`
- **Shaders**: `/usr/local/share/nova/shaders/*.spv`
- **Build**: `/home/persist/alembic/Nova/build/`

## Documentation

- Full build instructions: `BUILD.md`
- API reference: See header files in `/usr/local/include/nova/`
- Prima B2 integration: `B2_MFN_INTEGRATION.md` (in Prima directory)

## Next Steps

1. Build: `cd Nova && mkdir build && cd build && cmake .. && make -j$(nproc)`
2. Test: Run example above
3. Install: `sudo make install`
4. Integrate: Use in Prima B2 MFN system
