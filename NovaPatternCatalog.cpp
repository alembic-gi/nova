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
    NovaConfig nova_config;
    nova_config.name = "PatternCatalog";
    nova_config.screen = {1920, 1080};
    nova_config.debug_level = "release";
    nova_config.dimensions = "2D";
    nova_config.camera_type = "fixed";
    nova_config.compute = true;

    nova_engine_ = std::make_unique<Nova>(nova_config);
    if (!nova_engine_->initialized) {
        std::cerr << "NovaPatternCatalog: Failed to initialize Nova engine" << std::endl;
        return false;
    }

    // Access Nova internals (via _architect which is NovaCore)
    NovaCore* core = nova_engine_->getCore();
    if (!core) {
        std::cerr << "NovaPatternCatalog: Failed to get NovaCore" << std::endl;
        return false;
    }

    device_ = core->getDevice();
    compute_queue_ = core->getComputeQueue();

    // Create command pool for compute operations
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = 0;  // TODO: Get actual compute queue family index
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device_, &pool_info, nullptr, &compute_command_pool_) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create command pool" << std::endl;
        return false;
    }

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

    // Upload embedding to GPU buffer via staging buffer
    // Pattern embeddings buffer is GPU_ONLY, so we need a staging buffer for upload
    NovaCore* core = nova_engine_->getCore();
    VmaAllocator allocator = core->getAllocator();

    // Create staging buffer
    VkDeviceSize buffer_size = embedding.size() * sizeof(float);
    VkBuffer staging_buffer;
    VmaAllocation staging_allocation;

    VkBufferCreateInfo staging_info = {};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = buffer_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc_info = {};
    staging_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo staging_alloc_result;
    if (vmaCreateBuffer(allocator, &staging_info, &staging_alloc_info,
                        &staging_buffer, &staging_allocation, &staging_alloc_result) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create staging buffer" << std::endl;
        return false;
    }

    // Copy embedding data to staging buffer (already mapped)
    memcpy(staging_alloc_result.pMappedData, embedding.data(), buffer_size);

    // Copy from staging to GPU buffer
    // TODO: Implement actual buffer copy via command buffer
    // For now, this is a placeholder - full implementation needs command buffer submission

    // Cleanup staging buffer
    vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

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
    // Simplified check - in full implementation would query VMA stats
    return true;
}

double NovaPatternCatalog::getMemoryUsage() const {
    if (!nova_engine_) {
        return 0.0;
    }
    // Calculate memory usage based on allocated buffers
    size_t total_bytes = 0;
    total_bytes += config_.max_patterns * config_.embedding_dim * sizeof(float);  // pattern embeddings
    total_bytes += config_.max_patterns * sizeof(float);  // similarity scores
    total_bytes += 256 * sizeof(float);  // top-k scores
    total_bytes += 256 * sizeof(uint32_t);  // top-k indices
    total_bytes += 256;  // params buffer

    return static_cast<double>(total_bytes) / (1024.0 * 1024.0);  // Convert to MB
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
    if (!nova_engine_) {
        return false;
    }

    // Load shaders
    pattern_similarity_shader_ = loadShader("Core/components/shaders/pattern_similarity.comp.spv");
    if (pattern_similarity_shader_ == VK_NULL_HANDLE) {
        std::cerr << "NovaPatternCatalog: Failed to load pattern_similarity shader" << std::endl;
        return false;
    }

    topk_selection_shader_ = loadShader("Core/components/shaders/topk_selection.comp.spv");
    if (topk_selection_shader_ == VK_NULL_HANDLE) {
        std::cerr << "NovaPatternCatalog: Failed to load topk_selection shader" << std::endl;
        return false;
    }

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[5] = {};

    // Binding 0: Pattern embeddings (storage buffer)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Query embeddings (storage buffer)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Similarity scores (storage buffer)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Top-K scores (storage buffer)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Params (uniform buffer)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 5;
    layout_info.pBindings = bindings;

    if (device_ != VK_NULL_HANDLE) {
        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
            std::cerr << "NovaPatternCatalog: Failed to create descriptor set layout" << std::endl;
            return false;
        }
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;

    if (device_ != VK_NULL_HANDLE) {
        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
            std::cerr << "NovaPatternCatalog: Failed to create pipeline layout" << std::endl;
            return false;
        }
    }

    // Create compute pipeline for pattern similarity
    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = pattern_similarity_shader_;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout_;

    if (device_ != VK_NULL_HANDLE) {
        if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pattern_similarity_pipeline_) != VK_SUCCESS) {
            std::cerr << "NovaPatternCatalog: Failed to create pattern similarity pipeline" << std::endl;
            return false;
        }
    }

    // Create compute pipeline for top-K selection
    pipeline_info.stage.module = topk_selection_shader_;

    if (device_ != VK_NULL_HANDLE) {
        if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &topk_selection_pipeline_) != VK_SUCCESS) {
            std::cerr << "NovaPatternCatalog: Failed to create top-K selection pipeline" << std::endl;
            return false;
        }
    }

    return true;
}

