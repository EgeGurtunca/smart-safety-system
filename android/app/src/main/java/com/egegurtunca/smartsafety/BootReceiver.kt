package com.egegurtunca.smartsafety

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * Telefon yeniden baslatilinca izleme sessizce durmasin.
 *
 * Android 12+ arka plandan foreground service baslatmayi kisitliyor;
 * engellenirse cokmek yerine sessizce vazgeciyoruz -- kullanici
 * uygulamayi bir kez actiginda servis yine baslar.
 */
class BootReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {

        if (intent.action != Intent.ACTION_BOOT_COMPLETED) {
            return
        }

        val config = Settings.load(context)

        if (!config.canMonitor || !config.monitorEnabled) {
            return
        }

        try {
            MonitorService.start(context)
        } catch (e: Exception) {
            // Sistem izin vermedi; uygulama acilinca baslayacak.
        }
    }
}
