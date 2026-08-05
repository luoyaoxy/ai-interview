/**
 * @file http_client.cpp
 * @brief 通用 HTTP POST 请求实现。
 */

#include "common/http_client.h"

#include <array>
#include <memory>
#include <string>

#include <curl/curl.h>

namespace interview {
namespace common {
namespace {

class CurlGlobal final {
public:
    CurlGlobal()
        : result_(curl_global_init(CURL_GLOBAL_DEFAULT)) {
    }

    ~CurlGlobal() {
        if (result_ == CURLE_OK) {
            curl_global_cleanup();
        }
    }

    CURLcode Result() const noexcept {
        return result_;
    }

private:
    CURLcode result_;
};

const CurlGlobal& GetCurlGlobal() {
    static const CurlGlobal global;
    return global;
}

class CurlHeaders final {
public:
    ~CurlHeaders() {
        if (headers_) {
            curl_slist_free_all(headers_);
        }
    }

    bool Append(const std::string& header) {
        curl_slist* updated = curl_slist_append(headers_, header.c_str());
        if (!updated) {
            return false;
        }
        headers_ = updated;
        return true;
    }

    curl_slist* Get() const noexcept {
        return headers_;
    }

private:
    curl_slist* headers_ = nullptr;
};

size_t WriteCallback(void* contents, size_t size, size_t count, void* user_data) {
    auto* body = static_cast<std::string*>(user_data);
    const size_t bytes = size * count;
    body->append(static_cast<const char*>(contents), bytes);
    return bytes;
}

template <typename ConfigureRequest, typename CleanupRequest>
HttpResponse ExecuteRequest(
    const std::string& url,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options,
    ConfigureRequest configure_request,
    CleanupRequest cleanup_request) {
    HttpResponse response;

    if (url.empty()) {
        response.error_message = "HTTP URL is empty";
        return response;
    }

    const CURLcode global_result = GetCurlGlobal().Result();
    if (global_result != CURLE_OK) {
        response.error_message = std::string("Failed to initialize libcurl: ") +
                                 curl_easy_strerror(global_result);
        return response;
    }

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        response.error_message = "Failed to initialize CURL request";
        return response;
    }

    CurlHeaders request_headers;
    for (const auto& header : headers) {
        if (!request_headers.Append(header)) {
            response.error_message = "Failed to allocate HTTP request headers";
            return response;
        }
    }

    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    CURLcode option_result = CURLE_OK;
    auto set_option = [&](CURLoption option, auto value) {
        if (option_result == CURLE_OK) {
            option_result = curl_easy_setopt(curl.get(), option, value);
        }
    };

    set_option(CURLOPT_ERRORBUFFER, error_buffer.data());
    set_option(CURLOPT_URL, url.c_str());
    set_option(CURLOPT_HTTPHEADER, request_headers.Get());
    set_option(CURLOPT_WRITEFUNCTION, WriteCallback);
    set_option(CURLOPT_WRITEDATA, &response.body);
    set_option(CURLOPT_TIMEOUT, options.timeout_seconds);
    set_option(CURLOPT_SSL_VERIFYPEER, options.verify_ssl ? 1L : 0L);
    set_option(CURLOPT_SSL_VERIFYHOST, options.verify_ssl ? 2L : 0L);
    set_option(CURLOPT_NOSIGNAL, 1L);
    const CURLcode configure_result = configure_request(curl.get(), set_option);

    if (configure_result != CURLE_OK || option_result != CURLE_OK) {
        const CURLcode error = configure_result != CURLE_OK
            ? configure_result : option_result;
        response.error_message = std::string("Failed to configure HTTP request: ") +
                                 curl_easy_strerror(error);
        cleanup_request();
        return response;
    }

    const CURLcode perform_result = curl_easy_perform(curl.get());
    if (perform_result != CURLE_OK) {
        response.error_message = error_buffer[0] != '\0'
            ? error_buffer.data()
            : curl_easy_strerror(perform_result);
        cleanup_request();
        return response;
    }

