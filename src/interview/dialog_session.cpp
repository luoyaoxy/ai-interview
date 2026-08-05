/**
 * @file dialog_session.cpp
 * @brief 语音面试会话的核心协调实现。
 *
 * 主要模块：服务初始化、音频线程、实时事件处理、状态切换和 RAG 问答。
 */

#include "interview/dialog_session.h"
#include "services/audio_manager.h"
#include "services/realtime_client.h"
#include "services/rag_client.h"
#include "interview/interview_manager.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/interview_state.h"
#include "common/protocol.h"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace interview {
namespace session {

namespace {

constexpr auto kTtsInactivityTimeout = std::chrono::seconds(15);

std::int64_t SteadyClockMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void ReplaceAll(std::string& text,
                const std::string& from,
                const std::string& to) {
    if (from.empty()) {
        return;
    }

    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

// End-to-end TTS can skip punctuation-heavy programming identifiers. Keep the
// UI text unchanged, but provide a speech-friendly version to the voice model.
std::string MakeSpeechFriendlyText(std::string text) {
    const std::pair<const char*, const char*> replacements[] = {
        {"C++11", "C加加十一"},
        {"C++14", "C加加十四"},
        {"C++17", "C加加十七"},
        {"C++20", "C加加二十"},
        {"C++", "C加加"},
        {"const限定符", "常量限定符"},
        {"std::shared_ptr", "S T D shared pointer"},
        {"std::unique_ptr", "S T D unique pointer"},
        {"std::weak_ptr", "S T D weak pointer"},
        {"shared_ptr", "shared pointer"},
        {"unique_ptr", "unique pointer"},
        {"weak_ptr", "weak pointer"},
        {"make_shared", "make shared"},
        {"make_unique", "make unique"},
        {"enable_shared_from_this", "enable shared from this"},
        {"push_back", "push back"},
        {"emplace_back", "emplace back"},
        {"std::", "S T D"},
        {"RAII", "R A I I"}
    };

    for (const auto& replacement : replacements) {
        ReplaceAll(text, replacement.first, replacement.second);
    }
    return text;
}

} // namespace

class DialogSession::DialogSessionImpl {
public:
    std::unique_ptr<services::AudioDeviceManager> audio_manager;
    std::unique_ptr<services::RealtimeClient> realtime_client;
    std::shared_ptr<InterviewSession> interview_session;
    
    // 流程状态
    bool is_intro_done;
    std::atomic<bool> is_running;
    std::atomic<bool> is_playing_audio;
    std::atomic<bool> suppress_autonomous_tts;
    std::atomic<int> tts_cnt;
    std::atomic<bool> stop_after_final_summary;
    std::atomic<bool> final_summary_tts_started;
    std::atomic<bool> final_summary_tts_ended;
    std::atomic<std::int64_t> last_tts_activity_ms;
    // A prompt must not be treated as finished before its TTS_START arrives.
    std::atomic<bool> prompt_tts_pending;
    // Only one final ASR result may claim each candidate-answer window.
    std::atomic<bool> awaiting_candidate_answer;
    std::atomic<bool> answer_processing;
    std::atomic<std::size_t> tts_received_samples;
    std::atomic<std::size_t> tts_written_samples;
    std::atomic<unsigned int> tts_starvation_count;

    // 待处理的答案
    std::string pending_answer;
    std::mutex pending_answer_mutex;

    std::string final_summary;
    std::mutex final_summary_mutex;

    // RAG 服务（第七步新增）
    std::shared_ptr<services::RagBackend> rag_backend_;
    std::string rag_knowledge_base_id_;
    SessionMode session_mode_ = SessionMode::StructuredInterview;

    // 对话内容回调
    DialogSession::DialogContentCallback dialog_content_callback;

    std::thread microphone_thread;
    std::thread playback_thread;
    std::thread stop_thread;

