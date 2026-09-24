package com.aiinterview.app.ui.interview

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aiinterview.app.core.network.HealthApi
import com.aiinterview.app.core.network.SessionTokenProvider
import com.aiinterview.app.core.network.ServerEndpointProvider
import com.aiinterview.app.core.network.toUserMessage
import com.aiinterview.app.data.remote.InterviewEvaluationDto
import com.aiinterview.app.data.remote.InterviewDto
import com.aiinterview.app.data.remote.InterviewReportDto
import com.aiinterview.app.data.remote.InterviewTurnResponseDto
import com.aiinterview.app.data.repository.InterviewRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class InterviewStage { Setup, InProgress, Completed }

data class InterviewUiState(
    val stage: InterviewStage = InterviewStage.Setup,
    val tokenConfigured: Boolean = false,
    val serverUrl: String = "",
    val serverStatus: String = "未检查",
    val candidateName: String = "",
    val position: String = "C++ 工程师",
    val questionCount: String = "5",
    val resumeUri: Uri? = null,
    val resumeName: String? = null,
    val interviewId: String? = null,
    val question: String? = null,
    val questionNumber: Int = 0,
    val totalQuestions: Int = 0,
    val isFollowUp: Boolean = false,
    val answer: String = "",
    val latestEvaluation: InterviewEvaluationDto? = null,
    val report: InterviewReportDto? = null,
    val history: List<InterviewDto> = emptyList(),
    val isBusy: Boolean = false,
    val busyMessage: String? = null,
    val error: String? = null,
)

