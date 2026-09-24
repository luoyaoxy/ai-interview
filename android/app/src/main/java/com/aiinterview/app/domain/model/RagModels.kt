package com.aiinterview.app.domain.model

data class KnowledgeBase(
    val id: String,
    val name: String,
    val description: String,
    val documentCount: Int,
    val chunkCount: Int,
)

data class KnowledgeDocument(
    val id: String,
    val fileName: String,
    val status: DocumentStatus,
    val chunkCount: Int,
    val errorMessage: String,
)

enum class DocumentStatus {
    Queued,
    Processing,
    Ready,
    Failed,
    Unknown,
}

data class RagSource(
    val documentName: String,
    val content: String,
    val score: Double,
    val page: Int?,
)

data class RagAnswer(
    val conversationId: String,
    val knowledgeFound: Boolean,
    val answer: String,
    val sources: List<RagSource>,
)