    std::queue<std::vector<float>> audio_queue;
    size_t queued_audio_samples;
    std::mutex audio_queue_mutex;
    std::condition_variable audio_queue_cv;

    DialogSessionImpl(const std::string& name)
        : is_intro_done(false)
        , is_running(false)
        , is_playing_audio(false)
        , suppress_autonomous_tts(false)
        , tts_cnt(0)
        , stop_after_final_summary(false)
        , final_summary_tts_started(false)
        , final_summary_tts_ended(false)
        , last_tts_activity_ms(0)
        , prompt_tts_pending(false)
        , awaiting_candidate_answer(false)
        , answer_processing(false)
        , tts_received_samples(0)
        , tts_written_samples(0)
        , tts_starvation_count(0)
        , queued_audio_samples(0) {

        interview_session = std::make_shared<InterviewSession>(name);
        
        // 初始化全局状态机
        common::InterviewStateMachine::Instance().Reset();
    }

    ~DialogSessionImpl() {
        Stop();
    }

    void JoinStopThread() {
        if (stop_thread.joinable() &&
            stop_thread.get_id() != std::this_thread::get_id()) {
            stop_thread.join();
        }
    }

    void TransitionToState(common::InterviewState new_state) {
        common::InterviewStateMachine::Instance().SetState(new_state);
        LOG_INFO("状态: %s", common::InterviewStateMachine::GetStateName(new_state));
    }

    void Start() {
        if (is_running) {
            LOG_WARNING("Session already running");
            return;
        }

        auto& cfg = common::Config::Instance();

        // 创建音频管理器
        services::AudioConfig input_cfg;
        input_cfg.sample_rate = cfg.input_audio_config.sample_rate;
        input_cfg.channels = cfg.input_audio_config.channels;
        input_cfg.chunk = cfg.input_audio_config.chunk;

        services::AudioConfig output_cfg;
        output_cfg.sample_rate = cfg.output_audio_config.sample_rate;
        output_cfg.channels = cfg.output_audio_config.channels;
        output_cfg.chunk = cfg.output_audio_config.chunk;

        audio_manager = std::make_unique<services::AudioDeviceManager>(input_cfg, output_cfg);

        // 创建WebSocket客户端
        auto& ws_cfg = cfg.ws_config;
        realtime_client = std::make_unique<services::RealtimeClient>(ws_cfg.base_url, ws_cfg.headers);

        // 设置响应回调
        realtime_client->SetResponseCallback([this](const common::ParsedResponse& resp) {
            HandleServerResponse(resp);
        });

        // 连接到服务器
        LOG_INFO("Connecting to server...");
        TransitionToState(common::InterviewState::kConnecting);
        realtime_client->Connect();

        // 等待一小段时间让连接稳定
        std::this_thread::sleep_for(common::timing::CONNECTION_STABILIZE_DELAY);

        // 打开音频流
        LOG_INFO("Opening audio streams...");
        audio_manager->OpenInputStream();
        audio_manager->OpenOutputStream();

        is_running = true;

        // 启动播放线程
        playback_thread = std::thread([this]() {
            PlaybackThreadFunc();
        });

        // 启动麦克风线程
        microphone_thread = std::thread([this]() {
            MicrophoneThreadFunc();
        });

        is_playing_audio = true;
        
        // 发送开场白
        if (dialog_content_callback) {
            dialog_content_callback("interviewer", "你好，欢迎参加今天的面试，先做一个简单的自我介绍。", 0);
        }
        std::string intro = interview_session->GetIntroPrompt();
        SendInterviewerPrompt(intro);

        LOG_INFO("Dialog session started");
    }

