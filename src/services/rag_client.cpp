/**
 * @file rag_client.cpp
 * @brief 独立 RAG 服务客户端实现。
 */

#include "services/rag_client.h"

#include "common/http_client.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace interview {
namespace services {

namespace {

RagQueryResponse MakeErrorResponse(std::string message) {
    RagQueryResponse response;
    response.error_message = std::move(message);
    return response;
}

std::string ParseServiceError(const common::HttpResponse& http_response) {
    try {
        const auto body = nlohmann::json::parse(http_response.body);
        if (body.contains("error") && body["error"].is_object()) {
            const auto& error = body["error"];
            if (error.contains("message") && error["message"].is_string()) {
                return error["message"].get<std::string>();
            }
        }
    } catch (const nlohmann::json::exception&) {
        // 非 JSON 错误页由下面的通用信息处理。
    }

    std::string message = "RAG service returned HTTP " +
                          std::to_string(http_response.status_code);
    if (!http_response.body.empty()) {
        message += ": " + http_response.body;
    }
    return message;
}

std::string ParseRequestError(const common::HttpResponse& response,
                              const std::string& service_url) {
    if (!response.TransportSucceeded()) {
        return "Cannot connect to RAG service at " + service_url +
               ": " + response.error_message;
    }
    if (!response.IsSuccess()) {
        return ParseServiceError(response);
    }
    return {};
}

RagSource ParseSource(const nlohmann::json& item) {
    RagSource source;
    source.document_id = item.at("document_id").get<std::string>();
    source.document_name = item.at("document_name").get<std::string>();
    source.chunk_id = item.at("chunk_id").get<std::string>();
    source.content = item.at("content").get<std::string>();
    source.score = item.at("score").get<float>();
    if (item.contains("page") && !item["page"].is_null()) {
        source.page = item["page"].get<int>();
    }
    return source;
}

RagKnowledgeBase ParseKnowledgeBase(const nlohmann::json& item) {
    RagKnowledgeBase knowledge_base;
    knowledge_base.id = item.at("id").get<std::string>();
    knowledge_base.name = item.at("name").get<std::string>();
    knowledge_base.description = item.at("description").get<std::string>();
    knowledge_base.role_type = item.at("role_type").get<std::string>();
    knowledge_base.document_count = item.at("document_count").get<int>();
    knowledge_base.chunk_count = item.at("chunk_count").get<int>();
    return knowledge_base;
}

RagDocument ParseDocument(const nlohmann::json& item) {
    RagDocument document;
    document.id = item.at("id").get<std::string>();
    document.knowledge_base_id =
        item.at("knowledge_base_id").get<std::string>();
    document.file_name = item.at("file_name").get<std::string>();
    document.status = item.at("status").get<std::string>();
    document.error_message = item.value("error_message", std::string{});
    document.chunk_count = item.at("chunk_count").get<int>();
    return document;
}

std::string UrlEncode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[character >> 4]);
            encoded.push_back(kHex[character & 0x0F]);
        }
    }
    return encoded;
}

} // namespace

struct RagClient::Impl {
    std::string service_url;
    std::string api_key;
    std::string conversation_id;
    long timeout_seconds = 60;
    long retry_delay_ms = 500;
    int max_retries = 2;
    bool verify_ssl = true;
    bool initialized = false;
    mutable std::mutex mutex;

    std::vector<std::string> Headers(bool json_body = false) const {
        std::vector<std::string> headers = {"Accept: application/json"};
        if (json_body) {
            headers.push_back("Content-Type: application/json; charset=utf-8");
        }
        if (!api_key.empty()) {
            headers.push_back("Authorization: Bearer " + api_key);
        }
        return headers;
    }

    common::HttpRequestOptions Options() const {
        common::HttpRequestOptions options;
        options.timeout_seconds = timeout_seconds;
        options.verify_ssl = verify_ssl;
        return options;
    }

