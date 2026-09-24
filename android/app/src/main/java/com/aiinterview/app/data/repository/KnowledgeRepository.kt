package com.aiinterview.app.data.repository

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.aiinterview.app.core.network.RagApi
import com.aiinterview.app.data.remote.CreateKnowledgeBaseRequest
import com.aiinterview.app.data.remote.DocumentDto
import com.aiinterview.app.data.remote.KnowledgeBaseDto
import com.aiinterview.app.domain.model.DocumentStatus
import com.aiinterview.app.domain.model.KnowledgeBase
import com.aiinterview.app.domain.model.KnowledgeDocument
import dagger.hilt.android.qualifiers.ApplicationContext
import java.io.FileNotFoundException
import javax.inject.Inject
import javax.inject.Singleton
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.MultipartBody
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okio.BufferedSink

@Singleton
class KnowledgeRepository @Inject constructor(
    private val api: RagApi,
    @ApplicationContext private val context: Context,
) {
    suspend fun listKnowledgeBases(): List<KnowledgeBase> =
        api.listKnowledgeBases().items.map(KnowledgeBaseDto::toDomain)

    suspend fun createKnowledgeBase(name: String, description: String): KnowledgeBase =
        api.createKnowledgeBase(
            CreateKnowledgeBaseRequest(name = name.trim(), description = description.trim()),
        ).knowledgeBase.toDomain()

    suspend fun deleteKnowledgeBase(id: String) {
        val response = api.deleteKnowledgeBase(id)
        check(response.isSuccessful) { "删除知识库失败：HTTP ${response.code()}" }
    }

    suspend fun listDocuments(knowledgeBaseId: String): List<KnowledgeDocument> =
        api.listDocuments(knowledgeBaseId).items.map(DocumentDto::toDomain)

    suspend fun uploadDocument(knowledgeBaseId: String, uri: Uri): KnowledgeDocument {
        val resolver = context.contentResolver
        val fileName = resolver.displayName(uri) ?: "document"
        val mimeType = resolver.getType(uri) ?: "application/octet-stream"
        val requestBody = ContentUriRequestBody(resolver, uri, mimeType)
        val filePart = MultipartBody.Part.createFormData("file", fileName, requestBody)
        return api.uploadDocument(
            knowledgeBaseId = knowledgeBaseId,
            file = filePart,
            displayName = fileName.toRequestBody("text/plain".toMediaTypeOrNull()),
        ).document.toDomain()
    }

    suspend fun deleteDocument(knowledgeBaseId: String, documentId: String) {
        val response = api.deleteDocument(knowledgeBaseId, documentId)
        check(response.isSuccessful) { "删除文档失败：HTTP ${response.code()}" }
    }
}

private fun KnowledgeBaseDto.toDomain() = KnowledgeBase(
    id = id,
    name = name,
    description = description,
    documentCount = documentCount,
    chunkCount = chunkCount,
)

private fun DocumentDto.toDomain() = KnowledgeDocument(
    id = id,
    fileName = fileName,
    status = when (status) {
        "queued" -> DocumentStatus.Queued
        "processing" -> DocumentStatus.Processing
        "ready" -> DocumentStatus.Ready
        "failed" -> DocumentStatus.Failed
        else -> DocumentStatus.Unknown
    },
    chunkCount = chunkCount,
    errorMessage = errorMessage,
)

private fun ContentResolver.displayName(uri: Uri): String? =
    query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
        val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
    }

private class ContentUriRequestBody(
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
            ?: throw FileNotFoundException("无法读取所选文档")
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
