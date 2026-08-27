# Smart Safety System

Gaz (MQ-2), alev ve sıcaklık/nem izleyen, alarm durumunda fan ve buzzer
çalıştıran Arduino sistemi. NodeMCU veriyi Render'daki Flask API'ye gönderiyor,
Android uygulaması izleme, kontrol ve alarm bildirimi sağlıyor.

```
Arduino  --serial-->  NodeMCU  --HTTPS-->  Flask (Render)  -->  Supabase
   ^                     |                     ^
   |                     |                     |
   +---- komut ----------+                     +---- Android uygulaması
```

Komut ayrı bir kanaldan inmiyor: NodeMCU'nun 5 saniyede bir attığı POST'un
**cevabına** biniyor. Gecikme bir POST periyodu, yaklaşık 5 saniye.

## Dosyalar

| Dosya | Ne yapar |
|---|---|
| `app.py` | Flask API: veri alma, komut okuma/yazma, geçmiş, Telegram bildirimi |
| `templates/index.html` | Web arayüzü: canlı değerler, kontrol, grafik, geçmiş |
| `supabase_schema.sql` | Bir kez çalıştırılacak tablo/index/temizlik SQL'i |
| `test_app.py` | Saf mantık ve auth testleri (Supabase'e bağlanmaz) |
| `android/` | Kotlin + Compose Android uygulaması |
| `arduino/sketch_aug13a/` | Arduino: sensörler, alarm, LCD, SD, komut uygulama |
| `arduino/sketch_aug13b/` | NodeMCU: WiFi, sunucu iletişimi, komut aktarımı |

Aynı veriye iki yerden bakabilirsin: Render adresindeki site ve Android
uygulaması. `/health` sunucunun ayakta olup olmadığını döner.

NodeMCU **hiçbir zaman HTML göndermez** — sunucuya sadece 88 baytlık JSON gider.

## Bağlantılar

### Arduino Uno

| Pin | Bağlı |
|---|---|
| D2 | DHT11 DATA |
| D3 | Buzzer (+) — passive buzzer, `tone()` kullanılıyor |
| D4 | Buton (diğer bacak GND, `INPUT_PULLUP`, harici direnç yok) |
| D5 | Röle IN |
| D6 | Yeşil LED → 220Ω → GND |
| D7 | Kırmızı LED → 220Ω → GND |
| D8 | **NodeMCU D6** (komut girişi) |
| D9 | NodeMCU D7 (veri çıkışı, gerilim bölücü ile) |
| D10 | SD CS |
| D11 | SD MOSI |
| D12 | SD MISO |
| D13 | SD SCK |
| A0 | MQ-2 AO |
| A1 | Alev sensörü AO |
| A4 | LCD SDA (I2C) |
| A5 | LCD SCL (I2C) |

MQ-2 ve alev sensörünün DO uçları kullanılmıyor, analog okunuyor.

### Arduino ↔ NodeMCU

İki yön de gerekli — tek yön bağlıysa veri akar ama kontrol çalışmaz.

**Yukarı (veri), 5V → 3.3V bölücü ile:**

```
Arduino D9 ──1kΩ──┬──→ NodeMCU D7
                  │
                 1kΩ
                  │
                 1kΩ
                  │
                 GND
```

5V × 2kΩ/3kΩ = 3.33V

**Aşağı (komut), doğrudan:**

```
NodeMCU D6 ─────────→ Arduino D8
```

Bu yöne bölücü konmaz. NodeMCU zaten 3.3V veriyor, Arduino ~3.0V üstünü HIGH
sayar. Bölücü eklenirse sinyal eşiğin altına düşer ve komut hiç görünmez.

### Güç

- NodeMCU VIN ← Arduino 5V, NodeMCU G ← Arduino GND
- Fan **12V ayrı pilden**: pil(+) → röle COM, röle NO → fan(+), fan(−) → pil(−). NC kullanılmıyor
- Tüm GND'ler ortak

ESP8266 WiFi gönderirken 250–300 mA çekebilir. Röle, LCD ve SD kart da aynı 5V
hattında. Rastgele reset, sürekli WiFi kopması veya SD hatası görürsen NodeMCU'yu
ayrı USB'den besle (GND'ler yine ortak kalmalı).

## Kurulum

### 1. Supabase

`supabase_schema.sql` içeriğini SQL editöründe çalıştır. `commands` tablosunu,
geçmiş sorguları için index'i ve günlük temizlik işini kurar.

