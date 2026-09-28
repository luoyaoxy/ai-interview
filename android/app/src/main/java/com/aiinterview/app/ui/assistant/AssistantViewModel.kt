package com.aiinterview.app.ui.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aiinterview.app.core.network.SessionTokenProvider
import com.aiinterview.app.core.network.toUserMessage
import com.aiinterview.app.data.repository.ActiveKnowledgeBaseStore
import com.aiinterview.app.data.repository.RagRepository
import com.aiinterview.app.domain.model.KnowledgeBase
import com.aiinterview.app.domain.model.RagSource
import dagger.hilt.android.lifecycle.HiltViewModel
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class MessageAuthor { User, Assistant }

data class ChatMessage(
    val id: Long,
    val author: MessageAuthor,
    val text: String,
    val sources: List<RagSource> = emptyList(),
)

data class AssistantUiState(
    val activeKnowledgeBase: KnowledgeBase? = null,
    val tokenConfigured: Boolean = false,
    val messages: List<ChatMessage> = emptyList(),
    val isSending: Boolean = false,
    val error: String? = null,
)

@HiltViewModel
class AssistantViewModel @Inject constructor(
    private val repository: RagRepository,
    private val activeStore: ActiveKnowledgeBaseStore,
    tokenProvider: SessionTokenProvider,
) : ViewModel() {
    private val _uiState = MutableStateFlow(AssistantUiState())
    val uiState: StateFlow<AssistantUiState> = _uiState.asStateFlow()
    private var conversationId: String? = null
    private var nextMessageId = 0L

    init {
        viewModelScope.launch {
            tokenProvider.configured.collectLatest { configured ->
                _uiState.update { it.copy(tokenConfigured = configured) }
            }
        }
        viewModelScope.launch {
            activeStore.active.collectLatest { knowledgeBase ->
                if (knowledgeBase?.id != _uiState.value.activeKnowledgeBase?.id) {
                    val previousConversationId = conversationId
                    conversationId = null
                    if (previousConversationId != null) {
                        runCatching { repository.clearConversation(previousConversationId) }
                    }
                    _uiState.update {
                        it.copy(activeKnowledgeBase = knowledgeBase, messages = emptyList(), error = null)
                    }
                }
            }
        }
    }

    fun send(question: String) {
        val knowledgeBase = _uiState.value.activeKnowledgeBase
        if (knowledgeBase == null) {
            _uiState.update { it.copy(error = "请先在知识库页面选择一个知识库") }
            return
        }
        if (!_uiState.value.tokenConfigured) {
            _uiState.update { it.copy(error = "请先在知识库页面配置访问令牌") }
            return
        }
        if (question.isBlank() || _uiState.value.isSending) return

        val userMessage = ChatMessage(++nextMessageId, MessageAuthor.User, question.trim())
        _uiState.update {
            it.copy(messages = it.messages + userMessage, isSending = true, error = null)
        }
        viewModelScope.launch {
            runCatching {
                repository.query(question, knowledgeBase.id, conversationId)
            }.onSuccess { answer ->
                conversationId = answer.conversationId
                val assistantMessage = ChatMessage(
                    id = ++nextMessageId,
                    author = MessageAuthor.Assistant,
                    text = answer.answer,
                    sources = answer.sources,
                )
                _uiState.update {
                    it.copy(messages = it.messages + assistantMessage, isSending = false)
                }
            }.onFailure { throwable ->
                _uiState.update {
                    it.copy(isSending = false, error = throwable.toUserMessage())
                }
            }
        }
    }

    fun clearConversation() {
        val currentId = conversationId
        conversationId = null
        _uiState.update { it.copy(messages = emptyList(), error = null) }
        if (currentId != null) {
            viewModelScope.launch { runCatching { repository.clearConversation(currentId) } }
        }
    }
}
