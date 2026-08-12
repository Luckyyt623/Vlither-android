package com.vlither

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

class EventNotificationReceiver : BroadcastReceiver() {
    companion object {
        const val EXTRA_EVENT_ID = "event_id"
        const val EXTRA_EVENT_NAME = "event_name"
        const val EXTRA_SERVER_IP = "server_ip"
        private const val CHANNEL_ID = "vlither_events"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val eventId = intent.getStringExtra(EXTRA_EVENT_ID).orEmpty()
        val eventName = intent.getStringExtra(EXTRA_EVENT_NAME)
            ?.takeIf { it.isNotBlank() } ?: "Vlither event"
        val serverIp = intent.getStringExtra(EXTRA_SERVER_IP).orEmpty()
        val notificationId = eventId.hashCode() and 0x7fffffff
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE)
            as? NotificationManager ?: return

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    "Vlither events",
                    NotificationManager.IMPORTANCE_HIGH
                ).apply {
                    description = "Notifications when interested Vlither events begin"
                    enableVibration(true)
                }
            )
        }

        val openApp = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
            putExtra(EXTRA_EVENT_ID, eventId)
        }
        val contentIntent = PendingIntent.getActivity(
            context,
            notificationId,
            openApp,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            android.app.Notification.Builder(context, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            android.app.Notification.Builder(context)
        }
        val text = if (serverIp.isNotBlank())
            "$eventName is starting now. Server: $serverIp"
        else "$eventName is starting now."
        builder
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle("Vlither event is live")
            .setContentText(text)
            .setStyle(android.app.Notification.BigTextStyle().bigText(text))
            .setContentIntent(contentIntent)
            .setAutoCancel(true)
            .setPriority(android.app.Notification.PRIORITY_HIGH)
            .setCategory(android.app.Notification.CATEGORY_EVENT)
        manager.notify(notificationId, builder.build())
    }
}