    template <typename Request>
    common::HttpResponse ExecuteWithRetry(Request request) const {
        common::HttpResponse response;
        for (int attempt = 0; attempt <= max_retries; ++attempt) {
            response = request();
            const bool retryable = !response.TransportSucceeded() ||
                response.status_code == 429 || response.status_code == 502 ||
                response.status_code == 503 || response.status_code == 504;
            if (!retryable || attempt == max_retries) {
                return response;
            }
            const long delay = retry_delay_ms * (1L << attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        return response;
    }
};

RagClient::RagClient()
    : impl_(std::make_unique<Impl>()) {}

RagClient::~RagClient() = default;

std::string RagClient::GetBackendName() const {
    return "remote";
}

bool RagClient::Initialize(const std::string& service_url,
                           const std::string& api_key,
                           long timeout_seconds,
                           bool verify_ssl,
                           int max_retries,
                           long retry_delay_ms) {
    if (service_url.empty()) {
        return false;
    }
    if (timeout_seconds <= 0) {
        return false;
    }
    if (max_retries < 0 || max_retries > 5 || retry_delay_ms < 0) {
        return false;
    }

    std::string normalized_url = service_url;
    while (!normalized_url.empty() && normalized_url.back() == '/') {
        normalized_url.pop_back();
    }
    if (normalized_url.empty() ||
        (normalized_url.rfind("http://", 0) != 0 &&
         normalized_url.rfind("https://", 0) != 0)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->service_url = std::move(normalized_url);
    impl_->api_key = api_key;
    impl_->timeout_seconds = timeout_seconds;
    impl_->max_retries = max_retries;
    impl_->retry_delay_ms = retry_delay_ms;
    impl_->verify_ssl = verify_ssl;
    impl_->conversation_id.clear();
    impl_->initialized = true;
    return true;
}

RagQueryResponse RagClient::Ask(const std::string& question,
                                const std::string& knowledge_base_id,
                                const std::string& role_type) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (!impl_->initialized) {
        return MakeErrorResponse("RagClient is not initialized");
    }
    if (question.empty()) {
        return MakeErrorResponse("RAG question cannot be empty");
    }
    if (knowledge_base_id.empty()) {
        return MakeErrorResponse("RAG knowledge_base_id cannot be empty");
    }

    nlohmann::json request = {
        {"question", question},
        {"knowledge_base_id", knowledge_base_id}
    };
    if (!impl_->conversation_id.empty()) {
        request["conversation_id"] = impl_->conversation_id;
    }
    if (!role_type.empty()) {
        request["role_type"] = role_type;
    }

    const auto http_response = common::HttpClient::Post(
        impl_->service_url + "/api/v1/rag/query",
        request.dump(),
        impl_->Headers(true),
        impl_->Options());

    const std::string request_error =
        ParseRequestError(http_response, impl_->service_url);
    if (!request_error.empty()) {
        return MakeErrorResponse(request_error);
    }

    try {
        const auto body = nlohmann::json::parse(http_response.body);

        RagQueryResponse response;
        response.answer = body.at("answer").get<std::string>();
        response.conversation_id = body.at("conversation_id").get<std::string>();
        response.knowledge_found = body.at("knowledge_found").get<bool>();

        const auto& sources = body.at("sources");
        if (!sources.is_array()) {
            return MakeErrorResponse("Invalid RAG response: sources must be an array");
        }
        response.sources.reserve(sources.size());
        for (const auto& item : sources) {
            response.sources.push_back(ParseSource(item));
        }

        impl_->conversation_id = response.conversation_id;
        response.success = true;
        return response;
    } catch (const nlohmann::json::exception& error) {
        return MakeErrorResponse(
            std::string("Invalid RAG service response: ") + error.what());
    }
}

RagKnowledgeBaseListResponse RagClient::ListKnowledgeBases() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagKnowledgeBaseListResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }

    try {
        std::string cursor;
        do {
            std::string url = impl_->service_url +
                "/api/v1/knowledge-bases?page_size=100";
            if (!cursor.empty()) url += "&cursor=" + UrlEncode(cursor);
            const auto response = impl_->ExecuteWithRetry([this, &url]() {
                return common::HttpClient::Get(
                    url, impl_->Headers(), impl_->Options());
            });
            result.error_message = ParseRequestError(response, impl_->service_url);
            if (!result.error_message.empty()) return result;
            const auto body = nlohmann::json::parse(response.body);
            const auto& items = body.at("items");
            if (!items.is_array()) {
                result.error_message = "Invalid RAG response: items must be an array";
                return result;
            }
            for (const auto& item : items) {
                result.items.push_back(ParseKnowledgeBase(item));
            }
            const std::string previous_cursor = cursor;
            cursor = body.contains("next_cursor") && !body["next_cursor"].is_null()
                ? body["next_cursor"].get<std::string>() : std::string{};
            if (!cursor.empty() && cursor == previous_cursor) {
                result.error_message = "Invalid RAG response: pagination cursor did not advance";
                return result;
            }
        } while (!cursor.empty());
        result.success = true;
    } catch (const nlohmann::json::exception& error) {
        result.error_message =
            std::string("Invalid RAG service response: ") + error.what();
    }
    return result;
}

RagKnowledgeBaseResponse RagClient::CreateKnowledgeBase(
    const std::string& name,
    const std::string& description,
    const std::string& role_type) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagKnowledgeBaseResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    if (name.empty()) {
        result.error_message = "Knowledge base name cannot be empty";
        return result;
    }

    const nlohmann::json request = {
        {"name", name},
        {"description", description},
        {"role_type", role_type}
    };
    const auto response = common::HttpClient::Post(
        impl_->service_url + "/api/v1/knowledge-bases",
        request.dump(), impl_->Headers(true), impl_->Options());
    result.error_message = ParseRequestError(response, impl_->service_url);
    if (!result.error_message.empty()) {
        return result;
    }
    try {
        const auto body = nlohmann::json::parse(response.body);
        result.knowledge_base = ParseKnowledgeBase(body.at("knowledge_base"));
        result.success = true;
    } catch (const nlohmann::json::exception& error) {
        result.error_message =
            std::string("Invalid RAG service response: ") + error.what();
    }
    return result;
}

