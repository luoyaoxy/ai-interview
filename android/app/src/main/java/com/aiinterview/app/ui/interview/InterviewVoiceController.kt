package com.aiinterview.app.ui.interview

import android.content.Context
import android.speech.SpeechRecognizer
import android.speech.tts.TextToSpeech
import java.util.Locale

class InterviewVoiceController(
    context: Context,
    private val onVoiceError: (String) -> Unit,
) : TextToSpeech.OnInitListener {
    private val applicationContext = context.applicationContext
    private val textToSpeech = TextToSpeech(applicationContext, this)
    private var ttsReady = false
    private var pendingSpeech: String? = null

    fun speak(text: String) {
        if (text.isBlank()) return
        if (!ttsReady) {
            pendingSpeech = text
            return
        }
        textToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null, "interview-prompt")
    }

    fun stopSpeaking() {
        pendingSpeech = null
        textToSpeech.stop()
    }

    fun close() {
        textToSpeech.stop()
        textToSpeech.shutdown()
    }

    override fun onInit(status: Int) {
        if (status != TextToSpeech.SUCCESS) {
            onVoiceError("系统语音播报初始化失败，仍可继续文字面试")
            return
        }
        val result = textToSpeech.setLanguage(Locale.SIMPLIFIED_CHINESE)
        ttsReady = result != TextToSpeech.LANG_MISSING_DATA &&
            result != TextToSpeech.LANG_NOT_SUPPORTED
        if (!ttsReady) {
            onVoiceError("系统缺少中文语音包，仍可继续文字面试")
            return
        }
        textToSpeech.setSpeechRate(0.95f)
        pendingSpeech?.let(::speak)
        pendingSpeech = null
    }

}

internal fun speechRecognitionErrorMessage(error: Int): String = when (error) {
    SpeechRecognizer.ERROR_AUDIO -> "麦克风录音失败，请重试"
    SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "没有麦克风权限，请在系统设置中授权"
    SpeechRecognizer.ERROR_NETWORK,
    SpeechRecognizer.ERROR_NETWORK_TIMEOUT,
    -> "语音识别网络不可用，请检查网络后重试"
    SpeechRecognizer.ERROR_NO_MATCH -> "没有识别到清晰语音，请靠近麦克风后重试"
    SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "语音识别服务正忙，请稍后重试"
    SpeechRecognizer.ERROR_SERVER,
    SpeechRecognizer.ERROR_SERVER_DISCONNECTED,
    -> "语音识别服务暂时不可用，请稍后重试"
    SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "未检测到语音，请点击后再开始回答"
    SpeechRecognizer.ERROR_TOO_MANY_REQUESTS -> "语音识别请求过于频繁，请稍后重试"
    else -> "语音识别失败（错误码 $error），请重试或使用文字输入"
}
