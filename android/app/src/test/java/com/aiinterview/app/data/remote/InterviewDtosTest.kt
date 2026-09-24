package com.aiinterview.app.data.remote

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class InterviewDtosTest {
    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun decodesCompletedInterviewTurnAndReport() {
        val payload = """
            {
              "request_id":"req-1",
              "interview":{
                "id":"iv-1","candidate_name":"Lin","position":"Android",
                "question_count":1,"status":"completed","resume_file_name":null,
                "questions":["What is StateFlow?"],"current_question_index":1,
                "answers":[],
                "report":{"overall_score":88,"summary":"Good","strengths":["Kotlin"],
                  "improvements":[],"recommendation":"Practice"},
                "created_at":"2026-09-23T00:00:00Z","updated_at":"2026-09-23T00:01:00Z"
              },
              "question":null,"question_number":1,"total_questions":1,
              "is_follow_up":false,
              "evaluation":{"score":88,"feedback":"Good","follow_up_needed":false,"follow_up_question":null}
            }
        """.trimIndent()

        val result = json.decodeFromString<InterviewTurnResponseDto>(payload)

        assertNull(result.question)
        assertEquals(88.0, result.interview.report?.overallScore ?: 0.0, 0.0)
        assertEquals("Kotlin", result.interview.report?.strengths?.single())
    }

    @Test
    fun decodesResumableInterviewHistory() {
        val payload = """
            {
              "request_id":"req-history","total":1,"items":[{
                "id":"iv-2","candidate_name":"Lin","position":"Android",
                "question_count":3,"status":"in_progress","resume_file_name":"resume.pdf",
                "questions":["Q1","Q2","Q3"],"current_question_index":1,
                "current_question":"Please expand on Q2","is_follow_up":true,
                "answers":[],"report":null,
                "created_at":"2026-09-23T00:00:00Z","updated_at":"2026-09-23T00:01:00Z"
              }]
            }
        """.trimIndent()

        val result = json.decodeFromString<InterviewPageResponseDto>(payload)

        assertEquals("Please expand on Q2", result.items.single().currentQuestion)
        assertTrue(result.items.single().isFollowUp)
    }
}
