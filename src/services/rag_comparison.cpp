/**
 * @file rag_comparison.cpp
 * @brief 新旧 RAG 后端并行对比实现。
 */

#include "services/rag_comparison.h"

#include <chrono>
#include <future>
#include <set>
#include <stdexcept>
#include <utility>

namespace interview {
namespace services {

namespace {

bool IsValidUtf8(const std::string& text) {
    int remaining = 0;
    for (const unsigned char byte : text) {
        if (remaining == 0) {
            if ((byte & 0x80) == 0) continue;
            if ((byte & 0xE0) == 0xC0) remaining = 1;
            else if ((byte & 0xF0) == 0xE0) remaining = 2;
            else if ((byte & 0xF8) == 0xF0) remaining = 3;
            else return false;
        } else {
            if ((byte & 0xC0) != 0x80) return false;
            --remaining;
        }
    }
    return remaining == 0;
}

bool CitationsAreValid(
    const std::vector<RagSource>& sources,
    bool knowledge_found) {
    if (knowledge_found && sources.empty()) return false;
    for (const auto& source : sources) {
        if (source.document_name.empty() || source.content.empty() ||
            source.score < 0.0f || source.score > 1.0f ||
            !IsValidUtf8(source.document_name) || !IsValidUtf8(source.content)) {
            return false;
        }
    }
    return true;
}

RagBackendSnapshot RunBackend(
    const std::shared_ptr<RagBackend>& backend,
    const std::string& question,
    const std::string& knowledge_base_id,
    int top_k,
    float similarity_threshold) {
    RagBackendSnapshot snapshot;
    if (!backend) {
        snapshot.backend_name = "unavailable";
        snapshot.search.error_message = "RAG backend is null";
        snapshot.query.error_message = "RAG backend is null";
        return snapshot;
    }
    snapshot.backend_name = backend->GetBackendName();

    const auto search_start = std::chrono::steady_clock::now();
    try {
        snapshot.search = backend->Search(
            question, knowledge_base_id, top_k, similarity_threshold);
    } catch (const std::exception& error) {
        snapshot.search.error_message = error.what();
    } catch (...) {
        snapshot.search.error_message = "Unknown search failure";
    }
    snapshot.search_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - search_start).count();

    const auto answer_start = std::chrono::steady_clock::now();
    try {
        backend->ResetConversation();
        snapshot.query = backend->Ask(
            question, knowledge_base_id, "general_assistant");
    } catch (const std::exception& error) {
        snapshot.query.error_message = error.what();
    } catch (...) {
        snapshot.query.error_message = "Unknown answer failure";
    }
    snapshot.answer_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - answer_start).count();

    snapshot.utf8_valid = IsValidUtf8(question) &&
        IsValidUtf8(snapshot.query.answer) &&
        IsValidUtf8(snapshot.query.error_message);
    snapshot.citations_valid = CitationsAreValid(
        snapshot.search.sources, snapshot.search.knowledge_found) &&
        CitationsAreValid(
            snapshot.query.sources, snapshot.query.knowledge_found);
    return snapshot;
}

float CitationNameJaccard(
    const std::vector<RagSource>& first,
    const std::vector<RagSource>& second) {
    std::set<std::string> first_names;
    std::set<std::string> second_names;
    for (const auto& source : first) first_names.insert(source.document_name);
    for (const auto& source : second) second_names.insert(source.document_name);
    if (first_names.empty() && second_names.empty()) return 1.0f;

    std::size_t intersection = 0;
    for (const auto& name : first_names) {
        if (second_names.count(name) > 0) ++intersection;
    }
    const std::size_t union_size =
        first_names.size() + second_names.size() - intersection;
    return union_size == 0 ? 1.0f
        : static_cast<float>(intersection) / static_cast<float>(union_size);
}

} // namespace

RagComparisonRunner::RagComparisonRunner(
    std::shared_ptr<RagBackend> local_backend,
    std::shared_ptr<RagBackend> remote_backend)
    : local_backend_(std::move(local_backend)),
      remote_backend_(std::move(remote_backend)) {}

RagComparisonReport RagComparisonRunner::Compare(
    const std::string& question,
    const std::string& local_knowledge_base_id,
    const std::string& remote_knowledge_base_id,
    int top_k,
    float similarity_threshold) {
    auto local_future = std::async(std::launch::async, RunBackend,
        local_backend_, question, local_knowledge_base_id,
        top_k, similarity_threshold);
    auto remote_future = std::async(std::launch::async, RunBackend,
        remote_backend_, question, remote_knowledge_base_id,
        top_k, similarity_threshold);

    RagComparisonReport report;
    report.local = local_future.get();
    report.remote = remote_future.get();
    report.knowledge_found_matches =
        report.local.search.knowledge_found == report.remote.search.knowledge_found;
    report.citation_name_jaccard = CitationNameJaccard(
        report.local.search.sources, report.remote.search.sources);
    return report;
}

} // namespace services
} // namespace interview
