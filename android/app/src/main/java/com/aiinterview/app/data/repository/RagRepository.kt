package com.aiinterview.app.data.repository

import com.aiinterview.app.core.network.RagApi
import com.aiinterview.app.data.remote.RagQueryRequest
import com.aiinterview.app.domain.model.RagAnswer
import com.aiinterview.app.domain.model.RagSource
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class RagRepository @Inject constructor(
    private val api: RagApi,
) {
    suspend fun query(
        question: String,
        knowledgeBaseId: String,
        conversationId: String?,
    ): RagAnswer {
        val response = api.query(
            RagQueryRequest(
                question = question.trim(),
                knowledgeBaseId = knowledgeBaseId,
                conversationId = conversationId,
            ),
        )
        return RagAnswer(
            conversationId = response.conversationId,
            knowledgeFound = response.knowledgeFound,
            answer = response.answer,
            sources = response.sources.map {
                RagSource(
                    documentName = it.documentName,
                    content = it.content,
                    score = it.score,
                    page = it.page,
                )
            },
        )
    }

    suspend fun clearConversation(conversationId: String) {
        val response = api.clearConversation(conversationId)
        check(response.isSuccessful) { "清空会话失败：HTTP ${response.code()}" }
    }
}
