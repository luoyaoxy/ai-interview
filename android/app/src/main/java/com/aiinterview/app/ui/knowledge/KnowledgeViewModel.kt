package com.aiinterview.app.ui.knowledge

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aiinterview.app.core.network.HealthApi
import com.aiinterview.app.core.network.ServerEndpointProvider
import com.aiinterview.app.core.network.SessionTokenProvider
import com.aiinterview.app.core.network.toUserMessage
import com.aiinterview.app.data.repository.ActiveKnowledgeBaseStore
import com.aiinterview.app.data.repository.KnowledgeRepository
import com.aiinterview.app.domain.model.KnowledgeBase
import com.aiinterview.app.domain.model.KnowledgeDocument
import dagger.hilt.android.lifecycle.HiltViewModel
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class KnowledgeUiState(
    val serverUrl: String = "",
    val serverStatus: String = "检查中",
    val tokenConfigured: Boolean = false,
    val knowledgeBases: List<KnowledgeBase> = emptyList(),
    val selectedKnowledgeBase: KnowledgeBase? = null,
    val documents: List<KnowledgeDocument> = emptyList(),
    val isBusy: Boolean = false,
    val message: String? = null,
    val error: String? = null,
)

@HiltViewModel
class KnowledgeViewModel @Inject constructor(
    private val healthApi: HealthApi,
    private val repository: KnowledgeRepository,
    private val tokenProvider: SessionTokenProvider,
    private val endpointProvider: ServerEndpointProvider,
    private val activeStore: ActiveKnowledgeBaseStore,
) : ViewModel() {
    private val _uiState = MutableStateFlow(
        KnowledgeUiState(
            serverUrl = endpointProvider.baseUrl,
            tokenConfigured = tokenProvider.token != null,
        ),
    )
    val uiState: StateFlow<KnowledgeUiState> = _uiState.asStateFlow()

    init {
        checkHealth()
        if (tokenProvider.token != null) refreshKnowledgeBases()
    }

    fun configureConnection(serverUrl: String, token: String): Boolean = runCatching {
        endpointProvider.update(serverUrl)
        tokenProvider.update(token)
    }.fold(
        onSuccess = {
            activeStore.select(null)
            _uiState.update {
                it.copy(
                    serverUrl = endpointProvider.baseUrl,
                    tokenConfigured = tokenProvider.token != null,
                    knowledgeBases = emptyList(),
                    selectedKnowledgeBase = null,
                    documents = emptyList(),
                    error = null,
                )
            }
            checkHealth()
            if (tokenProvider.token != null) refreshKnowledgeBases()
            true
        },
        onFailure = { error ->
            _uiState.update { it.copy(error = error.message ?: "连接配置无效") }
            false
        },
    )

    fun refreshKnowledgeBases() = launchRequest {
        val items = repository.listKnowledgeBases()
        val previousId = _uiState.value.selectedKnowledgeBase?.id
        val selected = items.firstOrNull { it.id == previousId } ?: items.firstOrNull()
        activeStore.select(selected)
        _uiState.update {
            it.copy(knowledgeBases = items, selectedKnowledgeBase = selected)
        }
        loadDocumentsInternal(selected?.id)
    }

    fun createKnowledgeBase(name: String, description: String) {
        if (name.isBlank()) {
            _uiState.update { it.copy(error = "知识库名称不能为空") }
            return
        }
        launchRequest(successMessage = "知识库创建成功") {
            val created = repository.createKnowledgeBase(name, description)
            val items = repository.listKnowledgeBases()
            activeStore.select(created)
            _uiState.update {
                it.copy(
                    knowledgeBases = items,
                    selectedKnowledgeBase = created,
                    documents = emptyList(),
                )
            }
        }
    }

    fun selectKnowledgeBase(knowledgeBase: KnowledgeBase) {
        activeStore.select(knowledgeBase)
        _uiState.update {
            it.copy(selectedKnowledgeBase = knowledgeBase, documents = emptyList())
        }
        refreshDocuments()
    }

    fun deleteSelectedKnowledgeBase() {
        val selected = _uiState.value.selectedKnowledgeBase ?: return
        launchRequest(successMessage = "知识库已删除") {
            repository.deleteKnowledgeBase(selected.id)
            val items = repository.listKnowledgeBases()
            val next = items.firstOrNull()
            activeStore.select(next)
            _uiState.update {
                it.copy(
                    knowledgeBases = items,
                    selectedKnowledgeBase = next,
                    documents = emptyList(),
                )
            }
            loadDocumentsInternal(next?.id)
        }
    }

    fun refreshDocuments() = launchRequest {
        loadDocumentsInternal(_uiState.value.selectedKnowledgeBase?.id)
    }

    fun uploadDocument(uri: Uri) {
        val selected = _uiState.value.selectedKnowledgeBase
        if (selected == null) {
            _uiState.update { it.copy(error = "请先创建或选择知识库") }
            return
        }
        launchRequest(successMessage = "文档已上传，服务端正在处理") {
            repository.uploadDocument(selected.id, uri)
            loadDocumentsInternal(selected.id)
            refreshKnowledgeBaseCounts(selected.id)
        }
    }

    fun deleteDocument(documentId: String) {
        val selected = _uiState.value.selectedKnowledgeBase ?: return
        launchRequest(successMessage = "文档已删除") {
            repository.deleteDocument(selected.id, documentId)
            loadDocumentsInternal(selected.id)
            refreshKnowledgeBaseCounts(selected.id)
        }
    }

    fun clearNotice() {
        _uiState.update { it.copy(message = null, error = null) }
    }

    private fun checkHealth() {
        viewModelScope.launch {
            val status = runCatching { healthApi.getHealth().status }
                .fold(onSuccess = { it }, onFailure = { "不可达" })
            _uiState.update { it.copy(serverStatus = status) }
        }
    }

    private suspend fun loadDocumentsInternal(knowledgeBaseId: String?) {
        val documents = knowledgeBaseId?.let { repository.listDocuments(it) }.orEmpty()
        _uiState.update { it.copy(documents = documents) }
    }

    private suspend fun refreshKnowledgeBaseCounts(selectedId: String) {
        val items = repository.listKnowledgeBases()
        val selected = items.firstOrNull { it.id == selectedId }
        activeStore.select(selected)
        _uiState.update {
            it.copy(knowledgeBases = items, selectedKnowledgeBase = selected)
        }
    }

    private fun launchRequest(
        successMessage: String? = null,
        block: suspend () -> Unit,
    ) {
        viewModelScope.launch {
            _uiState.update { it.copy(isBusy = true, message = null, error = null) }
            runCatching { block() }
                .onSuccess {
                    _uiState.update { state ->
                        state.copy(isBusy = false, message = successMessage)
                    }
                }
                .onFailure { throwable ->
                    _uiState.update { state ->
                        state.copy(isBusy = false, error = throwable.toUserMessage())
                    }
                }
        }
    }
}
