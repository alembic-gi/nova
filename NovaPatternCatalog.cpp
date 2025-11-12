#include "NovaPatternCatalog.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstring>

NovaPatternCatalog::NovaPatternCatalog(const PatternSimilarityConfig& config)
    : config_(config)
    , initialized_(false)
    , device_(VK_NULL_HANDLE)
    , compute_queue_(VK_NULL_HANDLE)
    , compute_command_pool_(VK_NULL_HANDLE)
    , descriptor_pool_(VK_NULL_HANDLE)
    , descriptor_set_layout_(VK_NULL_HANDLE)
    , pipeline_layout_(VK_NULL_HANDLE)
    , pattern_similarity_pipeline_(VK_NULL_HANDLE)
    , topk_selection_pipeline_(VK_NULL_HANDLE)
    , pattern_similarity_shader_(VK_NULL_HANDLE)
    , topk_selection_shader_(VK_NULL_HANDLE)
    , pattern_embeddings_buffer_(VK_NULL_HANDLE)
    , pattern_embeddings_allocation_(VK_NULL_HANDLE)
    , similarity_scores_buffer_(VK_NULL_HANDLE)
    , similarity_scores_allocation_(VK_NULL_HANDLE)
    , topk_scores_buffer_(VK_NULL_HANDLE)
    , topk_scores_allocation_(VK_NULL_HANDLE)
    , topk_indices_buffer_(VK_NULL_HANDLE)
    , topk_indices_allocation_(VK_NULL_HANDLE)
    , params_buffer_(VK_NULL_HANDLE)
    , params_buffer_allocation_(VK_NULL_HANDLE)
{
    memset(&metrics_, 0, sizeof(metrics_));
}

NovaPatternCatalog::~NovaPatternCatalog() {
    shutdown();
}

bool NovaPatternCatalog::initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize Nova engine
    nova_engine_ = std::make_unique<Nova>();
    if (!nova_engine_->Initialize()) {
        std::cerr << "NovaPatternCatalog: Failed to initialize Nova engine" << std::endl;
        return false;
    }

    device_ = nova_engine_->GetDevice();
    compute_queue_ = nova_engine_->GetComputeQueue();

    // Create compute resources
    if (!createBuffers()) {
        std::cerr << "NovaPatternCatalog: Failed to create buffers" << std::endl;
        return false;
    }

    if (!createComputePipelines()) {
        std::cerr << "NovaPatternCatalog: Failed to create compute pipelines" << std::endl;
        return false;
    }

    if (!createDescriptorSets()) {
        std::cerr << "NovaPatternCatalog: Failed to create descriptor sets" << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "NovaPatternCatalog: Initialized successfully" << std::endl;
    std::cout << "  - Max patterns: " << config_.max_patterns << std::endl;
    std::cout << "  - Embedding dim: " << config_.embedding_dim << std::endl;
    std::cout << "  - Max VRAM: " << config_.max_gpu_memory_mb << " MB" << std::endl;

    return true;
}

bool NovaPatternCatalog::isInitialized() const {
    return initialized_;
}

void NovaPatternCatalog::shutdown() {
    if (!initialized_) {
        return;
    }

    cleanupResources();
    nova_engine_.reset();
    initialized_ = false;
}

