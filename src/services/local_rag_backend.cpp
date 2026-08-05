/**
 * @file local_rag_backend.cpp
 * @brief 将旧本地 RAG 调用链适配为统一 RagBackend 接口。
 */

#include "services/local_rag_backend.h"

#include "common/config.h"
#include "services/document_loader.h"
#include "services/embedding_client.h"
#include "services/llm_client.h"
#include "services/rag_prompt_builder.h"
#include "services/vector_store.h"

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace interview {
namespace services {

namespace {

bool ParseLocalKnowledgeBaseID(const std::string& value, int& result) {
    try {
        std::size_t parsed = 0;
        result = std::stoi(value, &parsed);
        return parsed == value.size() && result > 0;
    } catch (const std::exception&) {
        return false;
    }
}

RagSource ToRagSource(const SearchResult& result) {
    RagSource source;
    source.document_id = result.chunk.chunk_id;
    source.chunk_id = result.chunk.chunk_id;
    source.content = result.chunk.content;
    source.score = result.similarity;
    source.document_name = "未知来源";
    try {
        const auto metadata = nlohmann::json::parse(result.chunk.metadata);
        source.document_name = metadata.value("source", source.document_name);
        source.page = metadata.value("page", 0);
    } catch (const nlohmann::json::exception&) {
    }
    return source;
}

} // namespace

struct LocalRagBackend::Impl {
    std::shared_ptr<EmbeddingClient> embedding_client;
    std::shared_ptr<VectorStore> vector_store;
    std::shared_ptr<DocumentLoader> document_loader;
    std::shared_ptr<RAGPromptBuilder> prompt_builder;
    std::shared_ptr<LLMClient> llm_client;
    std::unordered_map<int, std::vector<RagDocument>> documents;
    std::string conversation_id;
    int next_document_id = 1;
    bool initialized = false;
    mutable std::mutex mutex;
};

LocalRagBackend::LocalRagBackend()
    : impl_(std::make_unique<Impl>()) {}

LocalRagBackend::~LocalRagBackend() = default;

bool LocalRagBackend::Initialize(
    std::shared_ptr<EmbeddingClient> embedding_client,
    std::shared_ptr<VectorStore> vector_store,
    std::shared_ptr<DocumentLoader> document_loader,
    std::shared_ptr<RAGPromptBuilder> prompt_builder,
    std::shared_ptr<LLMClient> llm_client) {
    if (!embedding_client || !vector_store || !document_loader ||
        !prompt_builder || !llm_client) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->embedding_client = std::move(embedding_client);
    impl_->vector_store = std::move(vector_store);
    impl_->document_loader = std::move(document_loader);
    impl_->prompt_builder = std::move(prompt_builder);
    impl_->llm_client = std::move(llm_client);
    impl_->initialized = true;
    return true;
}

std::string LocalRagBackend::GetBackendName() const {
    return "local";
}

RagQueryResponse LocalRagBackend::Ask(
    const std::string& question,
    const std::string& knowledge_base_id,
    const std::string& role_type) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagQueryResponse response;
    int kb_id = 0;
    if (!impl_->initialized) {
        response.error_message = "Local RAG backend is not initialized";
        return response;
    }
    if (!ParseLocalKnowledgeBaseID(knowledge_base_id, kb_id)) {
        response.error_message = "Invalid local knowledge base ID";
        return response;
    }

    impl_->vector_store->SetActiveKnowledgeBaseID(kb_id);
    impl_->prompt_builder->SetSystemRole(
        role_type == "interviewer" ? kInterviewerRole : kGeneralAssistantRole);
    const auto& rag = common::Config::Instance().GetRAGConfig();
    impl_->prompt_builder->SetSimilarityThreshold(rag.similarity_threshold);
    const auto prompt = impl_->prompt_builder->BuildPrompt(question, rag.top_k);
    response.knowledge_found = prompt.knowledge_found;
    for (const auto& source : prompt.sources) {
        response.sources.push_back(ToRagSource(source));
    }

    if (prompt.knowledge_found) {
        try {
            response.answer = impl_->llm_client->ChatWithRAG(
                prompt.system_prompt, prompt.user_prompt);
        } catch (const std::exception& error) {
            response.error_message = error.what();
            return response;
        }
    } else {
        response.answer = impl_->prompt_builder->GetSystemRole().fallback_response;
    }
    impl_->conversation_id = "local-" + knowledge_base_id;
    response.conversation_id = impl_->conversation_id;
    response.success = true;
    return response;
}

RagKnowledgeBaseListResponse LocalRagBackend::ListKnowledgeBases() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagKnowledgeBaseListResponse response;
    if (!impl_->initialized) {
        response.error_message = "Local RAG backend is not initialized";
        return response;
    }
    for (const auto& local : impl_->vector_store->ListKnowledgeBases()) {
        RagKnowledgeBase item;
        item.id = std::to_string(local.id);
        item.name = local.name;
        item.description = local.description;
        item.role_type = local.role_type;
        item.chunk_count = local.chunk_count;
        item.document_count = static_cast<int>(impl_->documents[local.id].size());
        response.items.push_back(std::move(item));
    }
    response.success = true;
    return response;
}

