/**
 * NovaPatternCatalog C API - Export C functions for Python ctypes binding
 *
 * This file provides a pure C API wrapper around the C++ NovaPatternCatalog class.
 * Python bindings use ctypes to call these functions via libnova_compute.so.
 *
 * Symbol visibility: Functions marked with extern "C" are exported from shared library.
 */

#include "NovaPatternCatalog.h"
#include <cstring>
#include <iostream>

// Export symbols for shared library
#ifdef _WIN32
    #define NOVA_API __declspec(dllexport)
#else
    #define NOVA_API __attribute__((visibility("default")))
#endif

extern "C" {

/**
 * Create NovaPatternCatalog instance.
 *
 * @param embedding_dim Pattern embedding dimensionality
 * @param max_patterns Maximum number of patterns in catalog
 * @param max_gpu_memory_mb Maximum VRAM allocation (MB)
 * @param enable_async Enable async compute queue
 * @return Opaque handle to catalog instance (void* pointing to NovaPatternCatalog*)
 */
NOVA_API void* nova_pattern_catalog_create(
    uint32_t embedding_dim,
    uint32_t max_patterns,
    uint32_t max_gpu_memory_mb,
    bool enable_async
) {
    try {
        NovaPatternCatalog::PatternSimilarityConfig config;
        config.embedding_dim = embedding_dim;
        config.max_patterns = max_patterns;
        config.max_gpu_memory_mb = max_gpu_memory_mb;
        config.enable_async = enable_async;

        auto* catalog = new NovaPatternCatalog(config);
        return static_cast<void*>(catalog);
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_catalog_create failed: " << e.what() << std::endl;
        return nullptr;
    }
}

/**
 * Initialize Nova compute resources (Vulkan device, buffers, pipelines).
 *
 * @param handle Catalog instance handle
 * @return true if initialization succeeded, false otherwise
 */
NOVA_API bool nova_pattern_catalog_initialize(void* handle) {
    if (!handle) {
        std::cerr << "nova_pattern_catalog_initialize: null handle" << std::endl;
        return false;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->initialize();
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_catalog_initialize failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Destroy NovaPatternCatalog instance and free GPU resources.
 *
 * @param handle Catalog instance handle
 */
NOVA_API void nova_pattern_catalog_destroy(void* handle) {
    if (!handle) {
        return;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        delete catalog;
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_catalog_destroy failed: " << e.what() << std::endl;
    }
}

/**
 * Add pattern to GPU catalog.
 *
 * @param handle Catalog instance handle
 * @param pattern_id Unique pattern identifier (null-terminated string)
 * @param embedding Pattern embedding array [embedding_dim], float32
 * @return true if pattern added successfully, false otherwise
 */
NOVA_API bool nova_pattern_add(
    void* handle,
    const char* pattern_id,
    const float* embedding
) {
    if (!handle || !pattern_id || !embedding) {
        std::cerr << "nova_pattern_add: null argument" << std::endl;
        return false;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);

        // Convert C array to std::vector
        std::string id(pattern_id);
        uint32_t embedding_dim = catalog->getEmbeddingDim();
        std::vector<float> embedding_vec(embedding, embedding + embedding_dim);

        return catalog->addPattern(id, embedding_vec);
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_add failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Remove pattern from catalog.
 *
 * @param handle Catalog instance handle
 * @param pattern_id Pattern identifier to remove
 * @return true if pattern removed successfully, false otherwise
 */
NOVA_API bool nova_pattern_remove(
    void* handle,
    const char* pattern_id
) {
    if (!handle || !pattern_id) {
        return false;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->removePattern(std::string(pattern_id));
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_remove failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Find top-K most similar patterns to query (GPU-accelerated).
 *
 * @param handle Catalog instance handle
 * @param query_embedding Query embedding array [embedding_dim], float32
 * @param top_k Number of top results to return
 * @param out_pattern_ids Output array of pattern IDs [top_k], caller-allocated
 * @param out_scores Output array of similarity scores [top_k], caller-allocated
 * @return Number of results returned (may be < top_k if catalog has fewer patterns)
 */
NOVA_API uint32_t nova_pattern_find_similar(
    void* handle,
    const float* query_embedding,
    uint32_t top_k,
    char** out_pattern_ids,
    float* out_scores
) {
    if (!handle || !query_embedding || !out_pattern_ids || !out_scores) {
        std::cerr << "nova_pattern_find_similar: null argument" << std::endl;
        return 0;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);

        // Convert C array to std::vector
        uint32_t embedding_dim = catalog->getEmbeddingDim();
        std::vector<float> query_vec(query_embedding, query_embedding + embedding_dim);

        // Run GPU similarity search
        auto results = catalog->findSimilar(query_vec, top_k);

        // Copy results to output arrays
        uint32_t count = static_cast<uint32_t>(results.size());
        for (uint32_t i = 0; i < count; i++) {
            const auto& [pattern_id, score] = results[i];

            // Allocate string memory (caller must free)
            out_pattern_ids[i] = strdup(pattern_id.c_str());
            out_scores[i] = score;
        }

        return count;
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_find_similar failed: " << e.what() << std::endl;
        return 0;
    }
}

/**
 * Get number of patterns in catalog.
 *
 * @param handle Catalog instance handle
 * @return Pattern count
 */
NOVA_API size_t nova_pattern_get_count(void* handle) {
    if (!handle) {
        return 0;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->getPatternCount();
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_get_count failed: " << e.what() << std::endl;
        return 0;
    }
}

/**
 * Get GPU memory usage (MB).
 *
 * @param handle Catalog instance handle
 * @return Memory usage in megabytes
 */
NOVA_API double nova_pattern_get_memory_usage(void* handle) {
    if (!handle) {
        return 0.0;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->getMemoryUsage();
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_get_memory_usage failed: " << e.what() << std::endl;
        return 0.0;
    }
}

/**
 * Check if memory usage is within safe limits.
 *
 * @param handle Catalog instance handle
 * @return true if memory usage is safe, false otherwise
 */
NOVA_API bool nova_pattern_is_memory_safe(void* handle) {
    if (!handle) {
        return false;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->isMemorySafe();
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_is_memory_safe failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Get catalog configuration (embedding_dim).
 * Added to fix embedding_dim access issue in other C API functions.
 *
 * @param handle Catalog instance handle
 * @return Embedding dimensionality
 */
NOVA_API uint32_t nova_pattern_get_embedding_dim(void* handle) {
    if (!handle) {
        return 0;
    }

    try {
        auto* catalog = static_cast<NovaPatternCatalog*>(handle);
        return catalog->getEmbeddingDim();
    } catch (const std::exception& e) {
        std::cerr << "nova_pattern_get_embedding_dim failed: " << e.what() << std::endl;
        return 0;
    }
}

} // extern "C"
