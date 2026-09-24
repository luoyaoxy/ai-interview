package com.aiinterview.app.ui.interview

import android.speech.SpeechRecognizer
import org.junit.Assert.assertTrue
import org.junit.Test

class InterviewVoiceControllerTest {
    @Test
    fun mapsPermissionAndNoMatchErrorsToActionableMessages() {
        assertTrue(
            speechRecognitionErrorMessage(SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS)
                .contains("权限"),
        )
        assertTrue(
            speechRecognitionErrorMessage(SpeechRecognizer.ERROR_NO_MATCH)
                .contains("清晰语音"),
        )
    }
}
