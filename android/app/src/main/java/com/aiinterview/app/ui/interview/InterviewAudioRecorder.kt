package com.aiinterview.app.ui.interview

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import kotlin.concurrent.thread

class InterviewAudioRecorder(
    private val onRecordingChanged: (Boolean) -> Unit,
    private val onAudioReady: (ByteArray) -> Unit,
    private val onError: (String) -> Unit,
) {
    @Volatile
    private var recording = false
    private var worker: Thread? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    fun start() {
        if (recording) return
        val minimum = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        if (minimum <= 0) {
            onError("无法初始化麦克风录音")
            return
        }
        val bufferSize = maxOf(minimum, CHUNK_BYTES)
        val recorder = try {
            AudioRecord(
                MediaRecorder.AudioSource.VOICE_RECOGNITION,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                bufferSize,
            )
        } catch (_: SecurityException) {
            onError("没有麦克风权限，请在系统设置中授权")
            return
        }
        if (recorder.state != AudioRecord.STATE_INITIALIZED) {
            recorder.release()
            onError("麦克风初始化失败，请关闭其他录音应用后重试")
            return
        }
        recording = true
        onRecordingChanged(true)
        worker = thread(name = "interview-audio-recorder") {
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(bufferSize)
            try {
                recorder.startRecording()
                while (recording && output.size() < MAX_AUDIO_BYTES) {
                    val count = recorder.read(buffer, 0, buffer.size)
                    if (count > 0) output.write(buffer, 0, count)
                }
            } catch (_: SecurityException) {
                mainHandler.post { onError("麦克风录音权限不可用") }
            } finally {
                runCatching { recorder.stop() }
                recorder.release()
                recording = false
                val audio = output.toByteArray()
                mainHandler.post {
                    onRecordingChanged(false)
                    if (audio.isNotEmpty()) onAudioReady(audio)
                }
            }
        }
    }

    fun stop() {
        recording = false
    }

    fun close() {
        recording = false
        worker?.join(500)
        worker = null
    }

    private companion object {
        const val SAMPLE_RATE = 16_000
        const val CHUNK_BYTES = 3_200
        const val MAX_AUDIO_BYTES = 16 * 1024 * 1024
    }
}
