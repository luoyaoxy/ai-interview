/**
 * @file embedding_client.cpp
 * @brief 文本向量化 HTTP 客户端实现。
 *
 * 主要模块：请求构造、单条与批量向量化、重试处理和响应解析。
 */

#include "services/embedding_client.h"
#include "common/http_client.h"
#include "common/logger.h"

#include <chrono>
#include <mutex>
#include <thread>

namespace interview {
namespace services {

// ============================================================
// 辅助函数：构造失败的 EmbeddingResponse
// ============================================================
static EmbeddingResponse MakeErrorResponse(const std::string& msg) {
    EmbeddingResponse er;
    er.success = false;
    er.error_message = msg;
    return er;
}

// ============================================================
// Pimpl 实现类
// ============================================================
class EmbeddingClient::Impl {
public:
    std::string api_url_;
    std::string api_key_;
    std::string model_;
    int dimension_ = 0;
    std::mutex request_mutex_;
    std::string last_error_;

    // 重试配置
    static constexpr int kMaxRetries = 3;
    static constexpr int kBaseDelayMs = 1000;  // 首次重试延迟 1 秒

    // ========================================================
    // 初始化
    // ========================================================
    bool Initialize(const std::string& api_url,
                    const std::string& api_key,
                    const std::string& model) {
        if (api_url.empty()) {
            LOG_ERROR("EmbeddingClient: API URL is empty");
            return false;
        }
        if (model.empty()) {
            LOG_ERROR("EmbeddingClient: model name is empty");
            return false;
        }

        api_url_ = api_url;
        api_key_ = api_key;
        model_ = model;

        LOG_INFO("EmbeddingClient initialized: url={}, model={}", api_url_, model_);
        return true;
    }

    // ========================================================
    // 单条 Embed（内部委托给 EmbedBatch）
    // ========================================================
    EmbeddingResponse Embed(const std::string& text) {
        auto batch = EmbedBatch({text});
        if (batch.empty()) {
            return MakeErrorResponse("EmbedBatch returned empty result");
        }
        return batch[0];
    }

    // ========================================================
    // 批量 Embed
    // ========================================================
    std::vector<EmbeddingResponse> EmbedBatch(
        const std::vector<std::string>& texts) {

        if (texts.empty()) {
            return {MakeErrorResponse("texts array is empty")};
        }

        std::lock_guard<std::mutex> request_lock(request_mutex_);

        LOG_INFO("EmbeddingClient::EmbedBatch: processing {} text(s)", texts.size());

        // 构造请求体
        nlohmann::json body;
        body["model"] = model_;
        // 单条文本用字符串，多条用数组（OpenAI 兼容格式均支持数组）
        body["input"] = texts;

        std::string request_json = body.dump();
        LOG_DEBUG("Embedding request body size: {} bytes", request_json.size());

        // 发送请求（带重试）
        std::string response_str;
        bool ok = SendRequest(request_json, response_str);

        if (!ok) {
            std::string err = last_error_.empty()
                ? "Embedding request failed after retries"
                : last_error_;
            LOG_ERROR("EmbeddingClient: {}", err);
            std::vector<EmbeddingResponse> errors;
            for (size_t i = 0; i < texts.size(); i++) {
                errors.push_back(MakeErrorResponse(err));
            }
            return errors;
        }

        // 解析响应
        return ParseResponse(response_str, texts.size());
    }

