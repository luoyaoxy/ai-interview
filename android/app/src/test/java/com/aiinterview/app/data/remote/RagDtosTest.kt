package com.aiinterview.app.data.remote

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RagDtosTest {
    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun decodesKnowledgeBasePageFromServiceContract() {
        val payload = """
            {
              "request_id": "req-1",
              "items": [{
                "id": "kb-1",
                "name": "Android",
                "description": "Interview notes",
                "role_type": "default",
                "document_count": 2,
                "chunk_count": 12,
                "created_at": "2026-09-23T12:00:00Z",
                "updated_at": "2026-09-23T12:00:00Z"
              }],
              "next_cursor": null,
              "total": 1
            }
        """.trimIndent()

        val result = json.decodeFromString<KnowledgeBasePageDto>(payload)

        assertEquals(1, result.total)
        assertEquals("kb-1", result.items.single().id)
        assertEquals(12, result.items.single().chunkCount)
    }

    @Test
    fun decodesRagAnswerAndSources() {
        val payload = """
            {
              "request_id": "req-2",
              "conversation_id": "conversation-1",
              "knowledge_found": true,
              "answer": "Use StateFlow [1]",
              "sources": [{
                "document_id": "doc-1",
                "document_name": "guide.md",
                "chunk_id": "chunk-1",
                "content": "StateFlow is observable state.",
                "score": 0.91,
                "page": 2
              }]
            }
        """.trimIndent()

        val result = json.decodeFromString<RagQueryResponseDto>(payload)

        assertTrue(result.knowledgeFound)
        assertEquals("conversation-1", result.conversationId)
        assertEquals("guide.md", result.sources.single().documentName)
    }
}
