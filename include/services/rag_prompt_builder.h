/**
 * @file rag_prompt_builder.h
 * @brief RAG Prompt 构建模块
 *
 * 将向量检索结果动态注入 LLM 的 System Prompt 中，
 * 使对话能够利用知识库内容进行回答。
 *
 * 核心功能：
 * 1. 接收用户查询 → 自动向量检索 → 拼接 Prompt → 返回完整上下文
 * 2. 管理面试官与 AI 面试助手两套角色预设
 * 3. 知识库命中/未命中时生成不同的 Prompt 策略
 *
 * 设计理念：
 * - 把"知识"从"角色"中解耦：同一套系统，换角色+换知识库=换场景
 * - 上传技术文档 + AI 面试助手角色 = 面试知识问答
 * - 上传面试题库 + 面试官角色 = 面试系统
 * - 上传规章制度 + 助手角色 = 企业助手
 *
 * 工作流程：
 * @code
 * RAGPromptBuilder builder;
 * builder.Initialize(embed_client, vector_store);
 * builder.SetSystemRole(kGeneralAssistantRole);
 *
 * // 一句话完成 检索+Prompt构建
 * auto prompt = builder.BuildPrompt("怎么退货？", 3);
 * // prompt.system_prompt → 发给 LLM 的完整 system prompt
 * // prompt.sources        → 检索到的知识来源（可展示给用户）
 * @endcode
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "services/embedding_client.h"
#include "services/vector_store.h"

namespace interview {
namespace services {

/**
 * @brief 系统角色配置
 *
 * 定义一个对话场景的全部行为参数。
 * 不同角色本质上是不同的 System Prompt 模板。
 *
 * 扩展示例：
 * - 面试官：角色描述强调"评估候选人"，风格"专业严谨"
 * - 客服：角色描述强调"解决问题"，风格"亲切友好"
 * - 企业助手：角色描述强调"内部知识"，风格"简洁准确"
 */
struct SystemRole {
    std::string role_name;          ///< 角色名称："面试官"/"客服"/"助手"
    std::string role_description;   ///< 角色职责描述，会注入 System Prompt
    std::string response_style;     ///< 回复风格："专业严谨"/"亲切友好"/"简洁准确"
    std::string fallback_response;  ///< 知识库无匹配时的兜底回复话术
};

/**
 * @brief RAG Prompt 构建结果
 *
 * 包含构建完成的 System Prompt、User Prompt 以及本次检索到的知识来源。
 * 调用方直接将其传给 LLMClient 即可。
 */
struct RAGPrompt {
    std::string system_prompt;                ///< 完整的 System Prompt（含知识库上下文）
    std::string user_prompt;                  ///< 用户消息 Prompt
    std::vector<SearchResult> sources;        ///< 本次检索到的知识来源（用于溯源展示）
    bool knowledge_found = false;             ///< 是否检索到了相关知识
};

/**
 * @brief RAG Prompt 构建器（Pimpl 模式）
 *
 * 职责单一：把"用户问题 + 角色 + 知识库"组装成 LLM 可用的 Prompt。
 *
 * 内部流程：
 * 1. 用 EmbeddingClient 将用户问题转为向量
 * 2. 用 VectorStore 检索 Top-K 相似文档块
 * 3. 将检索结果格式化为"参考知识库"段落
 * 4. 结合 SystemRole 生成完整的 System Prompt
 *
 * 依赖关系：
 * - EmbeddingClient：文本 → 向量（来自第一步）
 * - VectorStore：向量 → 文档块（来自第二步）
 * - DocumentLoader：文档 → 文本块（来自第三步，间接：用户先上传后，存入库中）
 */
class RAGPromptBuilder {
public:
    /// @brief 实现类前向声明（Pimpl 模式）
    struct Impl;

    RAGPromptBuilder();
    ~RAGPromptBuilder();

    // ========================================================
    // 初始化
    // ========================================================

    /**
     * @brief 初始化构建器
     *
     * 注入 Embedding 客户端和向量存储两个核心依赖。
     * 必须在使用 BuildPrompt 之前调用。
     *
     * @param embed_client Embedding 客户端（需已初始化）
     * @param vector_store 向量存储（需已打开数据库）
     */
    void Initialize(std::shared_ptr<EmbeddingClient> embed_client,
                    std::shared_ptr<VectorStore> vector_store);

    /// @brief 设置检索相似度阈值，范围为 0.0 到 1.0。
    void SetSimilarityThreshold(float threshold);

    // ========================================================
    // 角色管理
    // ========================================================

    /**
     * @brief 设置当前系统角色
     *
     * 角色决定了 System Prompt 的基调。
     * 提供两套内置预设（见 .cpp 文件中的常量）：
     * - kInterviewerRole：专业面试官
     * - kGeneralAssistantRole：AI 面试助手
     *
     * @param role 角色配置
     */
    void SetSystemRole(const SystemRole& role);

    /**
     * @brief 获取当前角色
     */
    const SystemRole& GetSystemRole() const;

    // ========================================================
    // Prompt 构建
    // ========================================================

    /**
     * @brief 构建 RAG Prompt（完整流程）
     *
     * 一步完成：用户问题向量化 → 知识库检索 → Prompt 拼接。
     *
     * 这是最常用的方法，调用方只需提供用户原始输入。
     *
     * @param user_query 用户原始输入（ASR 转写结果）
     * @param top_k 检索多少条相关知识，默认 3
     * @return 包含完整 System Prompt 和检索来源的 RAGPrompt
     */
    RAGPrompt BuildPrompt(const std::string& user_query, int top_k = 3);

    /**
     * @brief 使用已有检索结果构建 Prompt（跳过检索步骤）
     *
     * 用于调用方自行控制检索逻辑的场景。
     * 例如：先用多个知识库分别检索，再合并结果传给此方法。
     *
     * @param user_query 用户原始输入
     * @param search_results 已检索到的结果
     * @return RAGPrompt
     */
    RAGPrompt BuildPromptWithResults(
        const std::string& user_query,
        const std::vector<SearchResult>& search_results);

    /// @brief 禁用拷贝
    RAGPromptBuilder(const RAGPromptBuilder&) = delete;
    RAGPromptBuilder& operator=(const RAGPromptBuilder&) = delete;

private:
    std::unique_ptr<Impl> impl_;
};

// ============================================================
// 三套内置角色预设
// ============================================================

/// @brief 面试官角色
extern const SystemRole kInterviewerRole;

/// @brief AI 面试助手角色
extern const SystemRole kGeneralAssistantRole;

} // namespace services
} // namespace interview
