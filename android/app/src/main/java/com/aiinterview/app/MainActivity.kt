package com.aiinterview.app

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.mutableStateOf
import com.aiinterview.app.ui.AiInterviewApp
import com.aiinterview.app.ui.theme.AiInterviewTheme
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    private val sharedResumeUri = mutableStateOf<Uri?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sharedResumeUri.value = resumeUriFrom(intent)
        enableEdgeToEdge()
        setContent {
            AiInterviewTheme {
                AiInterviewApp(
                    sharedResumeUri = sharedResumeUri.value,
                    onSharedResumeConsumed = { sharedResumeUri.value = null },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        sharedResumeUri.value = resumeUriFrom(intent)
    }

    @Suppress("DEPRECATION")
    private fun resumeUriFrom(intent: Intent?): Uri? = when (intent?.action) {
        Intent.ACTION_SEND ->
            intent.getParcelableExtra(Intent.EXTRA_STREAM)
                ?: intent.clipData?.getItemAt(0)?.uri
        Intent.ACTION_SEND_MULTIPLE ->
            intent.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)?.firstOrNull()
                ?: intent.clipData?.getItemAt(0)?.uri
        Intent.ACTION_VIEW -> intent.data ?: intent.clipData?.getItemAt(0)?.uri
        else -> null
    }
}
