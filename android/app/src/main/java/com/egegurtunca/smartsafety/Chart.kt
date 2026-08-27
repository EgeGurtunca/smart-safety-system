package com.egegurtunca.smartsafety

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.background
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp

/**
 * Sicaklik/nem 0-100, gaz/alev 0-1023 araliginda. Tek olcege
 * sikistirilirsa sicaklik gorunmez olur, o yuzden her serinin
 * kendi tavani var.
 */
private data class Series(
    val label: String,
    val color: Color,
    val max: Float,
    val value: (Reading) -> Float?
)

private val SERIES = listOf(
    Series("Sıcaklık", Color(0xFFEF4444), 100f) { it.temperature?.toFloat() },
    Series("Nem", Color(0xFF3B82F6), 100f) { it.humidity?.toFloat() },
    Series("Gaz", Color(0xFFF59E0B), 1023f) { it.gas?.toFloat() },
    Series("Alev", Color(0xFF10B981), 1023f) { it.flame?.toFloat() }
)

@Composable
fun SensorChart(rows: List<Reading>, modifier: Modifier = Modifier) {

    Column(modifier) {

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            SERIES.forEach { series ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        Modifier
                            .size(8.dp)
                            .background(series.color, CircleShape)
                    )
                    Text(
                        text = " ${series.label}",
                        style = MaterialTheme.typography.labelSmall
                    )
                }
            }
        }

        Box(
            Modifier
                .fillMaxWidth()
                .height(200.dp)
                .padding(top = 8.dp)
        ) {

            if (rows.size < 2) {

                Text(
                    text = "Grafik için yeterli veri yok",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.align(Alignment.Center)
                )

            } else {

                val grid = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.15f)

                Canvas(Modifier.fillMaxWidth().height(200.dp)) {

                    for (i in 0..4) {
                        val y = size.height * i / 4f
                        drawLine(
                            color = grid,
                            start = androidx.compose.ui.geometry.Offset(0f, y),
                            end = androidx.compose.ui.geometry.Offset(size.width, y),
                            strokeWidth = 1f
                        )
                    }

                    val stepX =
                        if (rows.size > 1) size.width / (rows.size - 1) else size.width

                    SERIES.forEach { series ->

                        val path = Path()
                        var started = false

                        rows.forEachIndexed { index, reading ->

                            val raw = series.value(reading)

                            if (raw == null) {
                                // Olcum yok: cizgiyi kopar, uydurma deger cizme.
                                started = false
                                return@forEachIndexed
                            }

                            val ratio = (raw / series.max).coerceIn(0f, 1f)

                            val x = stepX * index
                            val y = size.height - (ratio * size.height)

                            if (started) {
                                path.lineTo(x, y)
                            } else {
                                path.moveTo(x, y)
                                started = true
                            }
                        }

                        drawPath(
                            path = path,
                            color = series.color,
                            style = Stroke(width = 3f)
                        )
                    }
                }
            }
        }
    }
}
