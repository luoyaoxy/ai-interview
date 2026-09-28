package com.aiinterview.app.core.network

import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

@Singleton
class SessionTokenProvider @Inject constructor(
    private val settings: SecureSettingsStore,
) {
    @Volatile
    var token: String? = settings.loadToken()
        private set

    private val _configured = MutableStateFlow(token != null)
    val configured: StateFlow<Boolean> = _configured.asStateFlow()

    fun update(value: String) {
        token = value.trim().takeIf(String::isNotEmpty)
        settings.saveToken(token)
        _configured.value = token != null
    }

    fun clear() {
        token = null
        settings.clearToken()
        _configured.value = false
    }
}
