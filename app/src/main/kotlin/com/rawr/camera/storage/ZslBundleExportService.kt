package com.rawr.camera.storage

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.net.toUri
import com.rawr.camera.integration.InternalTraceNative
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Keeps the Rawr process alive while a shutter-triggered ZSL snapshot is read back,
 * bundled by native code, and mirrored into the existing Rawr Artifacts collection.
 *
 * Native owns the GPU snapshot/container write. This service only waits for the
 * native ready marker and then reuses ArtifactDiagnosticExporter for MediaStore.
 */
class ZslBundleExportService : Service() {
    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val baseName = intent?.getStringExtra(EXTRA_BASE_NAME)?.takeIf { it.isNotBlank() }
        if (baseName == null) {
            stopSelf(startId)
            return START_NOT_STICKY
        }
        val dumpStatus = intent.getIntExtra(EXTRA_DUMP_STATUS, STATUS_INVALID)
        val saveLocationId =
            intent
                .getStringExtra(EXTRA_SAVE_LOCATION_ID)
                ?.takeIf { it.isNotBlank() } ?: "storage.dcim_camera"
        if (dumpStatus != STATUS_ACCEPTED) {
            ServiceCompat.startForeground(
                this,
                NOTIFICATION_ID_ACTIVE,
                notification("ZSL bundle export failed to start", dumpStatusText(dumpStatus), ongoing = false),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            )
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_DETACH)
            stopSelf(startId)
            return START_NOT_STICKY
        }

        if (!active.compareAndSet(false, true)) {
            getSystemService(NotificationManager::class.java).notify(
                NOTIFICATION_ID_RESULT,
                notification("ZSL save already in progress", "Wait for the current bundle to finish", ongoing = false)
            )
            return START_NOT_STICKY
        }

        ServiceCompat.startForeground(
            this,
            NOTIFICATION_ID_ACTIVE,
            notification("Saving ZSL bundle…", "Writing the pre-shutter ring to Rawr Artifacts", ongoing = true),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        )

        Thread({
            val exporter = ArtifactDiagnosticExporter(applicationContext)
            val result = exporter.exportZslLatest(baseName, WAIT_TIMEOUT_MS, saveLocationId)
            val success = result.success
            val publishedBytes = result.bytes
            InternalTraceNative.recordZslArtifactPublished(success, publishedBytes.coerceAtLeast(0L))
            val manager = getSystemService(NotificationManager::class.java)
            if (success) {
                manager.notify(
                    NOTIFICATION_ID_RESULT,
                    notification("ZSL bundle saved", "Saved to ${displaySaveLocation(saveLocationId)} ($baseName)", ongoing = false)
                )
            } else {
                manager.notify(
                    NOTIFICATION_ID_RESULT,
                    notification("ZSL bundle save failed", result.reason.take(120), ongoing = false)
                )
            }
            active.set(false)
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
        }, "rawr-zsl-artifact-export").start()

        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun ensureChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                getString(com.rawr.camera.R.string.notif_background_saves_name),
                NotificationManager.IMPORTANCE_DEFAULT
            ).apply {
                description = getString(com.rawr.camera.R.string.notif_background_saves_desc)
            }
        )
    }

    private fun notification(title: String, text: String, ongoing: Boolean) = NotificationCompat
        .Builder(this, CHANNEL_ID)
        .setSmallIcon(com.rawr.camera.R.drawable.ic_notification)
        .setContentTitle(title)
        .setContentText(text)
        .setOnlyAlertOnce(ongoing)
        .setOngoing(ongoing)
        .setAutoCancel(!ongoing)
        .build()

    private fun dumpStatusText(status: Int): String = when (status) {
        1 -> "ZSL ring is unavailable"
        2 -> "Another ZSL bundle export is already active"
        3 -> "ZSL ring has no completed frames"
        4 -> "ZSL frame metadata is incomplete"
        5 -> "Native ZSL export setup failed"
        else -> "Native ZSL export was not accepted"
    }

    private fun displaySaveLocation(saveLocationId: String): String = when (saveLocationId) {
        "storage.dcim_camera" -> "Internal storage / DCIM/Camera"
        "storage.pictures_raw" -> "Pictures / RAW Camera"
        else -> if (saveLocationId.startsWith("storage.tree:")) {
            runCatching {
                val uri = saveLocationId.removePrefix("storage.tree:").toUri()
                android.net.Uri.decode(uri.lastPathSegment ?: "Selected folder")
                    .substringAfterLast(':')
                    .ifBlank { "Selected folder" }
            }.getOrElse { "Selected folder" }
        } else {
            "Selected folder"
        }
    }

    companion object {
        const val EXTRA_BASE_NAME = "base_name"
        const val EXTRA_DUMP_STATUS = "dump_status"
        const val EXTRA_SAVE_LOCATION_ID = "save_location_id"
        private const val STATUS_ACCEPTED = 0
        private const val STATUS_INVALID = -1
        private const val CHANNEL_ID = "rawr_background_saves"
        private const val NOTIFICATION_ID_ACTIVE = 4101
        private const val NOTIFICATION_ID_RESULT = 4102
        private const val WAIT_TIMEOUT_MS = 120_000L
        private val active = AtomicBoolean(false)
    }
}
