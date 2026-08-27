package com.egegurtunca.smartsafety

import android.content.Context

/**
 * Sunucu adresi ve erişim anahtarı. Anahtar koda gömülmez —
 * kullanıcı bir kez girer, cihazda kalır.
 */
data class Config(
    val baseUrl: String,
    val token: String,
    val monitorEnabled: Boolean
) {
    /** Okuma uçları anahtarsız çalışıyor; izleme için adres yeterli. */
    val canMonitor: Boolean
        get() = baseUrl.isNotBlank()

    /** Fan/susturma/eşik yazmak için anahtar şart. */
    val canControl: Boolean
        get() = baseUrl.isNotBlank() && token.isNotBlank()
}

object Settings {

    private const val FILE = "smart_safety"
    private const val KEY_URL = "base_url"
    private const val KEY_TOKEN = "token"
    private const val KEY_MONITOR = "monitor_enabled"

    private const val DEFAULT_URL =
        "https://smart-safety-system-y11a.onrender.com"

    fun load(context: Context): Config {

        val prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

        return Config(
            baseUrl = prefs.getString(KEY_URL, DEFAULT_URL).orEmpty().trimEnd('/'),
            token = prefs.getString(KEY_TOKEN, "").orEmpty(),
            monitorEnabled = prefs.getBoolean(KEY_MONITOR, true)
        )
    }

    fun save(context: Context, config: Config) {

        context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_URL, config.baseUrl.trim().trimEnd('/'))
            .putString(KEY_TOKEN, config.token.trim())
            .putBoolean(KEY_MONITOR, config.monitorEnabled)
            .apply()
    }
}
