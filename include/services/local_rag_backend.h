/**
 * @file local_rag_backend.h
 * @brief 第八步并行验证使用的旧本地 RAG 适配器。
 */

#pragma once

#include "services/rag_client.h"

#include <memory>

namespace interview {
namespace services {

class DocumentLoader;
class EmbeddingClient;
class LLMClient;
class RAGPromptBuilder;
class VectorStore;

class LocalRagBackend final : public RagBackend {
public:
    LocalRagBackend();
    ~LocalRagBackend() override;

    bool Initialize(
        std::shared_ptr<EmbeddingClient> embedding_client,
        std::shared_ptr<VectorStore> vector_store,
        std::shared_ptr<DocumentLoader> document_loader,
        std::shared_ptr<RAGPromptBuilder> prompt_builder,
        std::shared_ptr<LLMClient> llm_client);

    std::string GetBackendName() const override;
    RagQueryResponse Ask(const std::string& question,
                         const std::string& knowledge_base_id,
                         const std::string& role_type = {}) override;
    RagKnowledgeBaseListResponse ListKnowledgeBases() override;
    RagKnowledgeBaseResponse CreateKnowledgeBase(
        const std::string& name,
        const std::string& description,
        const std::string& role_type = "interviewer") override;
    RagOperationResponse DeleteKnowledgeBase(
        const std::string& knowledge_base_id) override;
    RagDocumentListResponse ListDocuments(
        const std::string& knowledge_base_id) override;
    RagDocumentResponse UploadDocument(
        const std::string& knowledge_base_id,
        const std::string& file_path,
        const std::string& display_name = {}) override;
    RagOperationResponse DeleteDocument(
        const std::string& knowledge_base_id,
        const std::string& document_id) override;
    RagSearchResponse Search(
        const std::string& question,
        const std::string& knowledge_base_id,
        int top_k = 3,
        float similarity_threshold = 0.7f) override;
    void ResetConversation() override;
    std::string GetConversationID() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
