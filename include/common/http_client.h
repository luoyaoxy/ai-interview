/**
 * @file http_client.h
 * @brief 基于 libcurl 的通用 HTTP 客户端。
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace interview {
namespace common {

struct HttpRequestOptions {
    long timeout_seconds = 30;
    bool verify_ssl = true;
};

struct HttpResponse {
    std::string body;
    std::string error_message;
    long status_code = 0;

    bool TransportSucceeded() const noexcept;
    bool IsSuccess() const noexcept;
};

enum class HttpMethod {
    kGet,
    kPost,
    kPatch,
    kDelete
};

struct MultipartFile {
    std::string field_name;
    std::string file_path;
    std::string file_name;
    std::string content_type;
};

class HttpClient final {
public:
    static HttpResponse Request(
        HttpMethod method,
        const std::string& url,
        const std::string& body = {},
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});

    static HttpResponse Get(
        const std::string& url,
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});

    /**
     * @brief 发送 HTTP POST 请求。
     *
     * 该方法只处理通用网络逻辑，不解析业务 JSON，也不决定是否重试。
     */
    static HttpResponse Post(
        const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});

    static HttpResponse Patch(
        const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});

    static HttpResponse Delete(
        const std::string& url,
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});

    static HttpResponse PostMultipart(
        const std::string& url,
        const std::vector<std::pair<std::string, std::string>>& fields,
        const std::vector<MultipartFile>& files,
        const std::vector<std::string>& headers = {},
        const HttpRequestOptions& options = {});
};

} // namespace common
} // namespace interview
