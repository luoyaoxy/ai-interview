package com.aiinterview.app.core.network

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.aiinterview.app.BuildConfig
import dagger.hilt.android.qualifiers.ApplicationContext
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.inject.Inject
import javax.inject.Singleton
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull

@Singleton
class SecureSettingsStore @Inject constructor(
    @ApplicationContext context: Context,
) {
    private val preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    fun loadServerUrl(): String = preferences.getString(SERVER_URL, null)
        ?.let { saved -> runCatching { normalizeServerUrl(saved) }.getOrNull() }
        ?: normalizeServerUrl(BuildConfig.API_BASE_URL)

    fun saveServerUrl(value: String): String {
        val normalized = normalizeServerUrl(value)
        preferences.edit().putString(SERVER_URL, normalized).apply()
        return normalized
    }

    fun loadToken(): String? {
        val encrypted = preferences.getString(TOKEN_VALUE, null) ?: return null
        val iv = preferences.getString(TOKEN_IV, null) ?: return null
        return runCatching {
            val cipher = Cipher.getInstance(CIPHER_TRANSFORMATION)
            cipher.init(
                Cipher.DECRYPT_MODE,
                getOrCreateKey(),
                GCMParameterSpec(GCM_TAG_LENGTH_BITS, Base64.decode(iv, Base64.NO_WRAP)),
            )
            String(
                cipher.doFinal(Base64.decode(encrypted, Base64.NO_WRAP)),
                Charsets.UTF_8,
            ).takeIf(String::isNotBlank)
        }.getOrElse {
            clearToken()
            null
        }
    }

    fun saveToken(value: String?) {
        if (value.isNullOrBlank()) {
            clearToken()
            return
        }
        val cipher = Cipher.getInstance(CIPHER_TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
        val encrypted = cipher.doFinal(value.toByteArray(Charsets.UTF_8))
        preferences.edit()
            .putString(TOKEN_IV, Base64.encodeToString(cipher.iv, Base64.NO_WRAP))
            .putString(TOKEN_VALUE, Base64.encodeToString(encrypted, Base64.NO_WRAP))
            .apply()
    }

    fun clearToken() {
        preferences.edit().remove(TOKEN_IV).remove(TOKEN_VALUE).apply()
    }

    private fun getOrCreateKey(): SecretKey {
        val keyStore = KeyStore.getInstance(ANDROID_KEY_STORE).apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEY_STORE).run {
            init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .build(),
            )
            generateKey()
        }
    }

    private companion object {
        const val PREFERENCES_NAME = "secure_connection_settings"
        const val SERVER_URL = "server_url"
        const val TOKEN_IV = "token_iv"
        const val TOKEN_VALUE = "token_value"
        const val ANDROID_KEY_STORE = "AndroidKeyStore"
        const val KEY_ALIAS = "ai_interview_api_token_v1"
        const val CIPHER_TRANSFORMATION = "AES/GCM/NoPadding"
        const val GCM_TAG_LENGTH_BITS = 128
    }
}

internal fun normalizeServerUrl(value: String): String {
    val parsed = value.trim().toHttpUrlOrNull()
        ?: throw IllegalArgumentException("请输入完整的 http:// 或 https:// 服务地址")
    require(parsed.scheme == "http" || parsed.scheme == "https") {
        "服务地址只支持 HTTP 或 HTTPS"
    }
    require(parsed.username.isEmpty() && parsed.password.isEmpty()) {
        "服务地址中不能包含用户名或密码"
    }
    require(parsed.query == null && parsed.fragment == null) {
        "服务地址中不能包含查询参数或片段"
    }
    val path = parsed.encodedPath.trimEnd('/') + "/"
    return parsed.newBuilder().encodedPath(path).build().toString()
}