RagKnowledgeBaseResponse LocalRagBackend::CreateKnowledgeBase(
    const std::string& name,
    const std::string& description,
    const std::string& role_type) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagKnowledgeBaseResponse response;
    if (!impl_->initialized) {
        response.error_message = "Local RAG backend is not initialized";
        return response;
    }
    const int id = impl_->vector_store->CreateKnowledgeBase(
        name, description, role_type);
    if (id <= 0) {
        response.error_message = "Failed to create local knowledge base";
        return response;
    }
    response.knowledge_base.id = std::to_string(id);
    response.knowledge_base.name = name;
    response.knowledge_base.description = description;
    response.knowledge_base.role_type = role_type;
    response.success = true;
    return response;
}

RagOperationResponse LocalRagBackend::DeleteKnowledgeBase(
    const std::string& knowledge_base_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagOperationResponse response;
    int kb_id = 0;
    if (!impl_->initialized ||
        !ParseLocalKnowledgeBaseID(knowledge_base_id, kb_id)) {
        response.error_message = "Invalid local knowledge base ID";
        return response;
    }
    response.success = impl_->vector_store->DeleteKnowledgeBase(kb_id);
    if (response.success) {
        impl_->documents.erase(kb_id);
    } else {
        response.error_message = "Failed to delete local knowledge base";
    }
    return response;
}

RagDocumentListResponse LocalRagBackend::ListDocuments(
    const std::string& knowledge_base_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagDocumentListResponse response;
    int kb_id = 0;
    if (!impl_->initialized ||
        !ParseLocalKnowledgeBaseID(knowledge_base_id, kb_id)) {
        response.error_message = "Invalid local knowledge base ID";
        return response;
    }
    response.items = impl_->documents[kb_id];
    response.success = true;
    return response;
}

RagDocumentResponse LocalRagBackend::UploadDocument(
    const std::string& knowledge_base_id,
    const std::string& file_path,
    const std::string& display_name) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagDocumentResponse response;
    int kb_id = 0;
    if (!impl_->initialized ||
        !ParseLocalKnowledgeBaseID(knowledge_base_id, kb_id)) {
        response.error_message = "Invalid local knowledge base ID";
        return response;
    }
    try {
        auto chunks = impl_->document_loader->LoadDocument(file_path);
        if (chunks.empty()) {
            response.error_message = "No content was extracted from the document";
            return response;
        }
        std::vector<std::string> texts;
        texts.reserve(chunks.size());
        for (const auto& chunk : chunks) texts.push_back(chunk.content);
        const auto embeddings = impl_->embedding_client->EmbedBatch(texts);
        std::vector<DocumentChunk> valid_chunks;
        for (std::size_t index = 0;
             index < chunks.size() && index < embeddings.size(); ++index) {
            if (embeddings[index].success) {
                chunks[index].embedding = embeddings[index].embedding;
                valid_chunks.push_back(std::move(chunks[index]));
            }
        }
        const int written = impl_->vector_store->InsertChunks(kb_id, valid_chunks);
        if (written <= 0) {
            response.error_message = "Failed to write local document vectors";
            return response;
        }

        RagDocument document;
        document.id = "local-document-" +
                      std::to_string(impl_->next_document_id++);
        document.knowledge_base_id = knowledge_base_id;
        document.file_name = display_name.empty()
            ? std::filesystem::u8path(file_path).filename().u8string()
            : display_name;
        document.status = "ready";
        document.chunk_count = written;
        impl_->documents[kb_id].push_back(document);
        response.document = std::move(document);
        response.success = true;
    } catch (const std::exception& error) {
        response.error_message = error.what();
    }
    return response;
}

RagOperationResponse LocalRagBackend::DeleteDocument(
    const std::string&,
    const std::string&) {
    RagOperationResponse response;
    response.error_message =
        "旧本地 VectorStore 不支持按文档删除；可删除整个知识库后重建";
    return response;
}

RagSearchResponse LocalRagBackend::Search(
    const std::string& question,
    const std::string& knowledge_base_id,
    int top_k,
    float similarity_threshold) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagSearchResponse response;
    int kb_id = 0;
    if (!impl_->initialized ||
        !ParseLocalKnowledgeBaseID(knowledge_base_id, kb_id)) {
        response.error_message = "Invalid local knowledge base ID";
        return response;
    }
    impl_->vector_store->SetActiveKnowledgeBaseID(kb_id);
    impl_->prompt_builder->SetSimilarityThreshold(similarity_threshold);
    const auto prompt = impl_->prompt_builder->BuildPrompt(question, top_k);
    response.knowledge_found = prompt.knowledge_found;
    for (const auto& source : prompt.sources) {
        response.sources.push_back(ToRagSource(source));
    }
    response.success = true;
    return response;
}

void LocalRagBackend::ResetConversation() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->llm_client) impl_->llm_client->ClearConversationHistory();
    impl_->conversation_id.clear();
}

std::string LocalRagBackend::GetConversationID() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->conversation_id;
}

} // namespace services
} // namespace interview