bool NovaPatternCatalog::createBuffers() {
    if (!nova_engine_) {
        return false;
    }

    NovaCore* core = nova_engine_->getCore();
    VmaAllocator allocator = core->getAllocator();

    // Calculate buffer sizes
    VkDeviceSize pattern_embeddings_size = config_.max_patterns * config_.embedding_dim * sizeof(float);
    VkDeviceSize similarity_scores_size = config_.max_patterns * sizeof(float);
    VkDeviceSize topk_scores_size = 256 * sizeof(float);
    VkDeviceSize topk_indices_size = 256 * sizeof(uint32_t);
    VkDeviceSize params_size = 256;  // Enough for params struct

    // Check total size against limit
    VkDeviceSize total_size = pattern_embeddings_size + similarity_scores_size +
                               topk_scores_size + topk_indices_size + params_size;

    if (total_size > static_cast<VkDeviceSize>(config_.max_gpu_memory_mb) * 1024 * 1024) {
        std::cerr << "NovaPatternCatalog: Requested buffers exceed max GPU memory" << std::endl;
        return false;
    }

    // Create pattern embeddings buffer (GPU storage + CPU upload)
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = pattern_embeddings_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                        &pattern_embeddings_buffer_, &pattern_embeddings_allocation_, nullptr) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create pattern embeddings buffer" << std::endl;
        return false;
    }

    // Create similarity scores buffer
    buffer_info.size = similarity_scores_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                        &similarity_scores_buffer_, &similarity_scores_allocation_, nullptr) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create similarity scores buffer" << std::endl;
        return false;
    }

    // Create top-K scores buffer
    buffer_info.size = topk_scores_size;
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;  // Need to read back results

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                        &topk_scores_buffer_, &topk_scores_allocation_, nullptr) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create top-K scores buffer" << std::endl;
        return false;
    }

    // Create top-K indices buffer
    buffer_info.size = topk_indices_size;

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                        &topk_indices_buffer_, &topk_indices_allocation_, nullptr) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create top-K indices buffer" << std::endl;
        return false;
    }

    // Create params uniform buffer
    buffer_info.size = params_size;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                        &params_buffer_, &params_buffer_allocation_, nullptr) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create params buffer" << std::endl;
        return false;
    }

    std::cout << "NovaPatternCatalog: Buffer allocation summary:" << std::endl;
    std::cout << "  - Pattern embeddings: " << (pattern_embeddings_size / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "  - Similarity scores: " << (similarity_scores_size / 1024.0) << " KB" << std::endl;
    std::cout << "  - Top-K buffers: " << ((topk_scores_size + topk_indices_size) / 1024.0) << " KB" << std::endl;
    std::cout << "  - Total: " << (total_size / 1024.0 / 1024.0) << " MB" << std::endl;

    return true;
}

bool NovaPatternCatalog::createDescriptorSets() {
    if (!nova_engine_ || device_ == VK_NULL_HANDLE) {
        return false;
    }

    // Create descriptor pool
    VkDescriptorPoolSize pool_sizes[2] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[0].descriptorCount = 4;  // 4 storage buffers
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[1].descriptorCount = 1;  // 1 uniform buffer

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 2;  // One for each pipeline

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        std::cerr << "NovaPatternCatalog: Failed to create descriptor pool" << std::endl;
        return false;
    }

    // Allocate descriptor sets would happen here in full implementation
    // VkDescriptorSetAllocateInfo alloc_info = {};
    // alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    // alloc_info.descriptorPool = descriptor_pool_;
    // alloc_info.descriptorSetCount = 1;
    // alloc_info.pSetLayouts = &descriptor_set_layout_;
    // vkAllocateDescriptorSets(device_, &alloc_info, &descriptor_set_);

    return true;
}

VkShaderModule NovaPatternCatalog::loadShader(const std::string& shader_path) {
    // Read SPIR-V file
    std::ifstream file(shader_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "NovaPatternCatalog: Failed to open shader file: " << shader_path << std::endl;
        return VK_NULL_HANDLE;
    }

    size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);
    file.seekg(0);
    file.read(buffer.data(), file_size);
    file.close();

    // Create shader module
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = buffer.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (device_ != VK_NULL_HANDLE) {
        if (vkCreateShaderModule(device_, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
            std::cerr << "NovaPatternCatalog: Failed to create shader module: " << shader_path << std::endl;
            return VK_NULL_HANDLE;
        }
    }

    return shader_module;
}

