/**
 * @file rag_compare.cpp
 * @brief 使用同一问题并行验证旧本地 RAG 与独立 RAG 服务。
 */

#include "common/config.h"
#include "common/logger.h"
#include "services/document_loader.h"
#include "services/embedding_client.h"
#include "services/llm_client.h"
#include "services/local_rag_backend.h"
#include "services/rag_client.h"
#include "services/rag_comparison.h"
#include "services/rag_prompt_builder.h"
#include "services/vector_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using interview::common::Config;
using interview::services::DocumentLoader;
using interview::services::EmbeddingClient;
using interview::services::LLMClient;
using interview::services::LocalRagBackend;
using interview::services::RagBackend;
using interview::services::RagBackendSnapshot;
using interview::services::RagClient;
using interview::services::RagComparisonReport;
using interview::services::RagComparisonRunner;
using interview::services::RAGPromptBuilder;
using interview::services::RagSource;
using interview::services::VectorStore;
using nlohmann::json;

json SourceToJson(const RagSource& source) {
    return {
        {"document_id", source.document_id},
        {"document_name", source.document_name},
        {"chunk_id", source.chunk_id},
        {"content", source.content},
        {"score", source.score},
        {"page", source.page}
    };
}

json SnapshotToJson(const RagBackendSnapshot& snapshot) {
    json search_sources = json::array();
    for (const auto& source : snapshot.search.sources) {
        search_sources.push_back(SourceToJson(source));
    }
    json answer_sources = json::array();
    for (const auto& source : snapshot.query.sources) {
        answer_sources.push_back(SourceToJson(source));
    }
    return {
        {"backend", snapshot.backend_name},
        {"search", {
            {"success", snapshot.search.success},
            {"knowledge_found", snapshot.search.knowledge_found},
            {"error_message", snapshot.search.error_message},
            {"duration_ms", snapshot.search_duration_ms},
            {"sources", std::move(search_sources)}
        }},
        {"answer", {
            {"success", snapshot.query.success},
            {"knowledge_found", snapshot.query.knowledge_found},
            {"error_message", snapshot.query.error_message},
            {"duration_ms", snapshot.answer_duration_ms},
            {"text", snapshot.query.answer},
            {"sources", std::move(answer_sources)}
        }},
        {"utf8_valid", snapshot.utf8_valid},
        {"citations_valid", snapshot.citations_valid}
    };
}

std::shared_ptr<RagBackend> CreateLocalBackend() {
    const auto& config = Config::Instance();
    const auto& rag = config.GetRAGConfig();

    auto embedding = std::make_shared<EmbeddingClient>();
    const std::string embedding_api_key =
        rag.embedding_provider == "ollama"
            ? std::string{}
            : config.llm_config.api_key;
    if (!embedding->Initialize(
            rag.embedding_api_url, embedding_api_key, rag.embedding_model)) {
        throw std::runtime_error("Failed to initialize local Embedding client");
    }

    const auto database_path = std::filesystem::u8path(rag.vector_db_path);
    if (database_path.has_parent_path()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    auto store = std::make_shared<VectorStore>();
    if (!store->Open(rag.vector_db_path)) {
        throw std::runtime_error("Failed to open local vector database");
    }

    auto loader = std::make_shared<DocumentLoader>();
    loader->SetChunkConfig({rag.chunk_size, rag.chunk_overlap, true});
    auto prompt_builder = std::make_shared<RAGPromptBuilder>();
    prompt_builder->Initialize(embedding, store);
    prompt_builder->SetSimilarityThreshold(rag.similarity_threshold);
    auto llm = std::make_shared<LLMClient>();
    llm->SetMaxHistoryTurns(rag.max_history_turns);

    auto backend = std::make_shared<LocalRagBackend>();
    if (!backend->Initialize(
            embedding, store, loader, prompt_builder, llm)) {
        throw std::runtime_error("Failed to initialize local RAG backend");
    }
    return backend;
}

std::shared_ptr<RagBackend> CreateRemoteBackend() {
    const auto& rag = Config::Instance().GetRAGConfig();
    auto backend = std::make_shared<RagClient>();
    if (!backend->Initialize(
            rag.service_url,
            rag.api_key,
            rag.timeout_seconds,
            rag.verify_ssl,
            rag.max_retries,
            rag.retry_delay_ms)) {
        throw std::runtime_error("Failed to initialize remote RAG client");
    }
    return backend;
}

void WriteJson(const json& value, const std::string& output_path) {
    const std::string text = value.dump(2, ' ', false,
        json::error_handler_t::replace);
    if (output_path.empty()) {
        std::cout << text << '\n';
        return;
    }
    std::ofstream output(std::filesystem::u8path(output_path),
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + output_path);
    }
    output << text << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 2) {
        std::cerr << "Usage: RagComparisonTool <comparison-input.json>\n";
        return 2;
    }

    try {
        std::ifstream input(std::filesystem::u8path(argv[1]), std::ios::binary);
        if (!input) {
            throw std::runtime_error("Failed to open comparison input file");
        }
        json request;
        input >> request;

        const std::string config_path = request.value(
            "config_path", "config/default_config.json");
        Config::Instance().LoadFromFile(config_path);
        interview::common::Logger::Init("rag_comparison.log", false);

        const std::string question = request.at("question").get<std::string>();
        const std::string local_kb_id =
            request.at("local_knowledge_base_id").get<std::string>();
        const std::string remote_kb_id =
            request.at("remote_knowledge_base_id").get<std::string>();
        const auto& rag = Config::Instance().GetRAGConfig();
        const int top_k = request.value("top_k", rag.top_k);
        const float threshold = request.value(
            "similarity_threshold", rag.similarity_threshold);

        auto local = CreateLocalBackend();
        auto remote = CreateRemoteBackend();
        json report_json;

        const std::string document_path = request.value(
            "document_path", std::string{});
        if (!document_path.empty()) {
            const std::string display_name = request.value(
                "document_name",
                std::filesystem::u8path(document_path).filename().u8string());
            const auto local_upload = local->UploadDocument(
                local_kb_id, document_path, display_name);
            const auto remote_upload = remote->UploadDocument(
                remote_kb_id, document_path, display_name);
            report_json["document_upload"] = {
                {"local", {
                    {"success", local_upload.success},
                    {"error_message", local_upload.error_message},
                    {"document_id", local_upload.document.id}
                }},
                {"remote", {
                    {"success", remote_upload.success},
                    {"error_message", remote_upload.error_message},
                    {"document_id", remote_upload.document.id}
                }}
            };
        }

        RagComparisonRunner runner(local, remote);
        const RagComparisonReport report = runner.Compare(
            question, local_kb_id, remote_kb_id, top_k, threshold);
        report_json["question"] = question;
        report_json["local_knowledge_base_id"] = local_kb_id;
        report_json["remote_knowledge_base_id"] = remote_kb_id;
        report_json["local"] = SnapshotToJson(report.local);
        report_json["remote"] = SnapshotToJson(report.remote);
        report_json["comparison"] = {
            {"knowledge_found_matches", report.knowledge_found_matches},
            {"citation_name_jaccard", report.citation_name_jaccard},
            {"manual_answer_quality_review_required", true}
        };

        WriteJson(report_json, request.value("output_path", std::string{}));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RAG comparison failed: " << error.what() << '\n';
        return 1;
    }
}
