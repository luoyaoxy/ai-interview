/**
 * @file config.cpp
 * @brief 应用配置的加载、校验与运行时访问实现。
 *
 * 主要模块：JSON 配置解析、环境变量覆盖、RAG/角色配置和会话请求生成。
 */

#include "common/config.h"
#include "common/utils.h"
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace interview {
namespace common {

WebSocketConfig::WebSocketConfig() = default;

DialogConfig::DialogConfig() = default;

LLMConfig::LLMConfig() = default;

Config& Config::Instance() {
    static Config instance;
    return instance;
}

Config::Config() = default;

namespace {

bool TryGetEnvironment(const char* variable_name, std::string& result) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, variable_name) != 0 || value == nullptr) {
        return false;
    }
    result.assign(value);
    std::free(value);
    return !result.empty();
#else
    const char* value = std::getenv(variable_name);
    if (value == nullptr || value[0] == '\0') return false;
    result.assign(value);
    return true;
#endif
}

std::string GetEnvironmentOrDefault(
    const char* variable_name,
    const std::string& default_value) {
    std::string value;
    if (TryGetEnvironment(variable_name, value)) return value;
    return default_value;
}

long GetEnvironmentLongOrDefault(
    const char* variable_name,
    long default_value) {
    std::string value;
    if (!TryGetEnvironment(variable_name, value)) return default_value;
    try {
        std::size_t parsed = 0;
        const long result = std::stol(value, &parsed);
        if (parsed != value.size()) throw std::invalid_argument("trailing data");
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(
            std::string("Invalid integer environment variable: ") + variable_name);
    }
}

bool GetEnvironmentBoolOrDefault(
    const char* variable_name,
    bool default_value) {
    std::string text;
    if (!TryGetEnvironment(variable_name, text)) return default_value;
    if (text == "1" || text == "true" || text == "TRUE") return true;
    if (text == "0" || text == "false" || text == "FALSE") return false;
    throw std::runtime_error(
        std::string("Invalid boolean environment variable: ") + variable_name);
}

template <typename T>
T Require(const nlohmann::json& obj, const char* key) {
    if (!obj.contains(key)) {
        throw std::runtime_error(std::string("Missing required configuration key: ") + key);
    }
    return obj.at(key).get<T>();
}

const nlohmann::json& RequireObject(const nlohmann::json& obj, const char* key) {
    if (!obj.contains(key)) {
        throw std::runtime_error(std::string("Missing required configuration section: ") + key);
    }
    const auto& value = obj.at(key);
    if (!value.is_object()) {
        throw std::runtime_error(std::string("Configuration section must be object: ") + key);
    }
    return value;
}

} // namespace