    void Stop() {
        if (!is_running.exchange(false)) {
            JoinStopThread();
            return;
        }

        LOG_INFO("Stopping dialog session");

        audio_queue_cv.notify_all();

        // 等待线程结束
        if (microphone_thread.joinable()) {
            microphone_thread.join();
        }

        if (playback_thread.joinable()) {
            playback_thread.join();
        }

        // 关闭连接
        if (realtime_client) {
            realtime_client->Close();
        }

        // 清理音频
        if (audio_manager) {
            audio_manager->Cleanup();
        }

        // 保存面试报告
        if (interview_session) {
            try {
                std::string report_file = interview_session->SaveReport();
                LOG_INFO("Interview report saved: {}", report_file);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to save report: {}", e.what());
            }
        }

        if (interview_session) {
            std::string summary_copy;
            {
                std::lock_guard<std::mutex> summary_lock(final_summary_mutex);
                summary_copy = final_summary;
            }

            if (summary_copy.empty() && interview_session) {
                try {
                    summary_copy = interview_session->GenerateSummary();
                    if (!summary_copy.empty()) {
                        std::lock_guard<std::mutex> summary_lock(final_summary_mutex);
                        final_summary = summary_copy;
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to generate summary during shutdown: {}", e.what());
                }
            }

            if (!summary_copy.empty()) {
                LogMultilineBlock("[面试总结回顾]", summary_copy);
            }
        }

        LOG_INFO("Dialog session stopped");
    }

    void HandleServerResponse(const common::ParsedResponse& response) {

        if (response.message_type == "SERVER_ACK" && !response.payload_bytes.empty()) {
            // The end-to-end model automatically generates a spoken reply after
            // each user turn. Questions are controlled by the local interview
            // flow, so discard that autonomous audio and only play text-query TTS.
            if (suppress_autonomous_tts) {
                return;
            }

            // 服务器返回的PCM格式是Float32
            size_t float_count = response.payload_bytes.size() / sizeof(float);
            const float* float_data = reinterpret_cast<const float*>(response.payload_bytes.data());

            std::vector<float> audio_float(float_data, float_data + float_count);
            last_tts_activity_ms = SteadyClockMilliseconds();
            tts_received_samples += audio_float.size();

            // LOG_DEBUG("Received audio: {} bytes = {} float32 samples", response.payload_bytes.size(), float_count);

            // 加入播放队列
            {
                std::lock_guard<std::mutex> lock(audio_queue_mutex);
                queued_audio_samples += audio_float.size();
                audio_queue.push(audio_float);
            }
            audio_queue_cv.notify_one();
            return;
        }

        // 服务器事件
        if (response.message_type == "SERVER_FULL_RESPONSE") {
            // LOG_DEBUG("Received event: {} with payload size: {}", response.event, response.payload.dump().size());
            HandleEvent(response.event, response.payload);
        } else if (response.message_type == "SERVER_ERROR_RESPONSE" || response.message_type == "SERVER_ERROR") {
            LOG_ERROR("========================================");
            LOG_ERROR("[服务器错误]");
            LOG_ERROR("错误码: {}", response.code);
            LOG_ERROR("错误详情: {}", response.payload.dump(2));
            LOG_ERROR("========================================");

            // 服务器错误时，如果正在等待TTS响应，切换到空闲状态
            if (is_playing_audio && tts_cnt == 0) {
                LOG_WARNING("服务器错误导致无法播放TTS，切换到空闲状态");
                is_playing_audio = false;
                // TransitionToState(common::InterviewState::kIdle);
            }
        } else {
            LOG_WARNING("Received unexpected message type: {}", response.message_type);
        }
    }

    void HandleEvent(int event, const nlohmann::json& payload) {

        // Event TTS_START: TTS开始
        if (event == common::events::TTS_START) {
            // Some server/VAD paths can emit TTS_START more than once for the
            // same playback. Treat it as an idempotent active state instead of
            // a nesting counter, otherwise one missing TTS_END blocks forever.
            const int previous_tts_count = tts_cnt.exchange(1);
            last_tts_activity_ms = SteadyClockMilliseconds();
            prompt_tts_pending = false;
            if (previous_tts_count == 0) {
                tts_received_samples = 0;
                tts_written_samples = 0;
                tts_starvation_count = 0;
            }
            if (previous_tts_count > 0) {
                LOG_WARNING("Duplicate TTS_START received; keeping TTS active");
            }
            if (stop_after_final_summary) {
                final_summary_tts_started = true;
            }
            // 确保麦克风已静音,然后转换状态
            is_playing_audio = true;
            if (suppress_autonomous_tts) {
                LOG_INFO("Suppressing autonomous server TTS response");
            } else {
                TransitionToState(common::InterviewState::kInterviewerSpeaking);
            }
        }
        // Event TTS_END: TTS结束（但音频队列可能还有数据）
        else if (event == common::events::TTS_END) {
            const int previous_tts_count = tts_cnt.exchange(0);
            last_tts_activity_ms = SteadyClockMilliseconds();
            if (previous_tts_count > 0) {
                // 注意：不要在这里立即切换到 kIdle 状态
                // 因为音频队列中可能还有数据在播放
                // 状态切换应该在 PlaybackThreadFunc 中音频队列真正为空时处理
                LOG_DEBUG("TTS_END received; TTS marked inactive");
            } else {
                LOG_WARNING("TTS_END received while TTS was already inactive");
            }
            if (stop_after_final_summary &&
                final_summary_tts_started &&
                tts_cnt == 0) {
                final_summary_tts_ended = true;
            }
            audio_queue_cv.notify_one();
        }
        // Event USER_START_SPEAKING: 用户开始说话
        else if (event == common::events::USER_START_SPEAKING) {
            // 如果面试官正在说话，忽略此事件（可能是误触发）
            if (is_playing_audio || !awaiting_candidate_answer) {
                LOG_WARNING("[忽略] USER_START_SPEAKING during TTS playback (likely false trigger)");
                return;
            }
            
            TransitionToState(common::InterviewState::kCandidateSpeaking);
            // 清空播放队列
            {
                std::lock_guard<std::mutex> lock(audio_queue_mutex);
                while (!audio_queue.empty()) {
                    audio_queue.pop();
                }
                queued_audio_samples = 0;
            }
            tts_cnt = 0; // 强制重置计数器，立即结束TTS状态
        }
        // Event ASR_RESULT: ASR识别结果
        else if (event == common::events::ASR_RESULT && !payload.empty()) {
            // 如果面试官正在说话，忽略ASR结果（应该是静音数据的误识别）
            if (is_playing_audio || !awaiting_candidate_answer ||
                answer_processing) {
                LOG_DEBUG("[忽略] ASR_RESULT outside candidate-answer window");
                return;
            }
            
            auto results = payload.value("results", nlohmann::json::array());
            if (!results.empty()) {
                auto result = results[0];
                bool is_final = !result.value("is_interim", true);
                std::string text = result.value("text", std::string());

                if (is_final) {
                    bool expected = true;
                    if (!awaiting_candidate_answer.compare_exchange_strong(
                            expected, false)) {
                        LOG_DEBUG("[忽略] Duplicate final ASR result");
                        return;
                    }
                    answer_processing = true;

                    // The server will automatically produce its own answer for
                    // this audio turn. Keep ASR text, but mute that answer; the
                    // application will send exactly one selected question next.
                    suppress_autonomous_tts = true;

                    if (dialog_content_callback) {
                        int qidx = interview_session ? interview_session->GetCurrentQuestionIndex() + 1 : 1;
                        dialog_content_callback("candidate", text, qidx);
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(pending_answer_mutex);
                        pending_answer = std::move(text);
                    }
                    TransitionToState(common::InterviewState::kInterviewerThinking);
                } else {
                    LOG_DEBUG("[ASR临时结果]: {}", text);
                }
            } else {
                LOG_DEBUG("ASR_RESULT event received but results array is empty");
            }
        }
        // Event USER_STOP_SPEAKING: 用户说话结束
        else if (event == common::events::USER_STOP_SPEAKING) {
            // 如果面试官正在说话，忽略此事件（可能是误触发）
            if (is_playing_audio || !awaiting_candidate_answer) {
                LOG_WARNING("[忽略] USER_STOP_SPEAKING during TTS playback (likely false trigger)");
                return;
            }
            TransitionToState(common::InterviewState::kInterviewerThinking);
            LOG_INFO("[候选人说话结束]");
        }
        // Event SESSION_FINISHED/SESSION_ENDED: 会话结束
        else if (event == common::events::SESSION_FINISHED || event == common::events::SESSION_ENDED) {
            LOG_INFO("Session finished event: {}", event);
            TransitionToState(common::InterviewState::kSessionEnding);
            Stop();
            TransitionToState(common::InterviewState::kCompleted);
        }
    }

    void MicrophoneThreadFunc() {
        LOG_INFO("Microphone thread started");

        while (is_running) {
            try {
                // 读取麦克风数据（始终读取以清空缓冲区）
                auto audio_data = audio_manager->ReadAudio();

                // 准备要发送的音频数据
                std::vector<uint8_t> audio_bytes(audio_data.size() * 2);

                if (!is_playing_audio && awaiting_candidate_answer &&
                    !answer_processing) {
                    // 面试官不说话时：发送真实的麦克风录音
                    std::memcpy(audio_bytes.data(), audio_data.data(), audio_bytes.size());
                } else {
                    // 面试官说话时：发送静音数据（全零），保持音频流连续性避免服务器超时
                    std::memset(audio_bytes.data(), 0, audio_bytes.size());
                }

                // 发送到服务器（持续发送以避免 DialogAudioIdleTimeoutError）
                realtime_client->SendAudioData(audio_bytes);

                // 定期打印麦克风正在工作的日志（每5秒打印一次）
                static auto last_log_time = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time).count() >= 5) {
                    if (!is_playing_audio && awaiting_candidate_answer &&
                        !answer_processing) {
                        LOG_DEBUG("Microphone active - sending real audio data");
                    } else {
                        LOG_DEBUG("Microphone muted - sending silence data");
                    }
                    last_log_time = now;
                }

                std::this_thread::sleep_for(common::timing::AUDIO_SEND_INTERVAL);

            } catch (const std::exception& e) {
                LOG_ERROR("Microphone thread error: {}", e.what());
                std::this_thread::sleep_for(common::timing::ERROR_RETRY_DELAY);
            }
        }

