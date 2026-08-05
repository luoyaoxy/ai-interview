/**
 * @file rag_comparison.h
 * @brief 新旧 RAG 后端同题并行对比。
 */

#pragma once

#include "services/rag_client.h"

#include <memory>

namespace interview {
namespace services {

struct RagBackendSnapshot {
    std::string backend_name;
    RagSearchResponse search;
    RagQueryResponse query;
    long long search_duration_ms = 0;
    long long answer_duration_ms = 0;
    bool utf8_valid = false;
    bool citations_valid = false;
};

struct RagComparisonReport {
    RagBackendSnapshot local;
    RagBackendSnapshot remote;
    float citation_name_jaccard = 0.0f;
    bool knowledge_found_matches = false;
};

class RagComparisonRunner final {
public:
    RagComparisonRunner(
        std::shared_ptr<RagBackend> local_backend,
        std::shared_ptr<RagBackend> remote_backend);

    RagComparisonReport Compare(
        const std::string& question,
        const std::string& local_knowledge_base_id,
        const std::string& remote_knowledge_base_id,
        int top_k,
        float similarity_threshold);

private:
    std::shared_ptr<RagBackend> local_backend_;
    std::shared_ptr<RagBackend> remote_backend_;
};

} // namespace services
} // namespace interview
