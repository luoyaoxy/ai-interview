/**
 * @file llm_client.cpp
 * @brief 大语言模型 HTTP 客户端及面试智能任务实现。
 *
 * 主要模块：通用对话、问题生成、回答评分、面试总结和 RAG 对话。
 */

#include "services/llm_client.h"
#include "common/config.h"
#include "common/http_client.h"
#include "common/logger.h"
#include "common/utils.h"

#include <algorithm>
#include <stdexcept>

// Undefine Windows macros that conflict with our methods
#ifdef SendMessage
#undef SendMessage
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace interview {
namespace services {

class LLMClient::LLMClientImpl {
public:
    // 对话历史记录
    struct ConversationTurn {
        std::string user;
        std::string assistant;
    };

    std::vector<ConversationTurn> history_;
    int max_history_turns_ = 5;

    std::string CallAPI(const nlohmann::json& request_body) {
        auto& cfg = common::Config::Instance().llm_config;

        if (cfg.api_key.empty()) {
            throw std::runtime_error("LLM API key not configured");
        }

        // 将JSON转换为UTF-8字符串并保持变量存活
        std::string request_json = request_body.dump();
        LOG_INFO("=== LLM API Request ===");
        LOG_INFO("URL: {}", cfg.api_url);
        LOG_INFO("Model: {}", cfg.model);
        LOG_INFO("Request Body:\n{}", request_body.dump(2));

        const std::vector<std::string> headers = {
            "Content-Type: application/json; charset=utf-8",
            "Authorization: Bearer " + cfg.api_key
        };
        common::HttpRequestOptions options;
        options.timeout_seconds = cfg.timeout_seconds;
        options.verify_ssl = false;

        const common::HttpResponse response = common::HttpClient::Post(
            cfg.api_url, request_json, headers, options);
        if (!response.TransportSucceeded()) {
            throw std::runtime_error(
                "CURL request failed: " + response.error_message);
        }

        LOG_INFO("=== LLM API Response ===");
        LOG_INFO("HTTP Status: {}", response.status_code);
        LOG_INFO("Response Body:\n{}", response.body);

        if (response.status_code != 200) {
            LOG_ERROR("LLM API returned HTTP {}: {}",
                      response.status_code, response.body);
            throw std::runtime_error(
                "LLM API request failed with HTTP " +
                std::to_string(response.status_code));
        }

        return response.body;
    }

    nlohmann::json BuildRequestBody(const std::vector<Message>& messages,
                                    int max_tokens_override = -1) {
        auto& cfg = common::Config::Instance().llm_config;
        const int request_max_tokens = max_tokens_override > 0
            ? max_tokens_override
            : cfg.max_tokens;

        nlohmann::json request;

        // Token Pony API requires model_config with model name
        nlohmann::json model_config;
        model_config["name"] = cfg.model;  // 模型名称放在 model_config.name
        model_config["temperature"] = cfg.temperature;
        model_config["max_tokens"] = request_max_tokens;
        request["model_config"] = model_config;

        // 同时保留顶层的model字段以兼容其他API
        request["model"] = cfg.model;
        request["temperature"] = cfg.temperature;
        request["max_tokens"] = request_max_tokens;

        nlohmann::json messages_array = nlohmann::json::array();
        for (const auto& msg : messages) {
            nlohmann::json msg_obj;
            msg_obj["role"] = msg.role;
            msg_obj["content"] = msg.content;
            messages_array.push_back(msg_obj);
        }
        request["messages"] = messages_array;

        return request;
    }

    std::string ExtractResponse(const std::string& response_str) {
        try {
            auto response_json = nlohmann::json::parse(response_str);

            if (response_json.contains("choices") && !response_json["choices"].empty()) {
                auto& choice = response_json["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content")) {
                    return choice["message"]["content"].get<std::string>();
                }
            }

            throw std::runtime_error("Invalid response format from LLM API");

        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("Failed to parse LLM response: " + std::string(e.what()));
        }
    }

};

LLMClient::LLMClient()
    : pimpl_(std::make_unique<LLMClientImpl>()) {
}

LLMClient::~LLMClient() = default;

std::string LLMClient::SendMessage(const std::string& message,
                                   const std::string& system_prompt,
                                   int max_tokens_override) {
    std::vector<Message> messages;

    if (!system_prompt.empty()) {
        messages.push_back({"system", system_prompt});
    }
    messages.push_back({"user", message});

    return SendConversation(messages, max_tokens_override);
}

std::string LLMClient::SendConversation(const std::vector<Message>& messages,
                                        int max_tokens_override) {
    auto request_body = pimpl_->BuildRequestBody(
        messages, max_tokens_override);

    LOG_INFO("Sending request to LLM API...");
    std::string response_str = pimpl_->CallAPI(request_body);

    std::string content = pimpl_->ExtractResponse(response_str);

    LOG_INFO("=== LLM Extracted Content ===");
    LOG_INFO("Content:\n{}", content);

    return content;
}

nlohmann::json LLMClient::GenerateQuestionsFromResume(const std::string& resume_text, int min_questions) {
    std::string system_prompt = R"(你是一位资深的C++技术面试官。请仔细阅读候选人的简历，并根据简历内容生成至少)" +
                               std::to_string(min_questions) + R"(个技术面试问题。

要求：
1. 问题必须与候选人简历中的项目经验、技术栈紧密相关
2. 难度分布：30%基础题、50%中级题、20%高级题
3. 覆盖C++核心知识：语法、内存管理、面向对象、STL、多线程、性能优化等
4. 每个问题都要有明确的考察点
5. 问题要具体、可量化评估

请以JSON数组格式返回问题列表，每个问题包含以下字段：
- question: 问题内容
- category: 类别（如"内存管理"、"多线程"等）
- level: 难度（basic/intermediate/advanced）
- key_points: 关键考察点数组
- expected_keywords: 期望答案中包含的关键词数组

示例格式：
[
  {
    "question": "在你的XX项目中，如何管理内存以避免内存泄漏？",
    "category": "内存管理",
    "level": "intermediate",
    "key_points": ["智能指针", "RAII", "资源管理"],
    "expected_keywords": ["shared_ptr", "unique_ptr", "RAII", "析构函数"]
  }
])";

    std::string user_prompt = "候选人简历内容：\n\n" + resume_text +
                             "\n\n请根据以上简历生成" + std::to_string(min_questions) +
                             "个面试问题，以JSON格式返回。";

    LOG_INFO(">>> Calling LLM: GenerateQuestionsFromResume");
    LOG_INFO("Resume text length: {} characters", resume_text.length());
    LOG_INFO("Requested questions: {}", min_questions);

    std::string response = SendMessage(user_prompt, system_prompt, 1536);

    try {
        // 使用utils中的JSON解析函数
        auto questions = common::ParseJSONFromResponse(response, '[', ']');

        if (questions.empty() || !questions.is_array()) {
            LOG_ERROR("Parsed JSON is not a valid array or is empty");
            throw std::runtime_error("Invalid questions array format");
        }

        LOG_INFO("Generated {} questions from resume", questions.size());
        return questions;

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JSON parse error: {}", e.what());
        LOG_ERROR("Response that failed to parse: {}", response);
        throw std::runtime_error("Failed to parse questions JSON: " + std::string(e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse questions JSON: {}", e.what());
        throw std::runtime_error("Failed to generate questions from resume: " + std::string(e.what()));
    }
}

nlohmann::json LLMClient::EvaluateAnswer(
    const std::string& question,
    const std::string& answer,
    const std::string& reference_context) {
    std::string system_prompt = R"(你是一位资深的C++技术面试官，以严格、客观的标准评估候选人的回答。

评分标准（0-100分）- 请严格执行：
- 90-100分：回答准确、深入、全面，展现了深厚的技术功底和实战经验
- 70-89分：回答正确且清晰，涵盖主要知识点，但细节深度不够
- 50-69分：基本理解核心概念，但有明显遗漏、不够准确或深度不足
- 30-49分：理解片面，答案不完整，存在明显错误
- 0-29分：答非所问、完全错误或根本不理解

追问判断标准（追问应有针对性，避免过度追问）：
- 分数60-80分：回答有一定理解但深度不够，可通过1次追问考察细节
- 分数<60分：理解不足，无需追问，直接进入下一题
- 分数>80分：回答优秀，无需追问
- 特别注意：如果候选人回答明显不靠谱、胡说八道或敷衍，请给予低分（<50）且不追问

评分原则：
1. 对于错误或不相关的回答，请大胆给予低分
2. 不要因为回答了"一点东西"就给及格分
3. 评估要看答案的准确性、完整性和深度
4. 追问不是必须的，只在有价值时才追问

请以JSON格式返回评估结果，包含以下字段：
- score: 分数（0-100）
- feedback: 简短反馈（1-2句话，要客观真实）
- need_followup: 是否需要追问（true/false）
- followup_question: 追问的问题（如果need_followup为true）
- strengths: 回答的优点数组
- weaknesses: 回答的不足数组

示例1（优秀回答，不追问）：
{
  "score": 88,
  "feedback": "回答准确全面，展现了对智能指针的深入理解，包括引用计数和线程安全等关键点",
  "need_followup": false,
  "strengths": ["理解了智能指针的原理", "提到了引用计数实现", "了解线程安全问题"],
  "weaknesses": ["可以进一步说明weak_ptr的应用场景"]
}

示例2（中等回答，需追问）：
{
  "score": 72,
  "feedback": "理解了基本概念，但对底层实现细节掌握不够深入",
  "need_followup": true,
  "followup_question": "shared_ptr的引用计数在多线程环境下是如何保证线程安全的？",
  "strengths": ["理解了智能指针的基本用法", "提到了RAII原则"],
  "weaknesses": ["没有说明引用计数的实现", "未提及线程安全问题"]
}

示例3（差劲回答，不追问）：
{
  "score": 35,
  "feedback": "回答不准确，对智能指针的理解存在明显错误，建议系统学习",
  "need_followup": false,
  "strengths": [],
  "weaknesses": ["概念混淆", "答非所问", "没有抓住问题重点"]
})";

    std::string user_prompt = "问题：" + question +
                             "\n\n候选人回答：" + answer;
    if (!reference_context.empty()) {
        user_prompt +=
            "\n\n以下内容来自当前启用的知识库，请优先把其中的技术要点、"
            "岗位要求和评分标准作为评价依据；如果材料不完整或与问题无关，"
            "请结合专业知识客观评分，不要臆造材料中不存在的要求：\n"
            "----- 知识库参考资料 -----\n" +
            reference_context +
            "\n----- 参考资料结束 -----";
    }
    user_prompt +=
        "\n\n请评估这个回答，判断是否需要追问，并以JSON格式返回结果。";

    LOG_INFO(">>> Calling LLM: EvaluateAnswer");
    LOG_INFO("Question: {}", question);
    LOG_INFO("Answer: {}", answer);

    // Evaluation returns a small JSON object. A bounded completion prevents
    // long hidden reasoning from extending the pause between interview turns.
    std::string response = SendMessage(user_prompt, system_prompt, 2048);

    try {
        // 使用utils中的JSON解析函数
        auto evaluation = common::ParseJSONFromResponse(response);

        LOG_INFO("<<< Evaluation Result - Score: {}", evaluation["score"].get<int>());
        return evaluation;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse evaluation JSON: {}", e.what());
        // 返回默认评估
        nlohmann::json default_eval;
        default_eval["score"] = 50;
        default_eval["feedback"] = "评估失败，给予中等分数";
        default_eval["strengths"] = nlohmann::json::array();
        default_eval["weaknesses"] = nlohmann::json::array();
        default_eval["suggestions"] = nlohmann::json::array();
        return default_eval;
    }
}

nlohmann::json LLMClient::GenerateSummary(const nlohmann::json& interview_records,
                                          const std::string& resume_text) {
    std::string system_prompt = R"(你是一位资深的C++技术面试官。请根据面试记录生成全面的面试总结和建议。

总结要求：
1. 综合评价候选人的技术水平
2. 指出技术优势和薄弱环节
3. 给出具体的学习建议
4. 评估是否推荐录用

请以JSON格式返回总结，包含以下字段：
- overall_score: 总体分数（0-100）
- summary: 总体评价（2-3段文字）
- strengths: 技术优势列表
- weaknesses: 薄弱环节列表
- suggestions: 具体学习建议列表
- recommendation: 录用建议（"强烈推荐"/"推荐"/"待考虑"/"不推荐"）
- hiring_level: 建议级别（如"高级C++工程师"/"中级C++工程师"等）

示例格式：
{
  "overall_score": 75,
  "summary": "候选人具备扎实的C++基础...",
  "strengths": ["内存管理理解深入", "熟悉STL容器"],
  "weaknesses": ["多线程经验不足", "性能优化意识薄弱"],
  "suggestions": ["系统学习C++11/14/17新特性", "加强多线程编程实践"],
  "recommendation": "推荐",
  "hiring_level": "中级C++工程师"
})";

    std::string user_prompt = "面试记录：\n" + interview_records.dump(2);

    if (!resume_text.empty()) {
        user_prompt +=
            "\n\n候选人的简历已经用于生成本次面试问题。总结应以实际问答记录为主，"
            "无需复述简历内容。";
    }

    user_prompt += "\n\n请生成完整的面试总结和建议，以JSON格式返回。";

    LOG_INFO(">>> Calling LLM: GenerateSummary");
    LOG_INFO("Interview records: {} questions", interview_records.size());

    std::string response = SendMessage(user_prompt, system_prompt, 2560);

    try {
        // 使用utils中的JSON解析函数
        auto summary = common::ParseJSONFromResponse(response);

        LOG_INFO("<<< Summary generated - Overall score: {}",
                 summary.value("overall_score", 0));
        return summary;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse summary JSON: {}", e.what());
        throw std::runtime_error("Failed to generate summary: " + std::string(e.what()));
    }
}