        LOG_INFO("Microphone thread stopped");
    }

    void PlaybackThreadFunc() {
        LOG_INFO("Playback thread started");

        constexpr size_t kPromptPrebufferMilliseconds = 2000;
        constexpr size_t kFinalSummaryPrebufferMilliseconds = 2000;
        const auto& output_config = common::Config::Instance().output_audio_config;
        const size_t prompt_prebuffer_samples =
            static_cast<size_t>(output_config.sample_rate) *
            static_cast<size_t>(output_config.channels) *
            kPromptPrebufferMilliseconds / 1000;
        const size_t final_summary_prebuffer_samples =
            static_cast<size_t>(output_config.sample_rate) *
            static_cast<size_t>(output_config.channels) *
            kFinalSummaryPrebufferMilliseconds / 1000;
        bool playback_buffer_ready = false;

        LOG_INFO(
            "Playback prebuffer: {} ms for prompts; {} ms for final summary",
            kPromptPrebufferMilliseconds,
            kFinalSummaryPrebufferMilliseconds);

        while (is_running) {
            std::vector<float> audio_data;
            bool queue_empty = false;

            {
                std::unique_lock<std::mutex> lock(audio_queue_mutex);
                const bool queue_ready = audio_queue_cv.wait_for(lock, common::timing::AUDIO_QUEUE_WAIT, [this, &playback_buffer_ready, prompt_prebuffer_samples, final_summary_prebuffer_samples]() {
                    if (!is_running) {
                        return true;
                    }
                    if (audio_queue.empty()) {
                        return false;
                    }
                    const size_t required_samples = stop_after_final_summary
                        ? final_summary_prebuffer_samples
                        : prompt_prebuffer_samples;
                    return playback_buffer_ready ||
                           queued_audio_samples >= required_samples ||
                           (tts_cnt == 0 && !prompt_tts_pending);
                });

                if (!queue_ready && playback_buffer_ready && tts_cnt > 0) {
                    ++tts_starvation_count;
                    LOG_WARNING("TTS audio stream starved while playback was active");
                }

                if (!is_running) {
                    break;
                }

                // Recover if the service starts TTS but never sends TTS_END.
                // Audio packets refresh last_tts_activity_ms, so this only
                // fires after the stream has been completely silent/stalled.
                if (tts_cnt > 0) {
                    const auto last_activity = last_tts_activity_ms.load();
                    const auto inactive_for_ms =
                        SteadyClockMilliseconds() - last_activity;
                    if (last_activity > 0 &&
                        inactive_for_ms >=
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                kTtsInactivityTimeout)
                                .count()) {
                        LOG_WARNING(
                            "TTS timed out after {} ms without activity; recovering playback state",
                            inactive_for_ms);
                        tts_cnt = 0;
                        if (stop_after_final_summary &&
                            final_summary_tts_started) {
                            final_summary_tts_ended = true;
                        }
                    }
                }

                const size_t required_samples = stop_after_final_summary
                    ? final_summary_prebuffer_samples
                    : prompt_prebuffer_samples;
                const bool enough_audio =
                    playback_buffer_ready ||
                    queued_audio_samples >= required_samples ||
                    (tts_cnt == 0 && !prompt_tts_pending);

                if (!audio_queue.empty() && enough_audio) {
                    playback_buffer_ready = true;
                    const size_t batch_samples = queued_audio_samples;
                    audio_data = std::move(audio_queue.front());
                    audio_queue.pop();
                    audio_data.reserve(batch_samples);

                    // Merge every packet currently available into one
                    // contiguous PortAudio write. The realtime service emits
                    // many small packets; writing each one separately can
                    // produce sub-second discontinuities that do not register
                    // as PortAudio underflows.
                    while (!audio_queue.empty()) {
                        const auto& packet = audio_queue.front();
                        audio_data.insert(
                            audio_data.end(), packet.begin(), packet.end());
                        audio_queue.pop();
                    }
                    queued_audio_samples = 0;
                } else {
                    queue_empty = true;
                }
            }

            if (!audio_data.empty()) {
                try {
                    audio_manager->WriteAudio(audio_data);
                    tts_written_samples += audio_data.size();
                } catch (const std::exception& e) {
                    LOG_ERROR("Playback error: {}", e.what());
                }
            }

            if (queue_empty && tts_cnt == 0 && !prompt_tts_pending) {
                playback_buffer_ready = false;
                bool was_playing_audio = is_playing_audio.exchange(false);
                const bool opens_answer_window =
                    was_playing_audio &&
                    !answer_processing &&
                    !stop_after_final_summary;

                if (was_playing_audio) {
                    LOG_INFO(
                        "TTS playback stats: received={} samples, written={} samples, starvations={}",
                        tts_received_samples.load(),
                        tts_written_samples.load(),
                        tts_starvation_count.load());
                }

                if (opens_answer_window) {
                    LOG_INFO("[面试官说完了，请候选人回答] - 麦克风已恢复录音");
                    TransitionToState(common::InterviewState::kIdle);
                }

                // The final summary is complete only after the server has sent
                // TTS_END and every queued audio block has been written to the
                // output stream. A managed stop thread performs cleanup because
                // Stop() joins this playback thread. It remains joinable so the
                // owner can fully reclaim the old session before starting anew.
                if (stop_after_final_summary &&
                    final_summary_tts_started &&
                    final_summary_tts_ended) {
                    stop_after_final_summary = false;
                    if (audio_manager) {
                        audio_manager->DrainOutputStream();
                    }
                    LOG_INFO("Final summary playback completed; stopping dialog session");
                    TransitionToState(common::InterviewState::kSessionEnding);
                    stop_thread = std::thread([this]() {
                        Stop();
                        TransitionToState(common::InterviewState::kCompleted);
                    });
                    return;
                }

                if (opens_answer_window) {
                    awaiting_candidate_answer = true;
                }

                // 检查是否有待处理的答案
                ProcessPendingAnswer();
            }
        }