    const CURLcode info_result = curl_easy_getinfo(
        curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    if (info_result != CURLE_OK) {
        response.error_message = std::string("Failed to read HTTP status code: ") +
                                 curl_easy_strerror(info_result);
    }
    cleanup_request();
    return response;
}

} // namespace

bool HttpResponse::TransportSucceeded() const noexcept {
    return error_message.empty();
}

bool HttpResponse::IsSuccess() const noexcept {
    return TransportSucceeded() && status_code >= 200 && status_code < 300;
}

HttpResponse HttpClient::Request(
    HttpMethod method,
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    return ExecuteRequest(url, headers, options,
        [method, &body](CURL*, auto& set_option) {
            switch (method) {
            case HttpMethod::kGet:
                set_option(CURLOPT_HTTPGET, 1L);
                break;
            case HttpMethod::kPost:
                set_option(CURLOPT_POST, 1L);
                set_option(CURLOPT_POSTFIELDS, body.c_str());
                set_option(CURLOPT_POSTFIELDSIZE_LARGE,
                           static_cast<curl_off_t>(body.size()));
                break;
            case HttpMethod::kPatch:
                set_option(CURLOPT_CUSTOMREQUEST, "PATCH");
                set_option(CURLOPT_POSTFIELDS, body.c_str());
                set_option(CURLOPT_POSTFIELDSIZE_LARGE,
                           static_cast<curl_off_t>(body.size()));
                break;
            case HttpMethod::kDelete:
                set_option(CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            }
            return CURLE_OK;
        }, []() {});
}

HttpResponse HttpClient::Get(
    const std::string& url,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    return Request(HttpMethod::kGet, url, {}, headers, options);
}

HttpResponse HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    return Request(HttpMethod::kPost, url, body, headers, options);
}

HttpResponse HttpClient::Patch(
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    return Request(HttpMethod::kPatch, url, body, headers, options);
}

HttpResponse HttpClient::Delete(
    const std::string& url,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    return Request(HttpMethod::kDelete, url, {}, headers, options);
}

HttpResponse HttpClient::PostMultipart(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<MultipartFile>& files,
    const std::vector<std::string>& headers,
    const HttpRequestOptions& options) {
    curl_mime* mime = nullptr;
    return ExecuteRequest(url, headers, options,
        [&fields, &files, &mime](CURL* curl, auto& set_option) {
            mime = curl_mime_init(curl);
            if (!mime) {
                return CURLE_OUT_OF_MEMORY;
            }
            for (const auto& field : fields) {
                curl_mimepart* part = curl_mime_addpart(mime);
                if (!part) return CURLE_OUT_OF_MEMORY;
                CURLcode result = curl_mime_name(part, field.first.c_str());
                if (result != CURLE_OK) return result;
                result = curl_mime_data(
                    part, field.second.c_str(), CURL_ZERO_TERMINATED);
                if (result != CURLE_OK) return result;
            }
            for (const auto& file : files) {
                curl_mimepart* part = curl_mime_addpart(mime);
                if (!part) return CURLE_OUT_OF_MEMORY;
                CURLcode result = curl_mime_name(part, file.field_name.c_str());
                if (result != CURLE_OK) return result;
                result = curl_mime_filedata(part, file.file_path.c_str());
                if (result != CURLE_OK) return result;
                if (!file.file_name.empty()) {
                    result = curl_mime_filename(part, file.file_name.c_str());
                    if (result != CURLE_OK) return result;
                }
                if (!file.content_type.empty()) {
                    result = curl_mime_type(part, file.content_type.c_str());
                    if (result != CURLE_OK) return result;
                }
            }
            set_option(CURLOPT_MIMEPOST, mime);
            return CURLE_OK;
        }, [&mime]() {
            if (mime) {
                curl_mime_free(mime);
                mime = nullptr;
            }
        });
}

} // namespace common
} // namespace interview