bool NovaPatternCatalog::runSimilarityCompute(
    const std::vector<float>& query_embeddings,
    uint32_t query_count
) {
    if (!nova_engine_ || device_ == VK_NULL_HANDLE) {
        return false;
    }

    // 1. Upload query embeddings to GPU (via staging buffer)
    // 2. Bind descriptor sets (query, catalog, output, params)
    // 3. Dispatch compute shader
    // 4. Wait for completion

    // Calculate work group count
    uint32_t total_comparisons = query_count * static_cast<uint32_t>(pattern_ids_.size());
    uint32_t work_groups = (total_comparisons + config_.work_group_size - 1) / config_.work_group_size;

    // In full implementation:
    // - Create command buffer
    // - vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pattern_similarity_pipeline_)
    // - vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, ...)
    // - vkCmdDispatch(cmd, work_groups, 1, 1)
    // - Submit and wait

    return true;
}

bool NovaPatternCatalog::runTopKSelection(
    uint32_t num_scores,
    uint32_t k,
    std::vector<float>& topk_scores,
    std::vector<uint32_t>& topk_indices
) {
    if (!nova_engine_ || device_ == VK_NULL_HANDLE) {
        return false;
    }

    // 1. Bind descriptor sets (scores, topk_scores, topk_indices, params)
    // 2. Dispatch compute shader (single work group for bitonic sort)
    // 3. Download results from GPU
    // 4. Wait for completion

    topk_scores.resize(k);
    topk_indices.resize(k);

    // In full implementation:
    // - Create command buffer
    // - vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, topk_selection_pipeline_)
    // - vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, ...)
    // - vkCmdDispatch(cmd, 1, 1, 1)  // Single work group
    // - Submit and wait
    // - vmaMapMemory to read results

    // Placeholder: Initialize with dummy data
    for (uint32_t i = 0; i < k; i++) {
        topk_scores[i] = 1.0f - (i * 0.01f);
        topk_indices[i] = i % num_scores;
    }

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

    metrics_.gpu_memory_usage_mb = getMemoryUsage();
    metrics_.gpu_utilization_percent = 0.0;  // Would query from GPU in full implementation
}

bool NovaPatternCatalog::validateEmbedding(const std::vector<float>& embedding) const {
    return embedding.size() == config_.embedding_dim;
}

void NovaPatternCatalog::cleanupResources() {
    if (!device_) {
        return;
    }

    // Wait for device to be idle
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    // Destroy pipelines
    if (pattern_similarity_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pattern_similarity_pipeline_, nullptr);
        pattern_similarity_pipeline_ = VK_NULL_HANDLE;
    }

    if (topk_selection_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, topk_selection_pipeline_, nullptr);
        topk_selection_pipeline_ = VK_NULL_HANDLE;
    }

    // Destroy shader modules
    if (pattern_similarity_shader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, pattern_similarity_shader_, nullptr);
        pattern_similarity_shader_ = VK_NULL_HANDLE;
    }

    if (topk_selection_shader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, topk_selection_shader_, nullptr);
        topk_selection_shader_ = VK_NULL_HANDLE;
    }

    // Destroy pipeline layout
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor pool
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    // Free VMA buffers
    if (nova_engine_) {
        NovaCore* core = nova_engine_->getCore();
        VmaAllocator allocator = core->getAllocator();

        if (pattern_embeddings_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, pattern_embeddings_buffer_, pattern_embeddings_allocation_);
            pattern_embeddings_buffer_ = VK_NULL_HANDLE;
            pattern_embeddings_allocation_ = VK_NULL_HANDLE;
        }

        if (similarity_scores_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, similarity_scores_buffer_, similarity_scores_allocation_);
            similarity_scores_buffer_ = VK_NULL_HANDLE;
            similarity_scores_allocation_ = VK_NULL_HANDLE;
        }

        if (topk_scores_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, topk_scores_buffer_, topk_scores_allocation_);
            topk_scores_buffer_ = VK_NULL_HANDLE;
            topk_scores_allocation_ = VK_NULL_HANDLE;
        }

        if (topk_indices_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, topk_indices_buffer_, topk_indices_allocation_);
            topk_indices_buffer_ = VK_NULL_HANDLE;
            topk_indices_allocation_ = VK_NULL_HANDLE;
        }

        if (params_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, params_buffer_, params_buffer_allocation_);
            params_buffer_ = VK_NULL_HANDLE;
            params_buffer_allocation_ = VK_NULL_HANDLE;
        }
    }

    // Destroy command pool
    if (compute_command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, compute_command_pool_, nullptr);
        compute_command_pool_ = VK_NULL_HANDLE;
    }
}
