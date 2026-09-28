package com.aiinterview.app.data.repository

import com.aiinterview.app.domain.model.KnowledgeBase
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

@Singleton
class ActiveKnowledgeBaseStore @Inject constructor() {
    private val _active = MutableStateFlow<KnowledgeBase?>(null)
    val active: StateFlow<KnowledgeBase?> = _active.asStateFlow()

    fun select(knowledgeBase: KnowledgeBase?) {
        _active.value = knowledgeBase
    }
}
