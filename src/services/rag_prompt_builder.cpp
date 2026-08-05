/**
 * @file rag_prompt_builder.cpp
 * @brief RAG 检索结果与系统角色提示词的组装实现。
 *
 * 主要模块：内置角色、向量检索、来源整理和最终提示词构建。
 */

#include "services/rag_prompt_builder.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

namespace interview {
namespace services {

// ============================================================
// 面试官与 AI 面试助手两套内置角色预设
// ============================================================

const SystemRole kInterviewerRole = {
    "专业面试官",
    "你是一位资深技术面试官，负责评估候选人的技术能力和综合素质。"
    "你需要根据参考知识库中的岗位要求和技术标准来提问和评估。"
    "请根据候选人的回答质量进行评分和反馈。",
    "专业严谨，温和但不失锐度",
    "抱歉，我在当前知识库中没有找到相关岗位标准，请问能否提供更多信息？"
};

const SystemRole kGeneralAssistantRole = {
    "AI 面试助手",
    "你是一位技术面试学习助手，只根据当前启用知识库中的参考资料回答问题，"
    "帮助用户进行面试复习。不得编造知识库中不存在的内容。",
    "专业准确，简洁清晰，保留标准技术术语",
    "当前知识库中没有检索到与该问题相关的内容，请调整问题表述或上传相关技术资料后重试。"
};

// ============================================================
// 辅助函数：将相似度格式化为百分比字符串
// ============================================================
static std::string FormatSimilarity(float sim) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << (sim * 100.0f) << "%";
    return oss.str();
}

// ============================================================
// 辅助函数：从 metadata JSON 中提取源文件名
// ============================================================
static std::string ExtractSourceName(const std::string& metadata_json) {
    try {
        auto meta = nlohmann::json::parse(metadata_json);
        return meta.value("source", "未知来源");
    } catch (...) {
        return "未知来源";
    }
}

// ============================================================
// 辅助函数：格式化知识库检索结果为 Prompt 段落
// ============================================================
static std::string FormatKnowledgeContext(
    const std::vector<SearchResult>& results) {

    if (results.empty()) {
        return "";
    }

    std::ostringstream ctx;
    ctx << "## 参考知识库（以下是与当前问题相关的已知信息）\n\n";

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        std::string source = ExtractSourceName(r.chunk.metadata);

        ctx << "### 参考来源 " << (i + 1)
            << "（来源：" << source
            << "，相关度：" << FormatSimilarity(r.similarity) << "）\n";
        ctx << r.chunk.content << "\n\n";
    }

    ctx << "---\n";
    ctx << "**重要提示**：请仅根据以上参考资料回答用户问题。";
    ctx << "如果参考资料中没有相关信息，请如实告知用户，";
    ctx << "不要编造不存在的答案。\n\n";

    return ctx.str();
}

// ============================================================
// 辅助函数：构建完整的 System Prompt
// ============================================================
static std::string BuildSystemPrompt(
    const SystemRole& role,
    const std::string& knowledge_context) {

    std::ostringstream prompt;

    // --- 角色定义 ---
    prompt << "## 你的角色\n";
    prompt << "你是" << role.role_name << "。\n";
    prompt << role.role_description << "\n";
    prompt << "回复风格：" << role.response_style << "\n\n";

    // --- 知识库上下文 ---
    if (!knowledge_context.empty()) {
        prompt << knowledge_context;
    } else {
        prompt << "## 参考知识库\n";
        prompt << "（未检索到与当前问题相关的知识，请根据你的通用知识回答，";
        prompt << "但需明确告知用户「知识库中暂无相关信息」）\n\n";
    }

    // --- 行为规范 ---
    prompt << "## 行为规范\n";
    prompt << "1. 优先使用参考知识库中的信息回答问题\n";
    prompt << "2. 引用知识时，请在回答中标注来源编号（如「根据参考来源1」）\n";
    prompt << "3. 保持「" << role.response_style << "」的沟通风格\n";
    prompt << "4. 如果参考知识库信息不完整或存在矛盾，请明确指出\n";
    prompt << "5. 使用中文回复，回答简洁有针对性\n";

    return prompt.str();
}