        LOG_INFO("Playback thread stopped");
    }

    bool IsRunning() const {
        return is_running;
    }

    void ProcessPendingAnswer() {
        if (!interview_session) {
            return;
        }

        std::string answer_to_process;
        {
            std::lock_guard<std::mutex> lock(pending_answer_mutex);
            if (pending_answer.empty()) {
                return;
            }
            answer_to_process = std::move(pending_answer);
            pending_answer.clear();
        }

        if (answer_to_process.empty()) {
            return;
        }

        if (session_mode_ == SessionMode::KnowledgeChat) {
            if (common::Config::Instance().IsRAGEnabled() &&
                rag_backend_ && !rag_knowledge_base_id_.empty()) {
                const auto response = rag_backend_->Ask(
                    answer_to_process,
                    rag_knowledge_base_id_,
                    "general_assistant");
                if (response.success) {
                    SendNextPrompt(response.answer);
                } else {
                    LOG_WARNING("Remote RAG query failed: {}",
                                response.error_message);
                    answer_processing = false;
                    awaiting_candidate_answer = true;
                }
            } else {
                LOG_WARNING(
                    "KnowledgeChat requested but RAG chat services are unavailable");
                answer_processing = false;
                awaiting_candidate_answer = true;
            }
            return;
        }

        // 处理开场白后的自我介绍
        if (!is_intro_done) {
            is_intro_done = true;
            std::string prompt = interview_session->GetFirstQuestion();
            SendNextPrompt(prompt);
            return;
        }

        // RAG 只为评分提供参考资料，不能替代原有面试状态机。
        std::string reference_context;
        std::vector<std::string> reference_sources;
        if (common::Config::Instance().IsRAGEnabled() &&
            rag_backend_ && !rag_knowledge_base_id_.empty()) {
            try {
                const auto& rag = common::Config::Instance().GetRAGConfig();
                const std::string question =
                    interview_session->GetCurrentQuestionText();
                if (!question.empty()) {
                    const auto search = rag_backend_->Search(
                        question,
                        rag_knowledge_base_id_,
                        rag.top_k,
                        rag.similarity_threshold);
                    if (!search.success) {
                        throw std::runtime_error(search.error_message);
                    }
                    if (search.knowledge_found) {
                        std::ostringstream context;
                        for (size_t i = 0; i < search.sources.size(); ++i) {
                            const auto& source = search.sources[i];
                            context << "[参考资料 " << (i + 1)
                                    << "，相似度 " << source.score << "]\n"
                                    << source.content << "\n\n";
                            reference_sources.push_back(source.document_name);
                        }
                        reference_context = context.str();
                        LOG_INFO(
                            "RAG scoring context prepared: {} source(s)",
                            search.sources.size());
                    }
                }
            } catch (const std::exception& e) {
                // RAG 故障时继续使用原有 DeepSeek 评分，面试不中断。
                LOG_WARNING(
                    "RAG retrieval failed; falling back to standard scoring: {}",
                    e.what());
                reference_context.clear();
                reference_sources.clear();
            }
        }

        interview_session->RecordAnswer(
            answer_to_process, reference_context, reference_sources);
        
        // 判断是否需要追问
        std::string next_prompt;
        
        if (interview_session->ShouldFollowUp()) {
            next_prompt = interview_session->GetFollowUpQuestion();
        } else if (!interview_session->IsComplete()) {
            next_prompt = interview_session->GetNextQuestion();
        } else {
            std::string summary = interview_session->GenerateSummary();
            {
                std::lock_guard<std::mutex> summary_lock(final_summary_mutex);
                final_summary = summary;
            }

            // Do not use a fixed delay here: long summaries can still be
            // playing when the delay expires. The playback thread will stop
            // the session after TTS_END and after the local queue is drained.
            final_summary_tts_started = false;
            final_summary_tts_ended = false;
            stop_after_final_summary = true;
            SendNextPrompt(
                summary,
                "本次面试已经结束，完整总结已显示在界面中，感谢您的参与。");
            return;
        }

        SendNextPrompt(next_prompt);
    }
    