    // ========================================================
    // 发送 HTTP POST 请求（带重试）
    // ========================================================
    bool SendRequest(const std::string& request_json, std::string& response_str) {
        last_error_.clear();
        for (int attempt = 0; attempt < kMaxRetries; attempt++) {
            if (attempt > 0) {
                int delay_ms = kBaseDelayMs * (1 << (attempt - 1));  // 1s, 2s, 4s
                LOG_WARN("EmbeddingClient: retry {}/{} after {}ms",
                         attempt, kMaxRetries - 1, delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            std::vector<std::string> headers = {
                "Content-Type: application/json; charset=utf-8"
            };
            if (!api_key_.empty()) {
                headers.push_back("Authorization: Bearer " + api_key_);
            }

            common::HttpRequestOptions options;
            options.timeout_seconds = 30;
            options.verify_ssl = false;
            const common::HttpResponse response = common::HttpClient::Post(
                api_url_, request_json, headers, options);
            response_str = response.body;

            if (!response.TransportSucceeded()) {
                last_error_ = std::string("Cannot connect to embedding service at ") +
                              api_url_ + ": " + response.error_message;
                LOG_ERROR("EmbeddingClient: CURL error (attempt {}): {}",
                          attempt + 1, response.error_message);
                continue;  // 重试
            }

            const long http_code = response.status_code;

            LOG_INFO("EmbeddingClient: HTTP {}", http_code);

            if (http_code == 200) {
                return true;
            }

            LOG_ERROR("EmbeddingClient: HTTP {} error (attempt {}): {}",
                      http_code, attempt + 1, response_str);
            last_error_ = "Embedding service returned HTTP " +
                          std::to_string(http_code) + ": " + response_str;

            // 4xx 错误不重试（客户端错误，如密钥无效）
            if (http_code >= 400 && http_code < 500) {
                return false;
            }
            // 5xx 错误重试（服务器错误）
        }

        return false;
    }

    // ========================================================
    // 解析 Embedding API 响应
    // ========================================================
    std::vector<EmbeddingResponse> ParseResponse(
        const std::string& response_str,
        size_t expected_count) {

        std::vector<EmbeddingResponse> results;

        try {
            auto json_resp = nlohmann::json::parse(response_str);

            // 检查是否有错误字段
            if (json_resp.contains("error")) {
                std::string err_msg = json_resp["error"].value(
                    "message",
                    json_resp["error"].dump());
                LOG_ERROR("EmbeddingClient: API error: {}", err_msg);
                for (size_t i = 0; i < expected_count; i++) {
                    results.push_back(MakeErrorResponse(err_msg));
                }
                return results;
            }

            if (json_resp.contains("embeddings") &&
                json_resp["embeddings"].is_array()) {
                for (const auto& embedding : json_resp["embeddings"]) {
                    EmbeddingResponse er;
                    if (embedding.is_array()) {
                        er.embedding = embedding.get<std::vector<float>>();
                        er.dimension = static_cast<int>(er.embedding.size());
                        er.success = !er.embedding.empty();
                        if (!er.success) {
                            er.error_message = "Embedding vector is empty";
                        }
                    } else {
                        er.error_message = "Invalid item in 'embeddings' array";
                    }
                    results.push_back(std::move(er));
                }
            } else if (json_resp.contains("data") &&
                       json_resp["data"].is_array()) {
                const auto& data = json_resp["data"];
                for (const auto& item : data) {
                    EmbeddingResponse er;
                    er.success = true;

                    if (item.contains("embedding") &&
                        item["embedding"].is_array()) {
                        er.embedding = item["embedding"]
                            .get<std::vector<float>>();
                        er.dimension = static_cast<int>(er.embedding.size());
                        er.success = !er.embedding.empty();
                        if (!er.success) {
                            er.error_message = "Embedding vector is empty";
                        }
                    } else {
                        er.success = false;
                        er.error_message = "Item missing 'embedding' field";
                    }

                    results.push_back(std::move(er));
                }
            } else {
                LOG_ERROR(
                    "EmbeddingClient: response missing 'embeddings' or 'data' array");
                for (size_t i = 0; i < expected_count; i++) {
                    results.push_back(MakeErrorResponse(
                        "Response missing 'embeddings' or 'data' array"));
                }
                return results;
            }

            if (results.size() != expected_count) {
                const std::string error =
                    "Embedding count mismatch: expected " +
                    std::to_string(expected_count) + ", got " +
                    std::to_string(results.size());
                LOG_ERROR("EmbeddingClient: {}", error);
                results.assign(expected_count, MakeErrorResponse(error));
                return results;
            }

            int actual_dimension = 0;
            for (auto& result : results) {
                if (!result.success || result.embedding.empty()) {
                    result.success = false;
                    if (result.error_message.empty()) {
                        result.error_message = "Embedding vector is empty";
                    }
                    continue;
                }
                if (actual_dimension == 0) {
                    actual_dimension = result.dimension;
                } else if (result.dimension != actual_dimension) {
                    const std::string error =
                        "Embedding dimensions are inconsistent";
                    results.assign(expected_count, MakeErrorResponse(error));
                    return results;
                }
            }

            // 记录维度信息
            if (!results.empty() && results[0].success) {
                dimension_ = actual_dimension;
            }

            // 记录 token 用量（如果有）
            if (json_resp.contains("usage")) {
                LOG_INFO("EmbeddingClient: tokens used: {}",
                         json_resp["usage"].value("total_tokens", 0));
            }

            LOG_INFO("EmbeddingClient: got {} embedding(s), dim={}",
                     results.size(), dimension_);

        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("EmbeddingClient: JSON parse error: {}", e.what());
            for (size_t i = 0; i < expected_count; i++) {
                results.push_back(MakeErrorResponse(
                    std::string("JSON parse error: ") + e.what()));
            }
        }

        return results;
    }
};

// ============================================================
// 公开接口（Pimpl 转发）
// ============================================================

EmbeddingClient::EmbeddingClient()
    : impl_(std::make_unique<Impl>()) {
}

EmbeddingClient::~EmbeddingClient() = default;

bool EmbeddingClient::Initialize(
    const std::string& api_url,
    const std::string& api_key,
    const std::string& model) {
    return impl_->Initialize(api_url, api_key, model);
}

EmbeddingResponse EmbeddingClient::Embed(const std::string& text) {
    return impl_->Embed(text);
}

std::vector<EmbeddingResponse> EmbeddingClient::EmbedBatch(
    const std::vector<std::string>& texts) {
    return impl_->EmbedBatch(texts);
}

int EmbeddingClient::GetDimension() const {
    return impl_->dimension_;
}

} // namespace services
} // namespace interview
