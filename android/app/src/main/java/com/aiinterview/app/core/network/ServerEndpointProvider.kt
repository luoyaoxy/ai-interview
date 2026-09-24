package com.aiinterview.app.core.network

import javax.inject.Inject
import javax.inject.Singleton
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl

@Singleton
class ServerEndpointProvider @Inject constructor(
    private val settings: SecureSettingsStore,
) {
    @Volatile
    var baseUrl: String = settings.loadServerUrl()
        private set

    fun update(value: String) {
        baseUrl = settings.saveServerUrl(value)
    }

    fun resolve(original: HttpUrl): HttpUrl {
        return resolveServerRequest(baseUrl, original)
    }
}

internal fun resolveServerRequest(baseUrl: String, original: HttpUrl): HttpUrl {
    val relative = buildString {
        append(original.encodedPath.removePrefix("/"))
        original.encodedQuery?.let {
            append('?')
            append(it)
        }
    }
    return checkNotNull(baseUrl.toHttpUrl().resolve(relative)) {
        "无法根据服务地址创建请求 URL"
    }
}
