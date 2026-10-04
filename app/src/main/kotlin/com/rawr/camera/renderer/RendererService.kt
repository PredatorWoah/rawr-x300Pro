package com.rawr.camera.renderer

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.*
import android.widget.Toast
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat

class RendererService : Service() {
    private var wakeLock: PowerManager.WakeLock? = null
    override fun onBind(intent: Intent?) = null
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        channel(this)
        val notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(com.rawr.camera.R.drawable.ic_notification).setContentTitle("Rendering JPEG")
            .setContentText("Open Renderer to view progress").setOngoing(true).setOnlyAlertOnce(true)
            .setContentIntent(PendingIntent.getActivity(this, 0, Intent(this, RendererActivity::class.java), PendingIntent.FLAG_IMMUTABLE))
            .build()
        // Use the platform overload: older AndroidX masks out MEDIA_PROCESSING.
        startForeground(4201, notification,
            if (Build.VERSION.SDK_INT >= 35) ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING else ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "rawr:renderer").apply { acquire(30 * 60 * 1000L) }
        return START_NOT_STICKY
    }
    override fun onTimeout(startId: Int, fgsType: Int) { RendererStore.get(this).cancel(); stopSelf() }
    override fun onDestroy() { wakeLock?.let { if (it.isHeld) it.release() }; super.onDestroy() }
    companion object {
        private const val CHANNEL = "rawr_renderer"
        private fun channel(context: Context) { context.getSystemService(NotificationManager::class.java).createNotificationChannel(NotificationChannel(CHANNEL, "Renderer", NotificationManager.IMPORTANCE_DEFAULT)) }
        fun start(context: Context) { ContextCompat.startForegroundService(context, Intent(context, RendererService::class.java)) }
        // Context.stopService matches an Intent component; it does not remove a SAM callback.
        @android.annotation.SuppressLint("ImplicitSamInstance")
        fun stop(context: Context) { context.stopService(Intent(context, RendererService::class.java)) }
        fun completed(context: Context, visible: Boolean, uri: Uri) {
            if (visible) Handler(Looper.getMainLooper()).post { Toast.makeText(context, "JPEG saved", Toast.LENGTH_SHORT).show() }
            else {
                channel(context)
                val view = Intent(Intent.ACTION_VIEW).setDataAndType(uri, "image/jpeg").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                val notification = NotificationCompat.Builder(context, CHANNEL).setSmallIcon(com.rawr.camera.R.drawable.ic_notification)
                    .setContentTitle("JPEG saved").setAutoCancel(true).setContentIntent(PendingIntent.getActivity(context, 1, view, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)).build()
                context.getSystemService(NotificationManager::class.java).notify(4202, notification)
            }
        }
    }
}