Temizlik önemli: 5 saniyede bir kayıt günde ~17.000 satır demek, ücretsiz
katmanın 500 MB'ı birkaç ayda dolar.

### 2. Render ortam değişkenleri

| Değişken | Zorunlu | Açıklama |
|---|---|---|
| `SUPABASE_URL` | evet | Supabase proje URL'i |
| `SUPABASE_KEY` | evet | Supabase anon/service key |
| `API_TOKEN` | kontrol için evet | Kendi uydurduğun uzun rastgele metin |
| `TELEGRAM_BOT_TOKEN` | isteğe bağlı | Yedek bildirim kanalı |
| `TELEGRAM_CHAT_ID` | isteğe bağlı | Yedek bildirim kanalı |

`API_TOKEN` tanımlı değilse: veri akışı çalışır, ama **kontrol uçları
kapalıdır** — kimse uzaktan fanı açamaz.

Telegram isteğe bağlı: asıl bildirim artık Android uygulamasından geliyor.
İkisi birlikte de çalışır.

### 3. Firmware — SIRA ÖNEMLİ

1. **Önce NodeMCU'yu yak.** `sketch_aug13b.ino` içindeki `ssid`, `password` ve
   `apiToken` değerlerini doldur. `apiToken`, Render'a koyacağın `API_TOKEN` ile
   aynı olmalı.
2. **Sonra Arduino'yu yak** (`sketch_aug13a.ino`).
3. **En son Render'da `API_TOKEN`'ı tanımla.**

Ters sırada yaparsan (önce `API_TOKEN`, sonra firmware) eski firmware `X-Auth`
göndermediği için 401 alır ve veri akışı kesilir.

Repodaki firmware dosyalarında WiFi şifresi ve token yer tutucudur; gerçek
değerleri commit etme.

### 4. Android uygulaması

APK'yı derle:

```bash
cd android && ./gradlew assembleDebug
```

Çıktı: `android/app/build/outputs/apk/debug/app-debug.apk`

Telefona kur (USB hata ayıklama açıkken):

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

APK'yı telefona kopyalayıp dosya yöneticisinden de kurabilirsin — "bilinmeyen
kaynaklardan yükleme" izni gerekir.

Uygulamayı ilk açtığında **Ayarlar** bölümüne sunucu adresini ve `API_TOKEN`'ı
gir. Anahtar cihazda saklanır, koda gömülü değildir.

### 5. Samsung'da bildirimin kesilmemesi için

Uygulama arka planda bir foreground service çalıştırıp 10 saniyede bir sunucuyu
yokluyor ve alarmı kendisi bildiriyor. One UI varsayılan olarak kullanılmayan
uygulamaları uyutur, bu da bildirimi sessizce keser:

**Ayarlar → Batarya ve cihaz bakımı → Batarya → Arka planda kullanım sınırları**
— Smart Safety'nin "Uyuyan uygulamalar" ve "Derin uyuyan uygulamalar"
listelerinde **olmadığından** emin ol.

Ayrıca uygulama bilgisi → Batarya → **Kısıtlanmamış** seç.

## Kontrol kuralları

**Fan uzaktan sadece AÇILIR.** Cihazda mantık `alarm || buton || uzaktanFan`.
Alarm varken telefondan fan kapatılamaz — bu bir güvenlik sistemi, uzaktan
kumanda değil.

**Susturma kendini sıfırlar.** Alarm bittiğinde sunucu `mute`'u temizler.
Yoksa sistem bir kez susturulduktan sonra bir daha hiç ötmezdi.

**Eşikler EEPROM'da.** Uzaktan değiştirilen gaz/alev eşiği Arduino'nun
EEPROM'una yazılır, elektrik kesintisinde kaybolmaz. Sadece değer değiştiğinde
yazılır (EEPROM ömrü ~100k yazım).

**Alarm bildirimi kenar tetiklemeli.** Sadece alarm başladığında bildirim gider;
alarm sürerken tekrar tekrar ötmez.

## Test

```bash
python test_app.py
```

Supabase'e bağlanmaz. Seyreltme, alarm kenar tespiti, eşik doğrulaması ve
token kontrolünü test eder.

## Yerel çalıştırma

```bash
pip install -r requirements.txt
```

Ortam değişkenlerini tanımlayıp `python app.py` ile 5000 portunda çalışır.