    void SendNextPrompt(
        const std::string& prompt,
        const std::string& speech_prompt = {}) {
        // Audio produced from this explicit text query is the one response that
        // should be audible and displayed to the candidate.
        suppress_autonomous_tts = false;
        awaiting_candidate_answer = false;
        answer_processing = false;
        is_playing_audio = true;
        int qidx = interview_session ? interview_session->GetCurrentQuestionIndex() + 1 : 1;
        if (dialog_content_callback) {
            dialog_content_callback("interviewer", prompt, qidx);
        }
        SendInterviewerPrompt(
            speech_prompt.empty() ? prompt : speech_prompt);
    }

    void SendInterviewerPrompt(const std::string& text) {
        if (!realtime_client) {
            LOG_WARNING("Realtime client not ready, cannot send prompt");
            return;
        }
        if (text.empty()) {
            LOG_WARNING("Attempted to send empty interviewer prompt");
            return;
        }
        
        // 再发送到服务器进行TTS。界面保留技术术语原文，朗读文本则
        // 规避下划线、双冒号和加号等容易被语音模型跳过的符号。
        prompt_tts_pending = true;
        last_tts_activity_ms = SteadyClockMilliseconds();
        realtime_client->SendTextQuery(MakeSpeechFriendlyText(text));
    }