bool NovaPatternCatalog::addPattern(const std::string& pattern_id, const std::vector<float>& embedding) {
    if (!initialized_) {
        std::cerr << "NovaPatternCatalog: Not initialized" << std::endl;
        return false;
    }

    if (!validateEmbedding(embedding)) {
        std::cerr << "NovaPatternCatalog: Invalid embedding dimensions" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(catalog_mutex_);

    // Check if pattern already exists
    if (pattern_index_.find(pattern_id) != pattern_index_.end()) {
        std::cerr << "NovaPatternCatalog: Pattern already exists: " << pattern_id << std::endl;
        return false;
    }

    // Check capacity
    if (pattern_ids_.size() >= config_.max_patterns) {
        std::cerr << "NovaPatternCatalog: Catalog full (max: " << config_.max_patterns << ")" << std::endl;
        return false;
    }

    // Add to index
    uint32_t pattern_idx = static_cast<uint32_t>(pattern_ids_.size());
    pattern_ids_.push_back(pattern_id);
    pattern_index_[pattern_id] = pattern_idx;

    // Upload embedding to GPU buffer
    // TODO: Implement GPU buffer upload via Nova VMA
    // For now, placeholder - will be implemented in full version

    return true;
}

bool NovaPatternCatalog::removePattern(const std::string& pattern_id) {
    std::lock_guard<std::mutex> lock(catalog_mutex_);

    auto it = pattern_index_.find(pattern_id);
    if (it == pattern_index_.end()) {
        return false;
    }

    // Remove from index
    uint32_t idx = it->second;
    pattern_index_.erase(it);

    // Swap with last pattern (to avoid shifting)
    if (idx < pattern_ids_.size() - 1) {
        std::string last_id = pattern_ids_.back();
        pattern_ids_[idx] = last_id;
        pattern_index_[last_id] = idx;
    }
    pattern_ids_.pop_back();

    return true;
}

bool NovaPatternCatalog::updatePattern(const std::string& pattern_id, const std::vector<float>& embedding) {
    if (!removePattern(pattern_id)) {
        return false;
    }
    return addPattern(pattern_id, embedding);
}

size_t NovaPatternCatalog::getPatternCount() const {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    return pattern_ids_.size();
}

bool NovaPatternCatalog::hasPattern(const std::string& pattern_id) const {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    return pattern_index_.find(pattern_id) != pattern_index_.end();
}

std::vector<std::pair<std::string, float>> NovaPatternCatalog::findSimilar(
    const std::vector<float>& query_embedding,
    uint32_t top_k
) {
    auto start = std::chrono::high_resolution_clock::now();

    if (!initialized_) {
        std::cerr << "NovaPatternCatalog: Not initialized" << std::endl;
        return {};
    }

    if (!validateEmbedding(query_embedding)) {
        std::cerr << "NovaPatternCatalog: Invalid query embedding" << std::endl;
        return {};
    }

    std::lock_guard<std::mutex> lock(catalog_mutex_);

    if (pattern_ids_.empty()) {
        return {};
    }

    // Clamp top_k to catalog size
    top_k = std::min(top_k, static_cast<uint32_t>(pattern_ids_.size()));

    // Run GPU similarity compute
    if (!runSimilarityCompute(query_embedding, 1)) {
        std::cerr << "NovaPatternCatalog: Similarity compute failed" << std::endl;
        return {};
    }

    // Run GPU top-K selection
    std::vector<float> topk_scores;
    std::vector<uint32_t> topk_indices;
    if (!runTopKSelection(static_cast<uint32_t>(pattern_ids_.size()), top_k, topk_scores, topk_indices)) {
        std::cerr << "NovaPatternCatalog: Top-K selection failed" << std::endl;
        return {};
    }

    // Build results
    std::vector<std::pair<std::string, float>> results;
    results.reserve(top_k);
    for (uint32_t i = 0; i < top_k; i++) {
        uint32_t idx = topk_indices[i];
        if (idx < pattern_ids_.size()) {
            results.emplace_back(pattern_ids_[idx], topk_scores[i]);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    updateMetrics(elapsed_ms, pattern_ids_.size());

    return results;
}

std::vector<std::vector<std::pair<std::string, float>>> NovaPatternCatalog::findSimilarBatch(
    const std::vector<std::vector<float>>& query_embeddings,
    uint32_t top_k
) {
    // TODO: Implement batched GPU operations for performance
    // For now, use sequential findSimilar
    std::vector<std::vector<std::pair<std::string, float>>> results;
    results.reserve(query_embeddings.size());

    for (const auto& query : query_embeddings) {
        results.push_back(findSimilar(query, top_k));
    }

    return results;
}

bool NovaPatternCatalog::checkGPUMemory() const {
    if (!nova_engine_) {
        return false;
    }
    return nova_engine_->CheckGPUResources();
}

double NovaPatternCatalog::getMemoryUsage() const {
    if (!nova_engine_) {
        return 0.0;
    }
    return nova_engine_->GetGPUMemoryUsage();
}

double NovaPatternCatalog::getVRAMUsage() const {
    return getMemoryUsage();
}

bool NovaPatternCatalog::isMemorySafe() const {
    double usage_mb = getMemoryUsage();
    return usage_mb < config_.max_gpu_memory_mb;
}

NovaPatternCatalog::PerformanceMetrics NovaPatternCatalog::getPerformanceMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

void NovaPatternCatalog::resetPerformanceMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    memset(&metrics_, 0, sizeof(metrics_));
}

VkBuffer NovaPatternCatalog::getPatternEmbeddingsBuffer() const {
    return pattern_embeddings_buffer_;
}

VkBuffer NovaPatternCatalog::getSimilarityScoresBuffer() const {
    return similarity_scores_buffer_;
}

bool NovaPatternCatalog::synchronizeWithMFN(VkBuffer mfn_buffer) {
    // TODO: Implement zero-copy buffer synchronization with MFN
    // Use VkBuffer memory sharing for efficient data transfer
    return true;
}

// Private methods

bool NovaPatternCatalog::createComputePipelines() {
    // Load shaders
    pattern_similarity_shader_ = loadShader("Core/components/shaders/pattern_similarity.comp.spv");
    if (pattern_similarity_shader_ == VK_NULL_HANDLE) {
        return false;
    }

    topk_selection_shader_ = loadShader("Core/components/shaders/topk_selection.comp.spv");
    if (topk_selection_shader_ == VK_NULL_HANDLE) {
        return false;
    }

    // TODO: Create compute pipeline layout and pipelines
    // Will be implemented using Nova's Vulkan abstraction

    return true;
}

bool NovaPatternCatalog::createBuffers() {
    // TODO: Create VMA-managed buffers for:
    // - pattern_embeddings_buffer_ (max_patterns * embedding_dim * sizeof(float))
    // - similarity_scores_buffer_ (max_patterns * sizeof(float))
    // - topk_scores_buffer_ (256 * sizeof(float))
    // - topk_indices_buffer_ (256 * sizeof(uint32_t))
    // - params_buffer_ (uniform params)

    return true;
}

bool NovaPatternCatalog::createDescriptorSets() {
    // TODO: Create descriptor sets for compute shader bindings
    return true;
}

VkShaderModule NovaPatternCatalog::loadShader(const std::string& shader_path) {
    // TODO: Load SPIR-V shader module
    // For now, return placeholder
    return VK_NULL_HANDLE;
}

bool NovaPatternCatalog::runSimilarityCompute(
    const std::vector<float>& query_embeddings,
    uint32_t query_count
) {
    // TODO: Dispatch pattern_similarity.comp shader
    // 1. Upload query embeddings to GPU
    // 2. Bind descriptor sets (query, catalog, output, params)
    // 3. Dispatch compute (work groups: ceil(query_count * catalog_size / 256))
    // 4. Wait for completion

    return true;
}

bool NovaPatternCatalog::runTopKSelection(
    uint32_t num_scores,
    uint32_t k,
    std::vector<float>& topk_scores,
    std::vector<uint32_t>& topk_indices
) {
    // TODO: Dispatch topk_selection.comp shader
    // 1. Bind descriptor sets (scores, topk_scores, topk_indices, params)
    // 2. Dispatch compute (work groups: 1, since bitonic sort requires single work group)
    // 3. Download results from GPU
    // 4. Wait for completion

    topk_scores.resize(k);
    topk_indices.resize(k);

    return true;
}

void NovaPatternCatalog::updateMetrics(double search_time_ms, uint64_t patterns_processed) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    metrics_.total_searches++;
    metrics_.total_patterns_processed += patterns_processed;

    // Exponential moving average for timings
    double alpha = 0.1;
    metrics_.average_search_time_ms =
        alpha * search_time_ms + (1.0 - alpha) * metrics_.average_search_time_ms;

    if (nova_engine_) {
        metrics_.gpu_memory_usage_mb = nova_engine_->GetGPUMemoryUsage();
        metrics_.gpu_utilization_percent = nova_engine_->GetGPUUtilization();
    }
}

bool NovaPatternCatalog::validateEmbedding(const std::vector<float>& embedding) const {
    return embedding.size() == config_.embedding_dim;
}

void NovaPatternCatalog::cleanupResources() {
    // TODO: Cleanup Vulkan resources
    // - Destroy pipelines
    // - Destroy shader modules
    // - Free VMA buffers
    // - Destroy descriptor sets/pools
    // - Destroy command pool
}
