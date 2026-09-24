package com.aiinterview.app.data.remote

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class CreateKnowledgeBaseRequest(
    val name: String,
    val description: String = "",
    @SerialName("role_type") val roleType: String = "default",
)

@Serializable
data class KnowledgeBaseDto(
    val id: String,
    val name: String,
    val description: String,
    @SerialName("role_type") val roleType: String,
    @SerialName("document_count") val documentCount: Int,
    @SerialName("chunk_count") val chunkCount: Int,
    @SerialName("created_at") val createdAt: String,
    @SerialName("updated_at") val updatedAt: String,
)

@Serializable
data class KnowledgeBaseResponseDto(
    @SerialName("request_id") val requestId: String,
    @SerialName("knowledge_base") val knowledgeBase: KnowledgeBaseDto,
)

@Serializable
data class KnowledgeBasePageDto(
    @SerialName("request_id") val requestId: String,
    val items: List<KnowledgeBaseDto>,
    @SerialName("next_cursor") val nextCursor: String? = null,
    val total: Int,
)

@Serializable
data class DocumentDto(
    val id: String,
    @SerialName("knowledge_base_id") val knowledgeBaseId: String,
    @SerialName("file_name") val fileName: String,
    val status: String,
    @SerialName("chunk_count") val chunkCount: Int,
    @SerialName("error_message") val errorMessage: String,
    @SerialName("created_at") val createdAt: String,
    @SerialName("updated_at") val updatedAt: String,
)

@Serializable
data class DocumentResponseDto(
    @SerialName("request_id") val requestId: String,
    val document: DocumentDto,
)

@Serializable
data class DocumentPageDto(
    @SerialName("request_id") val requestId: String,
    val items: List<DocumentDto>,
    @SerialName("next_cursor") val nextCursor: String? = null,
    val total: Int,
)

@Serializable
data class SourceDto(
    @SerialName("document_id") val documentId: String,
    @SerialName("document_name") val documentName: String,
    @SerialName("chunk_id") val chunkId: String,
    val content: String,
    val score: Double,
    val page: Int? = null,
)

@Serializable
data class RagQueryRequest(
    val question: String,
    @SerialName("knowledge_base_id") val knowledgeBaseId: String,
    @SerialName("conversation_id") val conversationId: String? = null,
    @SerialName("role_type") val roleType: String = "general_assistant",
)

@Serializable
data class RagQueryResponseDto(
    @SerialName("request_id") val requestId: String,
    @SerialName("conversation_id") val conversationId: String,
    @SerialName("knowledge_found") val knowledgeFound: Boolean,
    val answer: String,
    val sources: List<SourceDto>,
)