// ============================================================
// RAG 对话接口（第四步新增）
// ============================================================

std::string LLMClient::ChatWithRAG(const std::string& system_prompt,
                                    const std::string& user_prompt) {
    std::vector<Message> messages;

    // 1. System Prompt（含 RAG 知识库上下文）
    messages.push_back({"system", system_prompt});

    // 2. 对话历史（最近 N 轮）
    int start_idx = 0;
    int history_size = static_cast<int>(pimpl_->history_.size());
    if (history_size > pimpl_->max_history_turns_) {
        start_idx = history_size - pimpl_->max_history_turns_;
    }
    for (int i = start_idx; i < history_size; i++) {
        messages.push_back({"user", pimpl_->history_[i].user});
        messages.push_back({"assistant", pimpl_->history_[i].assistant});
    }

    // 3. 当前用户问题
    messages.push_back({"user", user_prompt});

    LOG_INFO(">>> RAG Chat: history_turns={}, total_messages={}",
             history_size, messages.size());

    // 4. 调用 LLM
    std::string response = SendConversation(messages);

    // 5. 保存本轮对话到历史
    pimpl_->history_.push_back({user_prompt, response});

    // 6. 裁剪历史（保留最近 max_history_turns 轮）
    while (static_cast<int>(pimpl_->history_.size()) > pimpl_->max_history_turns_) {
        pimpl_->history_.erase(pimpl_->history_.begin());
    }

    return response;
}

void LLMClient::ClearConversationHistory() {
    pimpl_->history_.clear();
    LOG_INFO("Conversation history cleared");
}

void LLMClient::SetMaxHistoryTurns(int max_turns) {
    pimpl_->max_history_turns_ = max_turns > 0 ? max_turns : 5;
    LOG_INFO("Max history turns set to {}", pimpl_->max_history_turns_);
}

} // namespace services
} // namespace interview
