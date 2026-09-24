package com.aiinterview.app.core.network

import com.aiinterview.app.data.remote.CreateKnowledgeBaseRequest
import com.aiinterview.app.data.remote.CreateInterviewRequest
import com.aiinterview.app.data.remote.DocumentPageDto
import com.aiinterview.app.data.remote.DocumentResponseDto
import com.aiinterview.app.data.remote.KnowledgeBasePageDto
import com.aiinterview.app.data.remote.KnowledgeBaseResponseDto
import com.aiinterview.app.data.remote.InterviewResponseDto
import com.aiinterview.app.data.remote.InterviewPageResponseDto
import com.aiinterview.app.data.remote.InterviewTurnResponseDto
import com.aiinterview.app.data.remote.RagQueryRequest
import com.aiinterview.app.data.remote.RagQueryResponseDto
import com.aiinterview.app.data.remote.SubmitInterviewAnswerRequest
import com.aiinterview.app.data.remote.SpeechTranscriptionResponseDto
import okhttp3.MultipartBody
import okhttp3.RequestBody
import retrofit2.Response
import retrofit2.http.Body
import retrofit2.http.DELETE
import retrofit2.http.GET
import retrofit2.http.Headers
import retrofit2.http.Multipart
import retrofit2.http.POST
import retrofit2.http.Part
import retrofit2.http.Path
import retrofit2.http.Query

interface RagApi {
    @Headers("Content-Type: audio/pcm")
    @POST("api/v1/speech/transcribe")
    suspend fun transcribeSpeech(@Body audio: RequestBody): SpeechTranscriptionResponseDto

    @GET("api/v1/interviews")
    suspend fun listInterviews(
        @Query("limit") limit: Int = 50,
    ): InterviewPageResponseDto

    @GET("api/v1/interviews/{interviewId}")
    suspend fun getInterview(
        @Path("interviewId") interviewId: String,
    ): InterviewResponseDto

    @POST("api/v1/interviews")
    suspend fun createInterview(
        @Body request: CreateInterviewRequest,
    ): InterviewResponseDto

    @Multipart
    @POST("api/v1/interviews/{interviewId}/resume")
    suspend fun uploadInterviewResume(
        @Path("interviewId") interviewId: String,
        @Part file: MultipartBody.Part,
    ): InterviewResponseDto

    @POST("api/v1/interviews/{interviewId}/start")
    suspend fun startInterview(
        @Path("interviewId") interviewId: String,
    ): InterviewTurnResponseDto

    @POST("api/v1/interviews/{interviewId}/answers")
    suspend fun submitInterviewAnswer(
        @Path("interviewId") interviewId: String,
        @Body request: SubmitInterviewAnswerRequest,
    ): InterviewTurnResponseDto

    @POST("api/v1/interviews/{interviewId}/finish")
    suspend fun finishInterview(
        @Path("interviewId") interviewId: String,
    ): InterviewResponseDto

    @GET("api/v1/knowledge-bases")
    suspend fun listKnowledgeBases(
        @Query("page_size") pageSize: Int = 100,
    ): KnowledgeBasePageDto

    @POST("api/v1/knowledge-bases")
    suspend fun createKnowledgeBase(
        @Body request: CreateKnowledgeBaseRequest,
    ): KnowledgeBaseResponseDto

    @DELETE("api/v1/knowledge-bases/{knowledgeBaseId}")
    suspend fun deleteKnowledgeBase(
        @Path("knowledgeBaseId") knowledgeBaseId: String,
    ): Response<Unit>

    @GET("api/v1/knowledge-bases/{knowledgeBaseId}/documents")
    suspend fun listDocuments(
        @Path("knowledgeBaseId") knowledgeBaseId: String,
        @Query("page_size") pageSize: Int = 100,
    ): DocumentPageDto

    @Multipart
    @POST("api/v1/knowledge-bases/{knowledgeBaseId}/documents")
    suspend fun uploadDocument(
        @Path("knowledgeBaseId") knowledgeBaseId: String,
        @Part file: MultipartBody.Part,
        @Part("display_name") displayName: RequestBody,
    ): DocumentResponseDto

    @DELETE("api/v1/knowledge-bases/{knowledgeBaseId}/documents/{documentId}")
    suspend fun deleteDocument(
        @Path("knowledgeBaseId") knowledgeBaseId: String,
        @Path("documentId") documentId: String,
    ): Response<Unit>

    @POST("api/v1/rag/query")
    suspend fun query(@Body request: RagQueryRequest): RagQueryResponseDto

    @DELETE("api/v1/rag/conversations/{conversationId}")
    suspend fun clearConversation(
        @Path("conversationId") conversationId: String,
    ): Response<Unit>
}