void Config::LoadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + path);
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse configuration file: ") + e.what());
    }

    const auto& audio = RequireObject(root, "audio");
    const auto& input = RequireObject(audio, "input");
    input_audio_config.chunk = Require<int>(input, "chunk");
    input_audio_config.channels = Require<int>(input, "channels");
    input_audio_config.sample_rate = Require<int>(input, "sample_rate");
    input_audio_config.bit_size = Require<int>(input, "bit_size");
    input_audio_config.format = Require<std::string>(input, "format");

    const auto& output = RequireObject(audio, "output");
    output_audio_config.chunk = Require<int>(output, "chunk");
    output_audio_config.channels = Require<int>(output, "channels");
    output_audio_config.sample_rate = Require<int>(output, "sample_rate");
    output_audio_config.bit_size = Require<int>(output, "bit_size");
    output_audio_config.format = Require<std::string>(output, "format");

    const auto& ws = RequireObject(root, "ws");
    ws_config.base_url = Require<std::string>(ws, "base_url");
    ws_config.headers.clear();
    const auto& headers_obj = RequireObject(ws, "headers");
    for (const auto& item : headers_obj.items()) {
        ws_config.headers[item.key()] = item.value().get<std::string>();
    }

    // Sensitive credentials are loaded from environment variables when set.
    ws_config.headers["X-Api-App-ID"] = GetEnvironmentOrDefault(
        "DOUBAO_APP_ID",
        ws_config.headers["X-Api-App-ID"]);
    ws_config.headers["X-Api-Access-Key"] = GetEnvironmentOrDefault(
        "DOUBAO_ACCESS_KEY",
        ws_config.headers["X-Api-Access-Key"]);
    ws_config.headers["X-Api-App-Key"] = GetEnvironmentOrDefault(
        "DOUBAO_APP_KEY",
        ws_config.headers["X-Api-App-Key"]);

    auto connect_id_it = ws_config.headers.find("X-Api-Connect-Id");
    if (connect_id_it == ws_config.headers.end() || connect_id_it->second.empty()) {
        ws_config.headers["X-Api-Connect-Id"] = common::GenerateUUID();
    }

    const auto& dialog = RequireObject(root, "dialog");
    dialog_config.bot_name = Require<std::string>(dialog, "bot_name");
    dialog_config.system_role = Require<std::string>(dialog, "system_role");
    dialog_config.speaking_style = Require<std::string>(dialog, "speaking_style");
    dialog_config.city = Require<std::string>(dialog, "city");
    dialog_config.strict_audit = dialog.value("strict_audit", false);
    dialog_config.audit_response = Require<std::string>(dialog, "audit_response");
    dialog_config.recv_timeout = Require<int>(dialog, "recv_timeout");
    dialog_config.input_mod = Require<std::string>(dialog, "input_mod");

    const auto& tts = RequireObject(root, "tts");
    tts_config.speaker = Require<std::string>(tts, "speaker");
    tts_config.channel = Require<int>(tts, "channel");
    tts_config.format = Require<std::string>(tts, "format");
    tts_config.sample_rate = Require<int>(tts, "sample_rate");

    const auto& asr = RequireObject(root, "asr");
    asr_config.end_smooth_window_ms = Require<int>(asr, "end_smooth_window_ms");
    asr_config.vad_silence_duration = Require<int>(asr, "vad_silence_duration");
    asr_config.vad_speech_trigger_duration = Require<int>(asr, "vad_speech_trigger_duration");

    const auto& llm = RequireObject(root, "llm");
    llm_config.api_url = Require<std::string>(llm, "api_url");
    llm_config.api_key = GetEnvironmentOrDefault(
        "DEEPSEEK_API_KEY",
        llm.value("api_key", std::string{}));
    llm_config.model = Require<std::string>(llm, "model");
    llm_config.temperature = static_cast<float>(Require<double>(llm, "temperature"));
    llm_config.max_tokens = Require<int>(llm, "max_tokens");
    llm_config.timeout_seconds = Require<int>(llm, "timeout_seconds");

    // RAG 配置（第六步新增，可选段）
    LoadRAGConfig(root);

    // 角色预设（第六步新增，可选段）
    LoadRolesConfig(root);

    // 验证配置参数有效性
    ValidateConfiguration();
}

