package com.aiinterview.app.core.network

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import retrofit2.http.GET

interface HealthApi {
    @GET("health")
    suspend fun getHealth(): HealthResponse
}

@Serializable
data class HealthResponse(
    val status: String,
    @SerialName("service") val serviceName: String? = null,
    val version: String? = null,
)
