package com.egegurtunca.smartsafety

import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant
import java.time.LocalDateTime
import java.time.OffsetDateTime
import java.time.ZoneOffset

data class Reading(
    val timeMillis: Long,
    val temperature: Double?,
    val humidity: Double?,
    val gas: Int?,
    val flame: Int?,
    val fan: Boolean,
    val alarm: Boolean
)

data class Command(
    val fan: Boolean,
    val mute: Boolean,
    val gasThreshold: Int,
    val flameThreshold: Int,
    val tempRise: Int,
    val tempMax: Int
)

sealed class ApiResult<out T> {
    data class Ok<T>(val value: T) : ApiResult<T>()
    data class Fail(val message: String) : ApiResult<Nothing>()
}

object Api {

    private const val TIMEOUT_MS = 10_000

    /** Sunucu tarafi -1 gibi sacma esik kabul etmiyor; burada da ayni sinir. */
    const val SENSOR_MAX = 1023

    /** DHT11 sadece 0-50 C olcuyor; ustundeki tavan hic tetiklenmez. */
    const val TEMP_MAX_LIMIT = 50
    const val TEMP_RISE_MIN = 1
    const val TEMP_RISE_MAX = 30

    // ---------------------------------------------------------------
    // HTTP
    // ---------------------------------------------------------------

    private fun request(
        url: String,
        method: String,
        token: String?,
        body: String?
    ): Pair<Int, String> {

        val connection = URL(url).openConnection() as HttpURLConnection

        try {
            connection.requestMethod = method
            connection.connectTimeout = TIMEOUT_MS
            connection.readTimeout = TIMEOUT_MS

            if (!token.isNullOrBlank()) {
                connection.setRequestProperty("X-Auth", token)
            }

            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toByteArray()) }
            }

            val code = connection.responseCode

            val stream =
                if (code in 200..299) connection.inputStream
                else connection.errorStream

            val text = stream
                ?.bufferedReader()
                ?.use(BufferedReader::readText)
                .orEmpty()

            return code to text

        } finally {
            connection.disconnect()
        }
    }

    // ---------------------------------------------------------------
    // Zaman
    //
    // Supabase "2026-08-19T11:03:43.318123+00:00" doner, ama kolon
    // tipine gore offset'siz de gelebiliyor. Ikisini de kabul et.
    // ---------------------------------------------------------------

    private fun parseTime(value: String?): Long {

        if (value.isNullOrBlank()) return 0L

        return try {
            OffsetDateTime.parse(value).toInstant().toEpochMilli()

        } catch (e: Exception) {
            try {
                LocalDateTime.parse(value).toInstant(ZoneOffset.UTC).toEpochMilli()

            } catch (e2: Exception) {
                try {
                    Instant.parse(value).toEpochMilli()
                } catch (e3: Exception) {
                    0L
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // Ayristirma
    // ---------------------------------------------------------------

    private fun JSONObject.doubleOrNull(key: String): Double? =
        if (isNull(key)) null else optDouble(key).takeIf { !it.isNaN() }

    private fun JSONObject.intOrNull(key: String): Int? =
        if (isNull(key)) null else optInt(key, Int.MIN_VALUE).takeIf { it != Int.MIN_VALUE }

    private fun readingOf(json: JSONObject) = Reading(
        timeMillis = parseTime(
            if (json.isNull("created_at")) null else json.optString("created_at")
        ),
        temperature = json.doubleOrNull("temperature"),
        humidity = json.doubleOrNull("humidity"),
        gas = json.intOrNull("gas"),
        flame = json.intOrNull("flame"),
        fan = json.optInt("fan", 0) != 0 || json.optBoolean("fan", false),
        alarm = json.optInt("alarm", 0) != 0 || json.optBoolean("alarm", false)
    )

    // ---------------------------------------------------------------
    // Uclar
    // ---------------------------------------------------------------

    fun latest(config: Config): ApiResult<Reading?> = guarded {

        val (code, text) = request("${config.baseUrl}/api/latest", "GET", null, null)

        if (code !in 200..299) {
            return@guarded ApiResult.Fail("Sunucu $code")
        }

        val json = JSONObject(text)

        if (!json.has("created_at")) {
            // Henuz hic veri yok.
            ApiResult.Ok(null)
        } else {
            ApiResult.Ok(readingOf(json))
        }
    }

    fun history(config: Config, minutes: Int): ApiResult<List<Reading>> = guarded {

        val (code, text) = request(
            "${config.baseUrl}/api/history?minutes=$minutes", "GET", null, null
        )

        if (code !in 200..299) {
            return@guarded ApiResult.Fail("Sunucu $code")
        }

        val array = JSONArray(text)

        val rows = ArrayList<Reading>(array.length())

        for (i in 0 until array.length()) {
            rows.add(readingOf(array.getJSONObject(i)))
        }

        ApiResult.Ok(rows)
    }

    fun command(config: Config): ApiResult<Command> = guarded {

        val (code, text) = request("${config.baseUrl}/api/command", "GET", null, null)

        if (code !in 200..299) {
            return@guarded ApiResult.Fail("Sunucu $code")
        }

        val json = JSONObject(text)

        ApiResult.Ok(
            Command(
                fan = json.optBoolean("fan", false),
                mute = json.optBoolean("mute", false),
                gasThreshold = json.optInt("gas_threshold", 400),
                flameThreshold = json.optInt("flame_threshold", 80),
                tempRise = json.optInt("temp_rise", 5),
                tempMax = json.optInt("temp_max", 45)
            )
        )
    }

    fun sendCommand(config: Config, patch: JSONObject): ApiResult<Unit> = guarded {

        val (code, text) = request(
            "${config.baseUrl}/api/command", "POST", config.token, patch.toString()
        )

        when {
            code == 401 -> ApiResult.Fail("Erişim anahtarı geçersiz")

            code == 503 -> ApiResult.Fail("Sunucuda API_TOKEN tanımlı değil")

            code !in 200..299 -> {
                val reason = try {
                    JSONObject(text).optString("error", "Sunucu $code")
                } catch (e: Exception) {
                    "Sunucu $code"
                }
                ApiResult.Fail(reason)
            }

            else -> ApiResult.Ok(Unit)
        }
    }

    private inline fun <T> guarded(block: () -> ApiResult<T>): ApiResult<T> =
        try {
            block()
        } catch (e: Exception) {
            ApiResult.Fail(e.message ?: "Bağlantı hatası")
        }
}
