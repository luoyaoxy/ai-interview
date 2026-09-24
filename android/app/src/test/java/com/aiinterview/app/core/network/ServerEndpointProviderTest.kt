package com.aiinterview.app.core.network

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import okhttp3.HttpUrl.Companion.toHttpUrl

class ServerEndpointProviderTest {
    @Test
    fun normalizesServerUrlAndKeepsOptionalBasePath() {
        assertEquals("http://192.168.1.8:8000/", normalizeServerUrl(" http://192.168.1.8:8000 "))
        assertEquals("https://example.com/backend/", normalizeServerUrl("https://example.com/backend"))
    }

    @Test
    fun rejectsIncompleteOrCredentialBearingUrls() {
        assertThrows(IllegalArgumentException::class.java) { normalizeServerUrl("192.168.1.8:8000") }
        assertThrows(IllegalArgumentException::class.java) {
            normalizeServerUrl("https://user:password@example.com")
        }
    }

    @Test
    fun rewritesHostWhilePreservingApiPathAndQuery() {
        val result = resolveServerRequest(
            "https://example.com/backend/",
            "http://10.0.2.2:8000/api/v1/interviews?limit=20".toHttpUrl(),
        )

        assertEquals(
            "https://example.com/backend/api/v1/interviews?limit=20",
            result.toString(),
        )
    }
}