// ============================================================
// Pimpl 实现类
// ============================================================
class RAGPromptBuilder::Impl {
public:
    std::shared_ptr<EmbeddingClient> embed_client_;
    std::shared_ptr<VectorStore> vector_store_;
    SystemRole current_role_;
    bool initialized_ = false;
    float similarity_threshold_ = 0.7f;

    // ========================================================
    // 构建 Prompt（完整流程）
    // ========================================================
    RAGPrompt BuildPrompt(const std::string& user_query, int top_k) {
        RAGPrompt result;

        if (!initialized_) {
            LOG_ERROR("RAGPromptBuilder: not initialized");
            result.system_prompt = BuildSystemPrompt(current_role_, "");
            result.user_prompt = user_query;
            result.knowledge_found = false;
            return result;
        }

        if (user_query.empty()) {
            result.system_prompt = BuildSystemPrompt(current_role_, "");
            result.user_prompt = user_query;
            result.knowledge_found = false;
            return result;
        }

        // Step 1：将用户问题转为向量
        auto embed_resp = embed_client_->Embed(user_query);
        if (!embed_resp.success) {
            LOG_ERROR("RAGPromptBuilder: embedding failed: {}",
                      embed_resp.error_message);
            result.system_prompt = BuildSystemPrompt(current_role_, "");
            result.user_prompt = user_query;
            result.knowledge_found = false;
            return result;
        }

        // Step 2：检索 Top-K 相关知识
        auto search_results = vector_store_->Search(
            embed_resp.embedding, top_k, similarity_threshold_);

        // Step 3：构建最终 Prompt
        return BuildPromptWithResults(user_query, search_results);
    }

    // ========================================================
    // 使用已有检索结果构建 Prompt
    // ========================================================
    RAGPrompt BuildPromptWithResults(
        const std::string& user_query,
        const std::vector<SearchResult>& search_results) {

        RAGPrompt result;
        result.user_prompt = user_query;
        result.sources = search_results;
        result.knowledge_found = !search_results.empty();

        // 格式化知识上下文
        std::string knowledge_context = FormatKnowledgeContext(search_results);

        // 构建 System Prompt
        result.system_prompt = BuildSystemPrompt(current_role_, knowledge_context);

        LOG_INFO("RAGPromptBuilder: built prompt, knowledge_found={}, "
                 "sources={}, system_prompt_len={}",
                 result.knowledge_found,
                 result.sources.size(),
                 result.system_prompt.length());

        return result;
    }
};

// ============================================================
// 公开接口（Pimpl 转发）
// ============================================================

RAGPromptBuilder::RAGPromptBuilder()
    : impl_(std::make_unique<Impl>()) {
}

RAGPromptBuilder::~RAGPromptBuilder() = default;

void RAGPromptBuilder::Initialize(
    std::shared_ptr<EmbeddingClient> embed_client,
    std::shared_ptr<VectorStore> vector_store) {

    impl_->embed_client_ = std::move(embed_client);
    impl_->vector_store_  = std::move(vector_store);
    impl_->initialized_   = true;

    // 默认角色：通用助手
    if (impl_->current_role_.role_name.empty()) {
        impl_->current_role_ = kGeneralAssistantRole;
    }

    LOG_INFO("RAGPromptBuilder: initialized (default role: {})",
             impl_->current_role_.role_name);
}

void RAGPromptBuilder::SetSystemRole(const SystemRole& role) {
    impl_->current_role_ = role;
    LOG_INFO("RAGPromptBuilder: role set to '{}'", role.role_name);
}

void RAGPromptBuilder::SetSimilarityThreshold(float threshold) {
    impl_->similarity_threshold_ =
        std::max(0.0f, std::min(1.0f, threshold));
}

const SystemRole& RAGPromptBuilder::GetSystemRole() const {
    return impl_->current_role_;
}

RAGPrompt RAGPromptBuilder::BuildPrompt(
    const std::string& user_query, int top_k) {
    return impl_->BuildPrompt(user_query, top_k);
}

RAGPrompt RAGPromptBuilder::BuildPromptWithResults(
    const std::string& user_query,
    const std::vector<SearchResult>& search_results) {
    return impl_->BuildPromptWithResults(user_query, search_results);
}

} // namespace services
} // namespace interview
