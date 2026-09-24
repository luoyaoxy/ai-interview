package com.aiinterview.app.data.remote

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class CreateInterviewRequest(
    @SerialName("candidate_name") val candidateName: String,
    val position: String,
    @SerialName("question_count") val questionCount: Int = 5,
)

@Serializable
data class SubmitInterviewAnswerRequest(val answer: String)

@Serializable
data class SpeechTranscriptionResponseDto(val text: String)

@Serializable
data class InterviewEvaluationDto(
    val score: Double,
    val feedback: String,
    @SerialName("follow_up_needed") val followUpNeeded: Boolean,
    @SerialName("follow_up_question") val followUpQuestion: String? = null,
)

@Serializable
data class InterviewAnswerDto(
    val question: String,
    val answer: String,
    val evaluation: InterviewEvaluationDto,
    @SerialName("is_follow_up") val isFollowUp: Boolean,
)

@Serializable
data class InterviewReportDto(
    @SerialName("overall_score") val overallScore: Double,
    val summary: String,
    val strengths: List<String>,
    val improvements: List<String>,
    val recommendation: String,
)

@Serializable
data class InterviewDto(
    val id: String,
    @SerialName("candidate_name") val candidateName: String,
    val position: String,
    @SerialName("question_count") val questionCount: Int,
    val status: String,
    @SerialName("resume_file_name") val resumeFileName: String? = null,
    val questions: List<String>,
    @SerialName("current_question_index") val currentQuestionIndex: Int,
    @SerialName("current_question") val currentQuestion: String? = null,
    @SerialName("is_follow_up") val isFollowUp: Boolean = false,
    val answers: List<InterviewAnswerDto>,
    val report: InterviewReportDto? = null,
    @SerialName("created_at") val createdAt: String,
    @SerialName("updated_at") val updatedAt: String,
)

@Serializable
data class InterviewResponseDto(
    @SerialName("request_id") val requestId: String,
    val interview: InterviewDto,
)

@Serializable
data class InterviewPageResponseDto(
    @SerialName("request_id") val requestId: String,
    val items: List<InterviewDto>,
    val total: Int,
)

@Serializable
data class InterviewTurnResponseDto(
    @SerialName("request_id") val requestId: String,
    val interview: InterviewDto,
    val question: String? = null,
    @SerialName("question_number") val questionNumber: Int,
    @SerialName("total_questions") val totalQuestions: Int,
    @SerialName("is_follow_up") val isFollowUp: Boolean = false,
    val evaluation: InterviewEvaluationDto? = null,
)