RagOperationResponse RagClient::DeleteKnowledgeBase(
    const std::string& knowledge_base_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagOperationResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    const auto response = common::HttpClient::Delete(
        impl_->service_url + "/api/v1/knowledge-bases/" + knowledge_base_id,
        impl_->Headers(), impl_->Options());
    result.error_message = ParseRequestError(response, impl_->service_url);
    result.success = result.error_message.empty();
    return result;
}

RagDocumentListResponse RagClient::ListDocuments(
    const std::string& knowledge_base_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagDocumentListResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    try {
        std::string cursor;
        do {
            std::string url = impl_->service_url + "/api/v1/knowledge-bases/" +
                knowledge_base_id + "/documents?page_size=100";
            if (!cursor.empty()) url += "&cursor=" + UrlEncode(cursor);
            const auto response = impl_->ExecuteWithRetry([this, &url]() {
                return common::HttpClient::Get(
                    url, impl_->Headers(), impl_->Options());
            });
            result.error_message = ParseRequestError(response, impl_->service_url);
            if (!result.error_message.empty()) return result;
            const auto body = nlohmann::json::parse(response.body);
            const auto& items = body.at("items");
            if (!items.is_array()) {
                result.error_message = "Invalid RAG response: items must be an array";
                return result;
            }
            for (const auto& item : items) {
                result.items.push_back(ParseDocument(item));
            }
            const std::string previous_cursor = cursor;
            cursor = body.contains("next_cursor") && !body["next_cursor"].is_null()
                ? body["next_cursor"].get<std::string>() : std::string{};
            if (!cursor.empty() && cursor == previous_cursor) {
                result.error_message = "Invalid RAG response: pagination cursor did not advance";
                return result;
            }
        } while (!cursor.empty());
        result.success = true;
    } catch (const nlohmann::json::exception& error) {
        result.error_message =
            std::string("Invalid RAG service response: ") + error.what();
    }
    return result;
}

RagDocumentResponse RagClient::UploadDocument(
    const std::string& knowledge_base_id,
    const std::string& file_path,
    const std::string& display_name) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagDocumentResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    if (file_path.empty()) {
        result.error_message = "Document file path cannot be empty";
        return result;
    }

    std::vector<std::pair<std::string, std::string>> fields;
    if (!display_name.empty()) {
        fields.emplace_back("display_name", display_name);
    }
    common::MultipartFile file;
    file.field_name = "file";
    file.file_path = file_path;
    file.file_name = std::filesystem::u8path(file_path).filename().u8string();
    const auto response = common::HttpClient::PostMultipart(
        impl_->service_url + "/api/v1/knowledge-bases/" + knowledge_base_id +
            "/documents",
        fields, {file}, impl_->Headers(), impl_->Options());
    result.error_message = ParseRequestError(response, impl_->service_url);
    if (!result.error_message.empty()) {
        return result;
    }
    try {
        const auto body = nlohmann::json::parse(response.body);
        result.document = ParseDocument(body.at("document"));
        result.success = true;
    } catch (const nlohmann::json::exception& error) {
        result.error_message =
            std::string("Invalid RAG service response: ") + error.what();
    }
    return result;
}

RagOperationResponse RagClient::DeleteDocument(
    const std::string& knowledge_base_id,
    const std::string& document_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagOperationResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    const auto response = common::HttpClient::Delete(
        impl_->service_url + "/api/v1/knowledge-bases/" + knowledge_base_id +
            "/documents/" + document_id,
        impl_->Headers(), impl_->Options());
    result.error_message = ParseRequestError(response, impl_->service_url);
    result.success = result.error_message.empty();
    return result;
}

RagSearchResponse RagClient::Search(
    const std::string& question,
    const std::string& knowledge_base_id,
    int top_k,
    float similarity_threshold) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RagSearchResponse result;
    if (!impl_->initialized) {
        result.error_message = "RagClient is not initialized";
        return result;
    }
    const nlohmann::json request = {
        {"question", question},
        {"knowledge_base_id", knowledge_base_id},
        {"top_k", top_k},
        {"similarity_threshold", similarity_threshold}
    };
    const std::string request_body = request.dump();
    const auto response = impl_->ExecuteWithRetry([this, &request_body]() {
        return common::HttpClient::Post(
            impl_->service_url + "/api/v1/rag/search",
            request_body, impl_->Headers(true), impl_->Options());
    });
    result.error_message = ParseRequestError(response, impl_->service_url);
    if (!result.error_message.empty()) {
        return result;
    }
    try {
        const auto body = nlohmann::json::parse(response.body);
        result.knowledge_found = body.at("knowledge_found").get<bool>();
        const auto& sources = body.at("sources");
        if (!sources.is_array()) {
            result.error_message = "Invalid RAG response: sources must be an array";
            return result;
        }
        for (const auto& item : sources) {
            result.sources.push_back(ParseSource(item));
        }
        result.success = true;
    } catch (const nlohmann::json::exception& error) {
        result.error_message =
            std::string("Invalid RAG service response: ") + error.what();
    }
    return result;
}

void RagClient::ResetConversation() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->conversation_id.clear();
}

std::string RagClient::GetConversationID() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->conversation_id;
}

} // namespace services
} // namespace interview