void Config::ValidateConfiguration() const {
    // 验证音频配置
    if (input_audio_config.sample_rate <= 0 || input_audio_config.sample_rate > 192000) {
        throw std::runtime_error("Invalid input sample rate: " + std::to_string(input_audio_config.sample_rate));
    }
    if (output_audio_config.sample_rate <= 0 || output_audio_config.sample_rate > 192000) {
        throw std::runtime_error("Invalid output sample rate: " + std::to_string(output_audio_config.sample_rate));
    }
    if (input_audio_config.channels <= 0 || input_audio_config.channels > 8) {
        throw std::runtime_error("Invalid input channels: " + std::to_string(input_audio_config.channels));
    }
    if (output_audio_config.channels <= 0 || output_audio_config.channels > 8) {
        throw std::runtime_error("Invalid output channels: " + std::to_string(output_audio_config.channels));
    }
    if (input_audio_config.chunk <= 0 || input_audio_config.chunk > 65536) {
        throw std::runtime_error("Invalid input chunk size: " + std::to_string(input_audio_config.chunk));
    }
    if (output_audio_config.chunk <= 0 || output_audio_config.chunk > 65536) {
        throw std::runtime_error("Invalid output chunk size: " + std::to_string(output_audio_config.chunk));
    }

    // 验证WebSocket配置
    if (ws_config.base_url.empty()) {
        throw std::runtime_error("WebSocket base_url cannot be empty");
    }
    if (ws_config.base_url.substr(0, 6) != "wss://" && ws_config.base_url.substr(0, 5) != "ws://") {
        throw std::runtime_error("Invalid WebSocket URL scheme (must start with ws:// or wss://)");
    }

    // 验证LLM配置
    if (llm_config.api_url.empty()) {
        throw std::runtime_error("LLM API URL cannot be empty");
    }
    if (llm_config.api_key.empty()) {
        throw std::runtime_error("LLM API key cannot be empty");
    }
    if (llm_config.model.empty()) {
        throw std::runtime_error("LLM model name cannot be empty");
    }
    if (llm_config.temperature < 0.0f || llm_config.temperature > 2.0f) {
        throw std::runtime_error("LLM temperature must be between 0.0 and 2.0");
    }
    if (llm_config.max_tokens <= 0 || llm_config.max_tokens > 100000) {
        throw std::runtime_error("LLM max_tokens must be between 1 and 100000");
    }
    if (llm_config.timeout_seconds <= 0 || llm_config.timeout_seconds > 600) {
        throw std::runtime_error("LLM timeout_seconds must be between 1 and 600");
    }

    // 验证VAD配置
    if (asr_config.vad_silence_duration < 100 || asr_config.vad_silence_duration > 10000) {
        throw std::runtime_error("VAD silence duration must be between 100ms and 10000ms");
    }
    if (asr_config.vad_speech_trigger_duration < 50 || asr_config.vad_speech_trigger_duration > 5000) {
        throw std::runtime_error("VAD speech trigger duration must be between 50ms and 5000ms");
    }

    if (rag_config_.enabled) {
        if (rag_config_.use_remote_rag) {
            if (rag_config_.service_url.empty() ||
                (rag_config_.service_url.rfind("http://", 0) != 0 &&
                 rag_config_.service_url.rfind("https://", 0) != 0)) {
                throw std::runtime_error(
                    "RAG service_url must start with http:// or https://");
            }
            if (rag_config_.timeout_seconds <= 0 ||
                rag_config_.timeout_seconds > 600) {
                throw std::runtime_error(
                    "RAG timeout_seconds must be between 1 and 600");
            }
            if (rag_config_.max_retries < 0 || rag_config_.max_retries > 5) {
                throw std::runtime_error("RAG max_retries must be between 0 and 5");
            }
            if (rag_config_.retry_delay_ms < 0 ||
                rag_config_.retry_delay_ms > 30000) {
                throw std::runtime_error(
                    "RAG retry_delay_ms must be between 0 and 30000");
            }
        } else {
            if (rag_config_.embedding_api_url.empty() ||
                rag_config_.embedding_model.empty()) {
                throw std::runtime_error(
                    "Local RAG embedding configuration cannot be empty");
            }
            if (rag_config_.chunk_size <= 0 || rag_config_.chunk_overlap < 0 ||
                rag_config_.chunk_overlap >= rag_config_.chunk_size) {
                throw std::runtime_error("Invalid local RAG chunk configuration");
            }
            if (rag_config_.vector_db_path.empty()) {
                throw std::runtime_error("Local RAG vector_db_path cannot be empty");
            }
        }
        if (rag_config_.top_k <= 0) {
            throw std::runtime_error("RAG top_k must be greater than 0");
        }
        if (rag_config_.similarity_threshold < 0.0f ||
            rag_config_.similarity_threshold > 1.0f) {
            throw std::runtime_error(
                "RAG similarity_threshold must be between 0.0 and 1.0");
        }
    }
}

nlohmann::json Config::GenerateStartSessionRequest() const {
    nlohmann::json request;

    // ASR配置
    request["asr"]["extra"]["end_smooth_window_ms"] = asr_config.end_smooth_window_ms;
    request["asr"]["extra"]["vad_silence_duration"] = asr_config.vad_silence_duration;
    request["asr"]["extra"]["vad_speech_trigger_duration"] = asr_config.vad_speech_trigger_duration;

    // TTS配置
    request["tts"]["speaker"] = tts_config.speaker;
    request["tts"]["audio_config"]["channel"] = tts_config.channel;
    request["tts"]["audio_config"]["format"] = tts_config.format;
    request["tts"]["audio_config"]["sample_rate"] = tts_config.sample_rate;

    // Dialog配置
    request["dialog"]["bot_name"] = dialog_config.bot_name;
    request["dialog"]["system_role"] = dialog_config.system_role;
    request["dialog"]["speaking_style"] = dialog_config.speaking_style;
    request["dialog"]["location"]["city"] = dialog_config.city;
    request["dialog"]["extra"]["strict_audit"] = dialog_config.strict_audit;
    request["dialog"]["extra"]["audit_response"] = dialog_config.audit_response;
    request["dialog"]["extra"]["recv_timeout"] = dialog_config.recv_timeout;
    request["dialog"]["extra"]["input_mod"] = dialog_config.input_mod;

    return request;
}