@HiltViewModel
class InterviewViewModel @Inject constructor(
    private val repository: InterviewRepository,
    private val tokenProvider: SessionTokenProvider,
    private val endpointProvider: ServerEndpointProvider,
    private val healthApi: HealthApi,
) : ViewModel() {
    private val _uiState = MutableStateFlow(
        InterviewUiState(
            tokenConfigured = tokenProvider.token != null,
            serverUrl = endpointProvider.baseUrl,
        ),
    )
    val uiState: StateFlow<InterviewUiState> = _uiState.asStateFlow()

    init {
        checkConnection()
        if (tokenProvider.token != null) refreshHistory()
    }

    fun configureConnection(serverUrl: String, token: String): Boolean = runCatching {
        endpointProvider.update(serverUrl)
        tokenProvider.update(token)
    }.fold(
        onSuccess = {
            _uiState.update {
                it.copy(
                    serverUrl = endpointProvider.baseUrl,
                    tokenConfigured = tokenProvider.token != null,
                    history = emptyList(),
                    error = null,
                )
            }
            checkConnection()
            if (tokenProvider.token != null) refreshHistory()
            true
        },
        onFailure = { error ->
            _uiState.update { it.copy(error = error.message ?: "连接配置无效") }
            false
        },
    )

    fun checkConnection() {
        _uiState.update { it.copy(serverStatus = "检查中") }
        viewModelScope.launch {
            val status = runCatching { healthApi.getHealth() }
                .fold(
                    onSuccess = { response ->
                        "${response.status} · ${response.serviceName.orEmpty()} ${response.version.orEmpty()}".trim()
                    },
                    onFailure = { error -> "连接失败：${error.toUserMessage()}" },
                )
            _uiState.update { it.copy(serverStatus = status) }
        }
    }

    fun updateCandidateName(value: String) = _uiState.update { it.copy(candidateName = value) }
    fun updatePosition(value: String) = _uiState.update { it.copy(position = value) }
    fun updateQuestionCount(value: String) = _uiState.update {
        it.copy(questionCount = value.filter(Char::isDigit).take(2))
    }
    fun updateAnswer(value: String) = _uiState.update { it.copy(answer = value) }

    fun transcribeAudio(audio: ByteArray) {
        if (audio.isEmpty()) {
            showError("没有录到声音，请重试")
            return
        }
        launchRequest("正在转写语音…") {
            val transcript = repository.transcribe(audio).trim()
            check(transcript.isNotEmpty()) { "语音服务没有返回识别文字" }
            _uiState.update { state ->
                val answer = listOf(state.answer.trim(), transcript)
                    .filter(String::isNotEmpty)
                    .joinToString("，")
                state.copy(answer = answer, busyMessage = "正在分析回答并生成下一题…")
            }
            val id = checkNotNull(_uiState.value.interviewId) { "当前没有进行中的面试" }
            val turn = repository.answer(id, _uiState.value.answer)
            applyTurn(turn)
        }
    }

    fun selectResume(uri: Uri) {
        launchRequest("正在导入简历…") {
            val imported = repository.importResume(uri)
            _uiState.update {
                it.copy(resumeUri = imported.uri, resumeName = imported.displayName)
            }
        }
    }

    fun start() {
        val state = _uiState.value
        val count = state.questionCount.toIntOrNull()
        if (state.candidateName.isBlank() || state.position.isBlank() || count !in 1..20) {
            _uiState.update { it.copy(error = "请填写姓名、目标岗位，并将题数设置为 1～20") }
            return
        }
        launchRequest {
            val created = repository.create(state.candidateName, state.position, count!!)
            state.resumeUri?.let { repository.uploadResume(created.interview.id, it) }
            val turn = repository.start(created.interview.id)
            _uiState.update {
                it.copy(
                    stage = InterviewStage.InProgress,
                    interviewId = created.interview.id,
                    question = turn.question,
                    questionNumber = turn.questionNumber,
                    totalQuestions = turn.totalQuestions,
                    isFollowUp = turn.isFollowUp,
                    latestEvaluation = null,
                    answer = "",
                )
            }
        }
    }

    fun submitAnswer() {
        val state = _uiState.value
        val id = state.interviewId ?: return
        if (state.answer.isBlank()) {
            _uiState.update { it.copy(error = "请先填写回答") }
            return
        }
        launchRequest {
            val turn = repository.answer(id, state.answer)
            applyTurn(turn)
        }
    }

    fun finishEarly() {
        val id = _uiState.value.interviewId ?: return
        launchRequest {
            val response = repository.finish(id)
            _uiState.update {
                it.copy(
                    stage = InterviewStage.Completed,
                    question = null,
                    report = response.interview.report,
                )
            }
        }
    }

    fun refreshHistory() {
        if (tokenProvider.token == null) return
        launchRequest {
            val history = repository.list()
            _uiState.update { it.copy(history = history) }
        }
    }

    fun openInterview(interviewId: String) {
        launchRequest {
            val interview = repository.get(interviewId)
            when (interview.status) {
                "created" -> {
                    val turn = repository.start(interview.id)
                    _uiState.update {
                        it.copy(
                            stage = InterviewStage.InProgress,
                            interviewId = interview.id,
                            question = turn.question,
                            questionNumber = turn.questionNumber,
                            totalQuestions = turn.totalQuestions,
                            isFollowUp = turn.isFollowUp,
                            answer = "",
                            latestEvaluation = null,
                            report = null,
                        )
                    }
                }
                "in_progress" -> {
                    checkNotNull(interview.currentQuestion) { "服务端没有返回待回答问题" }
                    _uiState.update {
                        it.copy(
                            stage = InterviewStage.InProgress,
                            candidateName = interview.candidateName,
                            position = interview.position,
                            interviewId = interview.id,
                            question = interview.currentQuestion,
                            questionNumber = minOf(
                                interview.currentQuestionIndex + 1,
                                interview.questionCount,
                            ),
                            totalQuestions = interview.questionCount,
                            isFollowUp = interview.isFollowUp,
                            answer = "",
                            latestEvaluation = interview.answers.lastOrNull()?.evaluation,
                            report = null,
                        )
                    }
                }
                "completed" -> _uiState.update {
                    it.copy(
                        stage = InterviewStage.Completed,
                        candidateName = interview.candidateName,
                        position = interview.position,
                        interviewId = interview.id,
                        question = null,
                        report = interview.report,
                    )
                }
                else -> error("未知的面试状态：${interview.status}")
            }
        }
    }

    fun reset() {
        _uiState.value = InterviewUiState(
            tokenConfigured = tokenProvider.token != null,
            serverUrl = endpointProvider.baseUrl,
        )
        refreshHistory()
    }

    fun clearError() = _uiState.update { it.copy(error = null) }

    fun showError(message: String) = _uiState.update { it.copy(error = message) }

    private fun applyTurn(turn: InterviewTurnResponseDto) {
        _uiState.update {
            it.copy(
                stage = if (turn.interview.status == "completed") {
                    InterviewStage.Completed
                } else {
                    InterviewStage.InProgress
                },
                question = turn.question,
                questionNumber = turn.questionNumber,
                totalQuestions = turn.totalQuestions,
                isFollowUp = turn.isFollowUp,
                latestEvaluation = turn.evaluation,
                report = turn.interview.report,
                answer = "",
            )
        }
    }

    private fun launchRequest(
        busyMessage: String = "处理中…",
        block: suspend () -> Unit,
    ) {
        viewModelScope.launch {
            _uiState.update { it.copy(isBusy = true, busyMessage = busyMessage, error = null) }
            runCatching { block() }
                .onSuccess { _uiState.update { it.copy(isBusy = false, busyMessage = null) } }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(isBusy = false, busyMessage = null, error = error.toUserMessage())
                    }
                }
        }
    }
}
