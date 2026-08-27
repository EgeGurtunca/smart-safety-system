package com.egegurtunca.smartsafety

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

private val DANGER = Color(0xFFEF4444)
private val SAFE = Color(0xFF10B981)
private val WARN = Color(0xFFF59E0B)

private const val HUMIDITY_HIGH = 95.0
private const val HUMIDITY_LOW = 5.0

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        askNotificationPermission()

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                AppScreen()
            }
        }
    }

    private fun askNotificationPermission() {

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return
        }

        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.POST_NOTIFICATIONS
        ) == PackageManager.PERMISSION_GRANTED

        if (granted) {
            return
        }

        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }
            .launch(Manifest.permission.POST_NOTIFICATIONS)
    }
}


@Composable
fun AppScreen() {

    val context = LocalContext.current
    val scope = rememberCoroutineScopeCompat()

    var config by remember { mutableStateOf(Settings.load(context)) }

    var reading by remember { mutableStateOf<Reading?>(null) }
    var command by remember { mutableStateOf<Command?>(null) }
    var history by remember { mutableStateOf<List<Reading>>(emptyList()) }

    var minutes by remember { mutableStateOf(15) }
    var message by remember { mutableStateOf("") }

    var gasField by remember { mutableStateOf("") }
    var flameField by remember { mutableStateOf("") }
    var fieldsFilled by remember { mutableStateOf(false) }

    // Canli degerler ve komut durumu
    LaunchedEffect(config.baseUrl) {

        while (true) {

            if (config.canMonitor) {

                withContext(Dispatchers.IO) {

                    when (val result = Api.latest(config)) {
                        is ApiResult.Ok -> {
                            reading = result.value
                            message = ""
                        }
                        is ApiResult.Fail -> message = result.message
                    }

                    when (val result = Api.command(config)) {
                        is ApiResult.Ok -> command = result.value
                        is ApiResult.Fail -> Unit
                    }
                }

                command?.let {
                    if (!fieldsFilled) {
                        gasField = it.gasThreshold.toString()
                        flameField = it.flameThreshold.toString()
                        fieldsFilled = true
                    }
                }
            }

            delay(3000)
        }
    }

    // Gecmis / grafik
    LaunchedEffect(config.baseUrl, minutes) {

        while (true) {

            if (config.canMonitor) {
                withContext(Dispatchers.IO) {
                    when (val result = Api.history(config, minutes)) {
                        is ApiResult.Ok -> history = result.value
                        is ApiResult.Fail -> Unit
                    }
                }
            }

            delay(10_000)
        }
    }

    // Izleme servisi
    LaunchedEffect(config.canMonitor, config.monitorEnabled) {
        if (config.canMonitor && config.monitorEnabled) {
            MonitorService.start(context)
        } else {
            MonitorService.stop(context)
        }
    }

    fun send(patch: JSONObject, okText: String) {
        scope.launch {
            val result = withContext(Dispatchers.IO) { Api.sendCommand(config, patch) }
            message = when (result) {
                is ApiResult.Ok -> okText
                is ApiResult.Fail -> result.message
            }
            withContext(Dispatchers.IO) {
                (Api.command(config) as? ApiResult.Ok)?.let { command = it.value }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {

        Text(
            text = "Smart Safety System",
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold
        )

        if (message.isNotBlank()) {
            Text(text = message, color = DANGER, style = MaterialTheme.typography.bodySmall)
        }

        // -------------------------------------------------------------
        // Canli degerler
        // -------------------------------------------------------------

        val alarmOn = reading?.alarm == true

        Card(
            colors = CardDefaults.cardColors(
                containerColor =
                    if (alarmOn) DANGER.copy(alpha = 0.25f)
                    else MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Column(Modifier.padding(16.dp)) {

                Text(
                    text = if (alarmOn) "ALARM" else "NORMAL",
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold,
                    color = if (alarmOn) DANGER else SAFE
                )

                Text(
                    text = "Fan: " + if (reading?.fan == true) "AÇIK" else "KAPALI",
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            ValueCard("Gaz", reading?.gas?.toString(), Modifier.weight(1f))
            ValueCard("Alev", reading?.flame?.toString(), Modifier.weight(1f))
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            ValueCard(
                "Sıcaklık",
                reading?.temperature?.let { String.format("%.1f °C", it) },
                Modifier.weight(1f)
            )
            ValueCard(
                "Nem",
                reading?.humidity?.let { String.format("%.0f %%", it) },
                Modifier.weight(1f),
                note = humidityNote(reading?.humidity)
            )
        }

        // -------------------------------------------------------------
        // Kontrol
        // -------------------------------------------------------------

        Card {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {

                Text("Kontrol", fontWeight = FontWeight.Bold)

                if (!config.canControl) {
                    Text(
                        "Erişim anahtarı girilmedi — aşağıdaki ayarlardan ekle.",
                        style = MaterialTheme.typography.bodySmall
                    )
                }

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {

                    Button(
                        onClick = {
                            val next = if (command?.fan == true) 0 else 1
                            send(JSONObject().put("fan", next), "Fan komutu gönderildi")
                        },
                        enabled = config.canControl,
                        colors = ButtonDefaults.buttonColors(
                            containerColor =
                                if (command?.fan == true) DANGER
                                else MaterialTheme.colorScheme.secondaryContainer
                        )
                    ) {
                        Text(if (command?.fan == true) "Fan: AÇIK" else "Fan: KAPALI")
                    }

                    Button(
                        onClick = {
                            val next = if (command?.mute == true) 0 else 1
                            send(JSONObject().put("mute", next), "Susturma gönderildi")
                        },
                        enabled = config.canControl,
                        colors = ButtonDefaults.buttonColors(
                            containerColor =
                                if (command?.mute == true) DANGER
                                else MaterialTheme.colorScheme.secondaryContainer
                        )
                    ) {
                        Text(if (command?.mute == true) "Ses: KAPALI" else "Sesi kes")
                    }
                }

                // Komut cihaza ~5 saniyede iniyor; iyimser gosterim yok.
                if (command?.fan == true && reading?.fan == false) {
                    Text(
                        "Fan komutu cihaza gönderiliyor...",
                        style = MaterialTheme.typography.bodySmall
                    )
                }

                Row(
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {

                    OutlinedTextField(
                        value = gasField,
                        onValueChange = { gasField = it },
                        label = { Text("Gaz eşiği") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f)
                    )

                    OutlinedTextField(
                        value = flameField,
                        onValueChange = { flameField = it },
                        label = { Text("Alev eşiği") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f)
                    )
                }

                Button(
                    onClick = {
                        val gas = gasField.toIntOrNull()
                        val flame = flameField.toIntOrNull()

                        if (gas == null || flame == null ||
                            gas !in 0..Api.SENSOR_MAX || flame !in 0..Api.SENSOR_MAX
                        ) {
                            message = "Eşikler 0-${Api.SENSOR_MAX} arasında olmalı"
                        } else {
                            send(
                                JSONObject()
                                    .put("gas_threshold", gas)
                                    .put("flame_threshold", flame),
                                "Eşikler kaydedildi"
                            )
                        }
                    },
                    enabled = config.canControl
                ) {
                    Text("Eşikleri kaydet")
                }
            }
        }

        // -------------------------------------------------------------
        // Grafik
        // -------------------------------------------------------------

        Card {
            Column(Modifier.padding(16.dp)) {

                Text("Grafik", fontWeight = FontWeight.Bold)

                Row(
                    Modifier.padding(vertical = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(1 to "1 dk", 5 to "5 dk", 15 to "15 dk", 60 to "1 saat")
                        .forEach { (value, label) ->
                            FilterChip(
                                selected = minutes == value,
                                onClick = { minutes = value },
                                label = { Text(label) }
                            )
                        }
                }

                SensorChart(history, Modifier.fillMaxWidth())
            }
        }

        // -------------------------------------------------------------
        // Ayarlar
        // -------------------------------------------------------------

        Card {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {

                Text("Ayarlar", fontWeight = FontWeight.Bold)

                var urlField by remember { mutableStateOf(config.baseUrl) }
                var tokenField by remember { mutableStateOf(config.token) }

                OutlinedTextField(
                    value = urlField,
                    onValueChange = { urlField = it },
                    label = { Text("Sunucu adresi") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                OutlinedTextField(
                    value = tokenField,
                    onValueChange = { tokenField = it },
                    label = { Text("Erişim anahtarı (API_TOKEN)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                Button(onClick = {
                    val next = config.copy(baseUrl = urlField, token = tokenField)
                    Settings.save(context, next)
                    config = Settings.load(context)
                    message = "Ayarlar kaydedildi"
                }) {
                    Text("Kaydet")
                }

                Row(verticalAlignment = Alignment.CenterVertically) {

                    Switch(
                        checked = config.monitorEnabled,
                        onCheckedChange = { on ->
                            val next = config.copy(monitorEnabled = on)
                            Settings.save(context, next)
                            config = Settings.load(context)
                        }
                    )

                    Text(
                        "  Arka planda izle ve alarmda bildir",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
}


@Composable
private fun ValueCard(
    title: String,
    value: String?,
    modifier: Modifier = Modifier,
    note: String? = null
) {

    Card(modifier) {
        Column(Modifier.padding(16.dp)) {

            Text(title, style = MaterialTheme.typography.labelMedium)

            Text(
                text = value ?: "--",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            if (!note.isNullOrBlank()) {
                Text(
                    text = note,
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.Bold,
                    color = WARN
                )
            }
        }
    }
}


/** Nem uyarisi. Alarm degil: fan, buzzer ve bildirim tetiklenmez. */
private fun humidityNote(humidity: Double?): String? = when {
    humidity == null -> null
    humidity >= HUMIDITY_HIGH -> "ÇOK NEMLİ"
    humidity <= HUMIDITY_LOW -> "ÇOK KURU"
    else -> null
}


@Composable
private fun rememberCoroutineScopeCompat() =
    androidx.compose.runtime.rememberCoroutineScope()