// ============================================================
// RAG 配置解析（第六步新增）
// ============================================================
void Config::LoadRAGConfig(const nlohmann::json& json) {
    if (!json.contains("rag")) {
        rag_config_.enabled = false;
        return;
    }

    const auto& rag = json["rag"];
    rag_config_.service_url = GetEnvironmentOrDefault(
        "RAG_SERVICE_URL",
        rag.value("service_url", "http://127.0.0.1:8000"));
    const std::string configured_api_key = rag.value(
        "api_key", rag.value("service_api_key", std::string{}));
    rag_config_.api_key = GetEnvironmentOrDefault(
        "RAG_API_KEY",
        GetEnvironmentOrDefault("RAG_SERVICE_API_KEY", configured_api_key));
    const long configured_timeout = rag.value(
        "timeout_seconds", rag.value("service_timeout_seconds", 60L));
    rag_config_.timeout_seconds = GetEnvironmentLongOrDefault(
        "RAG_TIMEOUT_SECONDS", configured_timeout);
    rag_config_.retry_delay_ms = GetEnvironmentLongOrDefault(
        "RAG_RETRY_DELAY_MS", rag.value("retry_delay_ms", 500L));
    rag_config_.max_retries = static_cast<int>(GetEnvironmentLongOrDefault(
        "RAG_MAX_RETRIES", rag.value("max_retries", 2)));
    rag_config_.enabled = rag.value("enabled", true);
    rag_config_.verify_ssl = GetEnvironmentBoolOrDefault(
        "RAG_VERIFY_SSL",
        rag.value("verify_ssl", rag.value("service_verify_ssl", true)));
    rag_config_.use_remote_rag = GetEnvironmentBoolOrDefault(
        "RAG_USE_REMOTE",
        rag.value("use_remote_rag", true));
    rag_config_.embedding_provider = rag.value("embedding_provider", "ollama");
    rag_config_.embedding_api_url = rag.value(
        "embedding_api_url", "http://127.0.0.1:11434/api/embed");
    rag_config_.embedding_model = rag.value(
        "embedding_model", "qwen3-embedding:0.6b");
    rag_config_.vector_db_path = rag.value(
        "vector_db_path", "./knowledge_base/vectors.db");
    rag_config_.chunk_size = rag.value("chunk_size", 500);
    rag_config_.chunk_overlap = rag.value("chunk_overlap", 50);
    rag_config_.max_history_turns = rag.value("max_history_turns", 5);
    rag_config_.top_k = rag.value("top_k", 3);
    rag_config_.similarity_threshold = rag.value(
        "similarity_threshold", 0.7f);
}

// ============================================================
// 角色配置解析（第六步新增）
// ============================================================
void Config::LoadRolesConfig(const nlohmann::json& json) {
    if (!json.contains("roles")) {
        LoadBuiltInRoles();
        return;
    }

    const auto& roles = json["roles"];
    default_role_ = roles.value("default", "general_assistant");

    if (roles.contains("presets") && roles["presets"].is_object()) {
        for (const auto& [key, preset_json] : roles["presets"].items()) {
            RolePreset preset;
            preset.name = preset_json.value("name", key);
            preset.description = preset_json.value("description", "");
            preset.style = preset_json.value("style", "专业");
            preset.fallback = preset_json.value("fallback", "抱歉，无法回答。");
            role_presets_[key] = preset;
        }
    }

    // 设置活跃角色
    if (active_role_.empty()) {
        active_role_ = default_role_;
    }
}

// ============================================================
// 内置角色预设
// ============================================================
void Config::LoadBuiltInRoles() {
    default_role_ = "interviewer";

    role_presets_["interviewer"] = {
        "专业面试官",
        "你是一位资深技术面试官，负责评估候选人的技术能力和综合素质。"
        "请根据参考知识库中的岗位要求和技术标准来提问和评估。",
        "专业严谨，温和但不失锐度",
        "抱歉，我在当前知识库中没有找到相关岗位标准，请问能否提供更多信息？"
    };

    role_presets_["general_assistant"] = {
        "AI 面试助手",
        "你是一位技术面试学习助手，只根据当前启用知识库中的参考资料回答问题，"
        "帮助用户进行面试复习。不得编造知识库中不存在的内容。",
        "专业准确，简洁清晰，保留标准技术术语",
        "当前知识库中没有检索到与该问题相关的内容，请调整问题表述或上传相关技术资料后重试。"
    };

    active_role_ = default_role_;
}

// ============================================================
// 角色查询接口
// ============================================================
const RolePreset* Config::GetRolePreset(const std::string& key) const {
    auto it = role_presets_.find(key);
    if (it != role_presets_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> Config::GetRoleKeys() const {
    std::vector<std::string> keys;
    for (const auto& [key, _] : role_presets_) {
        keys.push_back(key);
    }
    return keys;
}

void Config::SetActiveRole(const std::string& role_key) {
    if (role_presets_.count(role_key)) {
        active_role_ = role_key;
    }
}

} // namespace common
} // namespace interview
