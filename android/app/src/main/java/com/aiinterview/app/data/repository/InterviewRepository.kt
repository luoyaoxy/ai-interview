package com.aiinterview.app.data.repository

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.aiinterview.app.core.network.RagApi
import com.aiinterview.app.data.remote.CreateInterviewRequest
import com.aiinterview.app.data.remote.InterviewResponseDto
import com.aiinterview.app.data.remote.InterviewTurnResponseDto
import com.aiinterview.app.data.remote.InterviewDto
import com.aiinterview.app.data.remote.SubmitInterviewAnswerRequest
import dagger.hilt.android.qualifiers.ApplicationContext
import java.io.File
import java.io.FileNotFoundException
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.MultipartBody
import okhttp3.RequestBody
import okio.BufferedSink

@Singleton
class InterviewRepository @Inject constructor(
    private val api: RagApi,
    @ApplicationContext private val context: Context,
) {
    data class ImportedResume(val uri: Uri, val displayName: String)

    suspend fun list(): List<InterviewDto> = api.listInterviews().items

    suspend fun get(interviewId: String): InterviewDto =
        api.getInterview(interviewId).interview

    suspend fun create(
        candidateName: String,
        position: String,
        questionCount: Int,
    ): InterviewResponseDto = api.createInterview(
        CreateInterviewRequest(candidateName.trim(), position.trim(), questionCount),
    )

    suspend fun uploadResume(interviewId: String, uri: Uri): InterviewResponseDto {
        val resolver = context.contentResolver
        val fileName = displayName(uri) ?: uri.lastPathSegment ?: "resume.txt"
        val mimeType = resolver.getType(uri) ?: "application/octet-stream"
        val body = ResumeRequestBody(resolver, uri, mimeType)
        return api.uploadInterviewResume(
            interviewId,
            MultipartBody.Part.createFormData("file", fileName, body),
        )
    }

    suspend fun start(interviewId: String): InterviewTurnResponseDto =
        api.startInterview(interviewId)

    suspend fun answer(interviewId: String, answer: String): InterviewTurnResponseDto =
        api.submitInterviewAnswer(interviewId, SubmitInterviewAnswerRequest(answer.trim()))

    suspend fun finish(interviewId: String): InterviewResponseDto =
        api.finishInterview(interviewId)

    suspend fun transcribe(audio: ByteArray): String = api.transcribeSpeech(
        audio.toRequestBody("audio/pcm".toMediaTypeOrNull()),
    ).text

    suspend fun importResume(uri: Uri): ImportedResume = withContext(Dispatchers.IO) {
        val resolver = context.contentResolver
        val originalName = displayName(uri) ?: fallbackName(resolver.getType(uri))
        val safeName = originalName.replace(Regex("[^A-Za-z0-9._+()\u4e00-\u9fff-]"), "_")
        val extension = safeName.substringAfterLast('.', "").lowercase()
        require(extension in SUPPORTED_RESUME_EXTENSIONS) {
            "暂不支持 .$extension 文件，请选择 PDF、DOCX、TXT、MD 或 JSON 简历"
        }
        val directory = File(context.cacheDir, "imported-resumes").apply { mkdirs() }
        val target = File(directory, "${System.currentTimeMillis()}-$safeName")
        val input = resolver.openInputStream(uri) ?: throw FileNotFoundException("无法读取所选简历")
        input.use { source ->
            target.outputStream().use { destination -> source.copyTo(destination) }
        }
        ImportedResume(Uri.fromFile(target), originalName)
    }

    fun displayName(uri: Uri): String? {
        if (uri.scheme == ContentResolver.SCHEME_FILE) return uri.lastPathSegment
        return context.contentResolver
            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
            }
    }

    private fun fallbackName(mimeType: String?): String = when (mimeType) {
        "application/pdf" -> "resume.pdf"
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document" -> "resume.docx"
        "application/json" -> "resume.json"
        "text/markdown" -> "resume.md"
        else -> "resume.txt"
    }

    private companion object {
        val SUPPORTED_RESUME_EXTENSIONS = setOf("pdf", "docx", "txt", "md", "json")
    }
}

private class ResumeRequestBody(
    private val resolver: ContentResolver,
    private val uri: Uri,
    private val mimeType: String,
) : RequestBody() {
    override fun contentType() = mimeType.toMediaTypeOrNull()

    override fun contentLength(): Long = resolver.openAssetFileDescriptor(uri, "r")?.use {
        it.length
    } ?: -1L

    override fun writeTo(sink: BufferedSink) {
        val input = resolver.openInputStream(uri)
            ?: throw FileNotFoundException("无法读取所选简历")
        input.use { source ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = source.read(buffer)
                if (count < 0) break
                sink.write(buffer, 0, count)
            }
        }
    }
}
