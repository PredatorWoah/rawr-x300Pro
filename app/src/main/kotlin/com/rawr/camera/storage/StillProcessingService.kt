package com.rawr.camera.storage

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/** Keeps accepted saves and their application owner alive until all outputs are published. */
class StillProcessingService : Service() {
    private var wakeLock: PowerManager.WakeLock? = null

    override fun onCreate() {
        super.onCreate()
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(
                CHANNEL,
                getString(com.rawr.camera.R.string.notif_photo_processing_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = getString(com.rawr.camera.R.string.notif_photo_processing_desc)
            }
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(com.rawr.camera.R.drawable.ic_notification)
            .setContentTitle("Saving photos")
            .setContentText(getString(com.rawr.camera.R.string.notif_photo_processing_desc))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .build()
        ServiceCompat.startForeground(
            this, NOTIFICATION_ID, notification,
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA or
                if (Build.VERSION.SDK_INT >= 35) ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
                else ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        )
        if (wakeLock == null) {
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "rawr:still-processing")
                .apply { acquire(10 * 60 * 1000L) }
        }
        synchronized(owners) {
            instance = this
            if (owners.isEmpty()) finishService()
        }
        return START_NOT_STICKY
    }

    private fun finishService() {
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onTimeout(startId: Int, fgsType: Int) {
        // Android's foreground-service time budget is finite. Do not restart from
        // the background or crash with a service timeout; accepted workers retain
        // their ownership and can finish while the process remains alive.
        finishService()
    }

    override fun onDestroy() {
        synchronized(owners) { if (instance === this) instance = null }
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        private const val CHANNEL = "rawr_still_processing"
        private const val NOTIFICATION_ID = 4103
        private val owners = mutableMapOf<Any, Int>()
        private var instance: StillProcessingService? = null

        fun acquire(context: Context, owner: Any) {
            synchronized(owners) {
                val needsStart = owners.isEmpty() && instance == null
                owners[owner] = (owners[owner] ?: 0) + 1
                try {
                    if (needsStart) ContextCompat.startForegroundService(context, Intent(context, StillProcessingService::class.java))
                } catch (error: RuntimeException) {
                    release(owner)
                    throw error
                }
            }
        }

        fun release(owner: Any) {
            synchronized(owners) {
                val count = owners[owner] ?: return
                if (count == 1) owners.remove(owner) else owners[owner] = count - 1
                if (owners.isEmpty()) instance?.finishService()
            }
        }
    }
}
