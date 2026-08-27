package com.egegurtunca.smartsafety

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.RingtoneManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Uygulama kapaliyken de alarmi haber veren foreground service.
 *
 * Sunucudan push beklemiyoruz: servis kendisi /api/latest'i yokluyor ve
 * bildirimi kendisi uretiyor. Boylece Firebase, VAPID, abonelik tablosu
 * gibi hicbir sey gerekmiyor.
 */
class MonitorService : Service() {

    companion object {

        private const val CHANNEL_STATUS = "status"
        private const val CHANNEL_ALARM = "alarm"

        private const val STATUS_ID = 1
        private const val ALARM_ID = 2

        private const val POLL_MS = 10_000L

        fun start(context: Context) {
            val intent = Intent(context, MonitorService::class.java)
            try {
                context.startForegroundService(intent)
            } catch (e: Exception) {
                // Android 12+ arka plandan foreground service baslatmayi
                // kisitliyor. Uygulama acikken cagrildiginda sorun olmaz.
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, MonitorService::class.java))
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private var job: Job? = null

    // null = henuz olcum yok. Bildirim sadece 0 -> 1 gecisinde atilir,
    // alarm surerken tekrar tekrar otmez.
    private var lastAlarm: Boolean? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()

        createChannels()

        startForeground(STATUS_ID, statusNotification("Başlatılıyor..."))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {

        if (job == null) {
            job = scope.launch { loop() }
        }

        // Sistem servisi oldururse yeniden baslatsin.
        return START_STICKY
    }

    override fun onDestroy() {
        job?.cancel()
        job = null
        super.onDestroy()
    }

    private suspend fun loop() {

        while (currentCoroutineContext().isActive) {
            poll()
            delay(POLL_MS)
        }
    }

    private fun poll() {

        val config = Settings.load(this)

        if (config.baseUrl.isBlank()) {
            updateStatus("Sunucu adresi girilmedi")
            return
        }

        when (val result = Api.latest(config)) {

            is ApiResult.Ok -> {

                val reading = result.value

                if (reading == null) {
                    updateStatus("Cihazdan veri bekleniyor")
                    return
                }

                updateStatus(summary(reading))

                if (reading.alarm && lastAlarm != true) {
                    notifyAlarm(reading)
                }

                lastAlarm = reading.alarm
            }

            is ApiResult.Fail -> updateStatus("Bağlantı yok — ${result.message}")
        }
    }

    private fun summary(reading: Reading): String {

        val gas = reading.gas?.toString() ?: "--"
        val flame = reading.flame?.toString() ?: "--"

        val temperature = reading.temperature
            ?.let { String.format("%.1f", it) }
            ?: "--"

        return if (reading.alarm) {
            "ALARM — Gaz $gas, Alev $flame"
        } else {
            "Normal — Gaz $gas, Alev $flame, $temperature°C"
        }
    }

    // -----------------------------------------------------------------
    // Bildirimler
    // -----------------------------------------------------------------

    private fun manager(): NotificationManager =
        getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    private fun createChannels() {

        val status = NotificationChannel(
            CHANNEL_STATUS,
            "Durum",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Arka planda izleme bildirimi"
            setShowBadge(false)
        }

        val alarm = NotificationChannel(
            CHANNEL_ALARM,
            "Alarm",
            NotificationManager.IMPORTANCE_HIGH
        ).apply {
            description = "Gaz veya alev alarmi"
            enableVibration(true)
            vibrationPattern = longArrayOf(0, 600, 300, 600, 300, 600)

            setSound(
                RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
                    ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION),
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()
            )
        }

        manager().createNotificationChannel(status)
        manager().createNotificationChannel(alarm)
    }

    private fun openAppIntent(): PendingIntent {

        val flags =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            else
                PendingIntent.FLAG_UPDATE_CURRENT

        return PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            flags
        )
    }

    private fun statusNotification(text: String) =
        NotificationCompat.Builder(this, CHANNEL_STATUS)
            .setContentTitle("Smart Safety System")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setContentIntent(openAppIntent())
            .build()

    private fun updateStatus(text: String) {
        manager().notify(STATUS_ID, statusNotification(text))
    }

    private fun notifyAlarm(reading: Reading) {

        val reasons = buildList {
            reading.gas?.let { add("Gaz: $it") }
            reading.flame?.let { add("Alev: $it") }
        }.joinToString("   ")

        val notification = NotificationCompat.Builder(this, CHANNEL_ALARM)
            .setContentTitle("ALARM — Smart Safety System")
            .setContentText(reasons.ifBlank { "Alarm tetiklendi" })
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setAutoCancel(true)
            .setContentIntent(openAppIntent())
            .build()

        manager().notify(ALARM_ID, notification)
    }
}