    void LogMultilineBlock(const std::string& title, const std::string& text) const {
        LOG_INFO("========================================");
        LOG_INFO("{}", title);
        LOG_INFO("========================================");

        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            LOG_INFO("{}", line);
        }

        LOG_INFO("========================================");
    }
};

DialogSession::DialogSession(const std::string& candidate_name)
    : pimpl_(std::make_unique<DialogSessionImpl>(candidate_name)) {}

DialogSession::~DialogSession() = default;

void DialogSession::SetRAGBackend(
    std::shared_ptr<services::RagBackend> rag_backend,
    const std::string& knowledge_base_id) {
    pimpl_->rag_backend_ = std::move(rag_backend);
    pimpl_->rag_knowledge_base_id_ = knowledge_base_id;
    LOG_INFO("RAG backend injected into DialogSession: {}",
             pimpl_->rag_backend_->GetBackendName());
}

void DialogSession::SetSessionMode(SessionMode mode) {
    pimpl_->session_mode_ = mode;
}

void DialogSession::ConfigureResumeInterview(const std::string& resume_pdf_path, int min_questions) {
    pimpl_->interview_session->LoadQuestionsFromResume(resume_pdf_path, min_questions);
}

void DialogSession::ConfigureDefaultInterview(int min_questions) {
    pimpl_->interview_session->GenerateDefaultQuestions(min_questions);
}

void DialogSession::Start() {
    pimpl_->Start();
}

void DialogSession::SetDialogContentCallback(DialogContentCallback callback) {
    pimpl_->dialog_content_callback = std::move(callback);
}

void DialogSession::HandleServerResponse(const common::ParsedResponse& response) {
    pimpl_->HandleServerResponse(response);
}

void DialogSession::Stop() {
    pimpl_->Stop();
}

bool DialogSession::IsRunning() const {
    return pimpl_->IsRunning();
}

} // namespace session
} // namespace interview

