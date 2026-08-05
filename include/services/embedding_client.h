/**
 * @file embedding_client.h
 * @brief Embedding 向量化 API 客户端模块
 *
 * 与 Embedding API 交互的客户端类。
 * 负责将文本转换为高维向量（Embedding），供 RAG 检索使用。
 *
 * 核心功能：
 * 1. 将单段文本转换为 Embedding 向量
 * 2. 批量转换多段文本（节省网络开销）
 * 3. 支持 OpenAI 兼容的 Embedding API
 *
 * 支持的 API：
 * - DeepSeek Embedding API（默认，复用 DEEPSEEK_API_KEY）
 * - 其他 OpenAI 兼容的 Embedding 服务
 *
 * 架构设计：
 * - Pimpl 模式：隐藏实现细节，减少头文件依赖
 * - 同步阻塞调用：每次请求 0.5-3 秒
 * - 通过通用 HttpClient 模块发送网络请求
 *
 * 使用示例：
 * @code
 * services::EmbeddingClient client;
 * client.Initialize(
 *     "http://127.0.0.1:11434/api/embed",
 *     "",
 *     "qwen3-embedding:0.6b");
 *
 * auto result = client.Embed("C++ 虚函数表是如何工作的？");
 * if (result.success) {
 *     // qwen3-embedding:0.6b 当前返回 1024 维 float 向量
 *     for (float v : result.embedding) { ... }
 * }
 * @endcode
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace interview {
namespace services {

/**
 * @brief 文档块结构体
 *
 * 表示知识库中被切割后的一个文档片段。
 * 包含原文内容及其对应的 Embedding 向量。
 *
 * 字段说明：
 * - chunk_id: 唯一标识，格式 "doc001_chunk003"
 * - content: 原始文本内容（供检索时返回给 LLM）
 * - embedding: 向量化表示（存储时填充，检索时用于相似度计算）
 * - metadata: JSON 格式的额外信息（来源文件名、页码、字符位置等）
 */
struct DocumentChunk {
    std::string chunk_id;              ///< 唯一ID，格式 "文件名_chunk序号"
    std::string content;               ///< 原文内容
    std::vector<float> embedding;      ///< 嵌入向量（维度取决于模型）
    std::string metadata;              ///< 额外信息（JSON格式），如来源文件名、页码
};

/**
 * @brief Embedding 请求结构体
 *
 * 封装一次 Embedding API 调用所需的参数。
 */
struct EmbeddingRequest {
    std::string text;   ///< 待向量化的文本
    std::string model;  ///< 模型名称，如 "qwen3-embedding:0.6b"
};

/**
 * @brief Embedding 响应结构体
 *
 * 封装 Embedding API 的返回结果。
 * 包含成功/失败状态、向量数据和错误信息。
 */
struct EmbeddingResponse {
    std::vector<float> embedding;  ///< 向量数据（success=true时有效）
    std::string error_message;     ///< 错误描述（success=false时有效）
    int dimension = 0;             ///< 向量维度
    bool success = false;          ///< 是否成功
};

/**
 * @brief Embedding API 客户端类
 *
 * 封装与 Embedding API 的所有交互逻辑。
 * 提供单条和批量两种文本向量化接口。
 *
 * 主要功能：
 * 1. 文本 → 向量（单条）
 * 2. 文本 → 向量（批量，一次 API 调用处理多条）
 * 3. 获取模型向量维度
 *
 * API 协议：
 * - 请求：POST {api_url}，Body: {"model": "...", "input": "..."|[...]}
 * - 响应：{"data": [{"index": 0, "embedding": [0.1, -0.2, ...]}], ...}
 * - 认证：Authorization: Bearer {api_key}
 *
 * 错误处理：
 * - 网络错误：返回 success=false，error_message 包含详情
 * - API 错误：检查 HTTP 状态码，非 200 返回失败
 * - JSON 解析失败：返回 success=false
 * - 不抛异常（调用方检查 success 字段即可）
 *
 * 性能考虑：
 * - 单条 Embed 内部调用 EmbedBatch，开销相同
 * - 批量 EmbedBatch 可显著减少网络往返次数
 * - 文本长度限制约 8192 tokens（由 API 决定）
 */
class EmbeddingClient {
public:
    /// @brief 实现类前向声明（Pimpl 模式）
    struct Impl;

    /**
     * @brief 构造函数
     *
     * 创建 Embedding 客户端实例，网络资源由通用 HttpClient 模块管理。
     */
    EmbeddingClient();

    /**
     * @brief 析构函数
     *
     * 清理客户端内部资源。
     */
    ~EmbeddingClient();

    /**
     * @brief 初始化客户端
     *
     * 配置 API 地址、密钥和模型名称。
     * 必须在调用 Embed/EmbedBatch 之前调用。
     *
     * @param api_url Embedding API 端点地址
     * @param api_key API 密钥
     * @param model 模型名称
     * @return true 初始化成功，false 失败
     */
    bool Initialize(const std::string& api_url,
                    const std::string& api_key,
                    const std::string& model = "qwen3-embedding:0.6b");

    /**
     * @brief 将单段文本转为向量
     *
     * 内部调用 EmbedBatch({text})，返回第一条结果。
     *
     * @param text 待向量化的文本
     * @return EmbeddingResponse 包含向量或错误信息
     */
    EmbeddingResponse Embed(const std::string& text);

    /**
     * @brief 批量将多段文本转为向量
     *
     * 一次 HTTP 请求处理多条文本，显著减少网络开销。
     * 适合文档分块后批量向量化的场景。
     *
     * @param texts 待向量化的文本列表
     * @return 与输入顺序一致的 EmbeddingResponse 列表
     */
    std::vector<EmbeddingResponse> EmbedBatch(
        const std::vector<std::string>& texts);

    /**
     * @brief 获取当前模型的向量维度
     *
     * 首次调用 Embed/EmbedBatch 后可用。
     * 维度取决于使用的模型，不在客户端代码中写死。
     *
     * @return 向量维度，未初始化时返回 0
     */
    int GetDimension() const;

    /// @brief 禁用拷贝
    EmbeddingClient(const EmbeddingClient&) = delete;
    EmbeddingClient& operator=(const EmbeddingClient&) = delete;

private:
    std::unique_ptr<Impl> impl_;  ///< 实现指针（Pimpl 模式）
};

} // namespace services
} // namespace interview
