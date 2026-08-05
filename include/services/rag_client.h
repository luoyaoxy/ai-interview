/**
 * @file rag_client.h
 * @brief 独立 RAG 服务的高级 HTTP 客户端。
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace services {

/** @brief RAG 回答引用的知识来源。 */
struct RagSource {
    std::string document_id;
    std::string document_name;
    std::string chunk_id;
    std::string content;
    float score = 0.0f;
    int page = 0;  ///< 页码未知时为 0。
};

/** @brief 一次 RAG 查询的结果；调用失败时查看 error_message。 */
struct RagQueryResponse {
    std::string answer;
    std::string error_message;
    std::string conversation_id;
    std::vector<RagSource> sources;
    bool knowledge_found = false;
    bool success = false;
};

struct RagKnowledgeBase {
    std::string id;
    std::string name;
    std::string description;
    std::string role_type;
    int document_count = 0;
    int chunk_count = 0;
};

struct RagDocument {
    std::string id;
    std::string knowledge_base_id;
    std::string file_name;
    std::string status;
    std::string error_message;
    int chunk_count = 0;
};

struct RagOperationResponse {
    std::string error_message;
    bool success = false;
};

struct RagKnowledgeBaseResponse {
    RagKnowledgeBase knowledge_base;
    std::string error_message;
    bool success = false;
};

struct RagKnowledgeBaseListResponse {
    std::string error_message;
    std::vector<RagKnowledgeBase> items;
    bool success = false;
};

struct RagDocumentResponse {
    RagDocument document;
    std::string error_message;
    bool success = false;
};

struct RagDocumentListResponse {
    std::string error_message;
    std::vector<RagDocument> items;
    bool success = false;
};

struct RagSearchResponse {
    std::string error_message;
    std::vector<RagSource> sources;
    bool knowledge_found = false;
    bool success = false;
};

/** @brief Qt 层使用的统一 RAG 后端接口，供本地与远程实现并行验证。 */
class RagBackend {
public:
    virtual ~RagBackend() = default;
    virtual std::string GetBackendName() const = 0;

    virtual RagQueryResponse Ask(
        const std::string& question,
        const std::string& knowledge_base_id,
        const std::string& role_type = {}) = 0;
    virtual RagKnowledgeBaseListResponse ListKnowledgeBases() = 0;
    virtual RagKnowledgeBaseResponse CreateKnowledgeBase(
        const std::string& name,
        const std::string& description,
        const std::string& role_type = "interviewer") = 0;
    virtual RagOperationResponse DeleteKnowledgeBase(
        const std::string& knowledge_base_id) = 0;
    virtual RagDocumentListResponse ListDocuments(
        const std::string& knowledge_base_id) = 0;
    virtual RagDocumentResponse UploadDocument(
        const std::string& knowledge_base_id,
        const std::string& file_path,
        const std::string& display_name = {}) = 0;
    virtual RagOperationResponse DeleteDocument(
        const std::string& knowledge_base_id,
        const std::string& document_id) = 0;
    virtual RagSearchResponse Search(
        const std::string& question,
        const std::string& knowledge_base_id,
        int top_k = 3,
        float similarity_threshold = 0.7f) = 0;
    virtual void ResetConversation() = 0;
    virtual std::string GetConversationID() const = 0;
};

/**
 * @brief 通过 HTTP 调用独立 RAG 服务。
 *
 * 客户端保存服务端返回的 conversation_id，后续 Ask 调用会自动复用它。
 * 同一个客户端实例的查询会串行执行，避免同一会话中的消息乱序。
 */
class RagClient final : public RagBackend {
public:
    RagClient();
    ~RagClient();

    std::string GetBackendName() const override;

    /** @brief 配置远程服务；service_url 示例：http://127.0.0.1:8000。 */
    bool Initialize(const std::string& service_url,
                    const std::string& api_key,
                    long timeout_seconds = 60,
                    bool verify_ssl = true,
                    int max_retries = 2,
                    long retry_delay_ms = 500);

    /** @brief 向指定知识库提问。该方法不会抛出网络或 JSON 解析异常。 */
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

    /** @brief 只清除客户端保存的会话 ID；不会删除服务端会话。 */
    void ResetConversation() override;

    /** @brief 获取当前服务端会话 ID，尚未查询时为空。 */
    std::string GetConversationID() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
