package com.rawr.camera.renderer

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.rawr.camera.ui.CaptureTheme

class RendererActivity : ComponentActivity() {
    private val viewModel: RendererViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (savedInstanceState == null) extractIncomingDngUri(intent)?.let(viewModel::import)
        setContent { CaptureTheme { RendererRoute(viewModel, onExit = ::finish) } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        extractIncomingDngUri(intent)?.let(viewModel::import)
    }

    override fun onStart() { super.onStart(); viewModel.onVisible() }
    override fun onStop() { viewModel.onHidden(); super.onStop() }
}

/** Single incoming DNG: share sheet (ACTION_SEND via EXTRA_STREAM) or
 * an external editor request such as Google Photos "Edit in" (ACTION_EDIT via data). */
internal fun extractIncomingDngUri(intent: Intent?): Uri? {
    val uri =
        when (intent?.action) {
            Intent.ACTION_SEND -> intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
            Intent.ACTION_EDIT -> intent.data
            else -> return null
        } ?: return null
    when (intent.type?.substringBefore(';')?.trim()?.lowercase()) {
        "image/x-adobe-dng", "image/dng", "image/x-dng" -> Unit
        else -> return null
    }
    return uri
}
