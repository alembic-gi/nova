#pragma once

#include "Nova.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

/**
 * NovaPatternCatalog - GPU-accelerated Pattern Similarity Search
 *
 * This class provides GPU-accelerated pattern catalog operations for Prima B2:
 * - Cosine similarity search using Vulkan compute shaders
 * - Top-K pattern selection with parallel sorting
 * - Memory-safe GPU buffer management
 * - Zero-copy MFN integration support
 *
 * Performance targets:
 * - 2-3x faster than PyTorch for similarity search
 * - <2GB VRAM usage (pre-allocated pools)
 * - 1-2ms for 1000 patterns @ 512-dim embeddings
 */
class NovaPatternCatalog {
public:
    struct PatternSimilarityConfig {
        // Catalog dimensions
        uint32_t embedding_dim = 512;       // Dimensionality of pattern embeddings
        uint32_t max_patterns = 10000;      // Maximum patterns in catalog
        uint32_t batch_size = 32;           // Batch size for multi-query operations

        // GPU settings
        uint32_t max_gpu_memory_mb = 2048;  // Maximum VRAM allocation
        bool enable_async = true;           // Enable async compute queue
        uint32_t compute_queue_count = 2;   // Number of compute queues

        // Performance tuning
        uint32_t work_group_size = 256;     // Shader work group size
        bool enable_gpu_fallback = true;    // Fall back to CPU if GPU fails
    };

    NovaPatternCatalog(const PatternSimilarityConfig& config);
    ~NovaPatternCatalog();

    // Initialization and teardown
    bool initialize();
    bool isInitialized() const;
    void shutdown();

    // Pattern management
    bool addPattern(const std::string& pattern_id, const std::vector<float>& embedding);
    bool removePattern(const std::string& pattern_id);
    bool updatePattern(const std::string& pattern_id, const std::vector<float>& embedding);
    size_t getPatternCount() const;
    bool hasPattern(const std::string& pattern_id) const;

    // Similarity search (single query)
    std::vector<std::pair<std::string, float>> findSimilar(
        const std::vector<float>& query_embedding,
        uint32_t top_k = 10
    );

    // Batch similarity search (multiple queries)
    std::vector<std::vector<std::pair<std::string, float>>> findSimilarBatch(
        const std::vector<std::vector<float>>& query_embeddings,
        uint32_t top_k = 10
    );

    // Memory safety and monitoring
    bool checkGPUMemory() const;
    double getMemoryUsage() const;
    double getVRAMUsage() const;
    bool isMemorySafe() const;

    // Performance metrics
    struct PerformanceMetrics {
        double average_search_time_ms;
        double average_batch_time_ms;
        double gpu_memory_usage_mb;
        double gpu_utilization_percent;
        uint64_t total_searches;
        uint64_t total_patterns_processed;
    };

    PerformanceMetrics getPerformanceMetrics() const;
    void resetPerformanceMetrics();

    // Zero-copy MFN integration
    VkBuffer getPatternEmbeddingsBuffer() const;
    VkBuffer getSimilarityScoresBuffer() const;
    bool synchronizeWithMFN(VkBuffer mfn_buffer);

private:
    PatternSimilarityConfig config_;
    std::unique_ptr<Nova> nova_engine_;
    bool initialized_;

    // GPU resources
    VkDevice device_;
    VkQueue compute_queue_;
    VkCommandPool compute_command_pool_;
    VkDescriptorPool descriptor_pool_;
    VkDescriptorSetLayout descriptor_set_layout_;
    VkPipelineLayout pipeline_layout_;

    // Compute pipelines
    VkPipeline pattern_similarity_pipeline_;
    VkPipeline topk_selection_pipeline_;
    VkShaderModule pattern_similarity_shader_;
    VkShaderModule topk_selection_shader_;

    // GPU buffers (VMA-managed for memory safety)
    VkBuffer pattern_embeddings_buffer_;
    VmaAllocation pattern_embeddings_allocation_;
    VkBuffer similarity_scores_buffer_;
    VmaAllocation similarity_scores_allocation_;
    VkBuffer topk_scores_buffer_;
    VmaAllocation topk_scores_allocation_;
    VkBuffer topk_indices_buffer_;
    VmaAllocation topk_indices_allocation_;
    VkBuffer params_buffer_;
    VmaAllocation params_buffer_allocation_;

    // Pattern index (maps pattern_id → buffer offset)
    std::vector<std::string> pattern_ids_;
    std::unordered_map<std::string, uint32_t> pattern_index_;
    std::mutex catalog_mutex_;

    // Performance tracking
    mutable PerformanceMetrics metrics_;
    mutable std::mutex metrics_mutex_;

    // Internal methods
    bool createComputePipelines();
    bool createBuffers();
    bool createDescriptorSets();
    VkShaderModule loadShader(const std::string& shader_path);

    bool runSimilarityCompute(
        const std::vector<float>& query_embeddings,
        uint32_t query_count
    );

    bool runTopKSelection(
        uint32_t num_scores,
        uint32_t k,
        std::vector<float>& topk_scores,
        std::vector<uint32_t>& topk_indices
    );

    void updateMetrics(double search_time_ms, uint64_t patterns_processed);
    bool validateEmbedding(const std::vector<float>& embedding) const;
    void cleanupResources();
};
