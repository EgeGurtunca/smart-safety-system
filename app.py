import os
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from functools import wraps

from flask import Flask, request, jsonify, render_template
from supabase import create_client

app = Flask(__name__)

SUPABASE_URL = os.environ.get("SUPABASE_URL")
SUPABASE_KEY = os.environ.get("SUPABASE_KEY")

API_TOKEN = os.environ.get("API_TOKEN")

TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN")
TELEGRAM_CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID")

supabase = (
    create_client(SUPABASE_URL, SUPABASE_KEY)
    if SUPABASE_URL and SUPABASE_KEY
    else None
)

# Telefonda cizilecek nokta sayisi. 1 saat ham veri ~720 nokta, cok fazla.
MAX_POINTS = 120

# Sensor titrerse arka arkaya bildirim yagmasin.
NOTIFY_COOLDOWN_MINUTES = 5

DEFAULT_COMMAND = {
    "fan_request": False,
    "mute": False,
    "gas_threshold": 400,
    "flame_threshold": 80,
    "temp_rise": 5,
    "temp_max": 45
}

# DHT11 sadece 0-50 C olcuyor; ustundeki bir tavan hic tetiklenmez.
TEMP_MAX_LIMIT = 50


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def truthy(value):
    """Supabase kolonu bool da int de olabilir, NodeMCU 0/1 gonderiyor."""

    if value is None:
        return False

    if isinstance(value, str):
        return value.strip() not in ("", "0", "false", "False")

    return bool(value)


def require_token(allow_open=False):
    """X-Auth header dogrulamasi.

    API_TOKEN tanimli degilse allow_open=True olan uclar calismaya devam eder
    (cihaz token kurulmadan once de veri gonderebilsin), yazma uclari reddedilir
    -- kimse uzaktan fani acamasin.
    """

    def decorator(f):

        @wraps(f)
        def wrapper(*args, **kwargs):

            if not API_TOKEN:

                if allow_open:
                    print("WARNING: API_TOKEN tanimli degil, /api/data korumasiz")
                    return f(*args, **kwargs)

                return jsonify({
                    "success": False,
                    "error": "API_TOKEN sunucuda tanimli degil"
                }), 503

            if request.headers.get("X-Auth") != API_TOKEN:
                return jsonify({
                    "success": False,
                    "error": "unauthorized"
                }), 401

            return f(*args, **kwargs)

        return wrapper

    return decorator


def alarm_edge(previous, current):
    """Alarm gecisi: 'rise', 'fall' veya None."""

    was = truthy(previous)
    now = truthy(current)

    if now and not was:
        return "rise"

    if was and not now:
        return "fall"

    return None


def downsample(rows, max_points=MAX_POINTS):
    """Esit arayla seyreltir. Ilk ve son nokta her zaman korunur."""

    if len(rows) <= max_points:
        return rows

    step = len(rows) / max_points

    out = [rows[int(i * step)] for i in range(max_points)]
    out[-1] = rows[-1]

    return out


def read_command():

    response = (
        supabase
        .table("commands")
        .select("*")
        .eq("id", 1)
        .limit(1)
        .execute()
    )

    if response.data:
        return response.data[0]

    return dict(DEFAULT_COMMAND)


def write_command(patch):

    patch["updated_at"] = now_iso()

    (
        supabase
        .table("commands")
        .update(patch)
        .eq("id", 1)
        .execute()
    )


def device_command(command):
    """Cihazin bekledigi kisa format."""

    return {
        "fan": int(truthy(command.get("fan_request"))),
        "mute": int(truthy(command.get("mute"))),
        "gt": int(command.get("gas_threshold") or DEFAULT_COMMAND["gas_threshold"]),
        "ft": int(command.get("flame_threshold") or DEFAULT_COMMAND["flame_threshold"]),
        "tr": int(command.get("temp_rise") or DEFAULT_COMMAND["temp_rise"]),
        "tm": int(command.get("temp_max") or DEFAULT_COMMAND["temp_max"])
    }


def send_telegram(text):

    if not (TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID):
        return

    url = (
        "https://api.telegram.org/bot" +
        TELEGRAM_BOT_TOKEN +
        "/sendMessage"
    )

    payload = urllib.parse.urlencode({
        "chat_id": TELEGRAM_CHAT_ID,
        "text": text
    }).encode()

    try:
        # NodeMCU'nun POST timeout'u 5 sn; burada beklemek onun isteginin
        # zaman asimina ugramasina yol acmasin.
        urllib.request.urlopen(url, data=payload, timeout=3).close()

    except Exception as e:
        # Bildirim kaybi, veri kaybindan iyidir. POST'u dusurme.
        print("TELEGRAM ERROR:", e)


def alarm_message(record, command):

    reasons = []

    limits = device_command(command)

    gas = record.get("gas")
    flame = record.get("flame")
    temperature = record.get("temperature")

    if gas is not None and gas >= limits["gt"]:
        reasons.append("gaz " + str(gas))

    if flame is not None and flame <= limits["ft"]:
        reasons.append("alev " + str(flame))

    if temperature is not None and temperature >= limits["tm"]:
        reasons.append("sicaklik " + str(temperature))

    # Hizli isinma alarmi cihazda 60 saniyelik pencereyle hesaplaniyor;
    # sunucu tek kayda bakarak bunu goremez.
    fallback = "belirlenemedi (hizli sicaklik yukselisi olabilir)"

    return (
        "ALARM - Smart Safety System\n"
        "Sebep: " + (", ".join(reasons) if reasons else fallback) + "\n"
        "Gaz: " + str(gas) + "  Alev: " + str(flame) + "\n"
        "Sicaklik: " + str(record.get("temperature")) + " C  "
        "Nem: " + str(record.get("humidity")) + " %"
    )


def cooldown_passed(command):

    last = command.get("last_notified_at")

    if not last:
        return True

    try:
        stamp = datetime.fromisoformat(last.replace("Z", "+00:00"))

    except ValueError:
        return True

    age = datetime.now(timezone.utc) - stamp

    return age >= timedelta(minutes=NOTIFY_COOLDOWN_MINUTES)


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/health")
def health():
    return jsonify({
        "service": "smart-safety-system",
        "ok": True,
        "auth": bool(API_TOKEN)
    })


@app.route("/api/data", methods=["POST"])
@require_token(allow_open=True)
def receive_data():

    data = request.get_json(silent=True)

    if not data:
        return jsonify({
            "success": False,
            "error": "No JSON data"
        }), 400

    try:

        record = {
            "temperature": data.get("temperature"),
            "humidity": data.get("humidity"),
            "gas": data.get("gas"),
            "flame": data.get("flame"),
            "button": data.get("button"),
            "fan": data.get("fan"),
            "alarm": data.get("alarm")
        }

        previous = (
            supabase
            .table("sensor_data")
            .select("alarm")
            .order("created_at", desc=True)
            .limit(1)
            .execute()
        )

        previous_alarm = (
            previous.data[0].get("alarm") if previous.data else None
        )

        supabase.table("sensor_data").insert(record).execute()

        command = read_command()

        edge = alarm_edge(previous_alarm, record["alarm"])

        # Bildirim/susturma islerinin hatasi veri akisini dusurmemeli:
        # cihaz komutunu alamazsa bir sonraki turda duzelir, veri geri gelmez.
        try:

            if edge == "fall" and truthy(command.get("mute")):
                # Alarm bitti: susturma kendini sifirlar,
                # yoksa sistem bir daha hic otmez.
                write_command({"mute": False})
                command["mute"] = False

            elif edge == "rise" and cooldown_passed(command):
                send_telegram(alarm_message(record, command))
                write_command({"last_notified_at": now_iso()})

        except Exception as e:
            print("ALARM/NOTIFY ERROR:", e)

        return jsonify({
            "success": True,
            "cmd": device_command(command)
        })

    except Exception as e:

        print("DATABASE ERROR:", e)

        return jsonify({
            "success": False,
            "error": str(e)
        }), 500


@app.route("/api/command", methods=["GET"])
def get_command():

    try:
        command = read_command()

        return jsonify({
            "fan": truthy(command.get("fan_request")),
            "mute": truthy(command.get("mute")),
            "gas_threshold": command.get("gas_threshold"),
            "flame_threshold": command.get("flame_threshold"),
            "temp_rise": command.get("temp_rise"),
            "temp_max": command.get("temp_max"),
            "updated_at": command.get("updated_at")
        })

    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/command", methods=["POST"])
@require_token()
def set_command():

    data = request.get_json(silent=True)

    if not data:
        return jsonify({
            "success": False,
            "error": "No JSON data"
        }), 400

    patch = {}

    if "fan" in data:
        patch["fan_request"] = truthy(data["fan"])

    if "mute" in data:
        patch["mute"] = truthy(data["mute"])

    # Her alanin kendi gecerli araligi var; cihaza sacma esik gitmesin.
    # gaz/alev analogRead sinirlari, sicaklik DHT11'in olcebildigi aralik.
    for key, low, high in (
        ("gas_threshold", 0, 1023),
        ("flame_threshold", 0, 1023),
        ("temp_rise", 1, 30),
        ("temp_max", 0, TEMP_MAX_LIMIT)
    ):

        if key not in data:
            continue

        try:
            value = int(data[key])

        except (TypeError, ValueError):
            return jsonify({
                "success": False,
                "error": key + " sayi olmali"
            }), 400

        if not low <= value <= high:
            return jsonify({
                "success": False,
                "error": "%s %d-%d araliginda olmali" % (key, low, high)
            }), 400

        patch[key] = value

    if not patch:
        return jsonify({
            "success": False,
            "error": "Degistirilecek alan yok"
        }), 400

    try:
        write_command(patch)

        return jsonify({
            "success": True,
            "applied": patch
        })

    except Exception as e:
        return jsonify({
            "success": False,
            "error": str(e)
        }), 500


@app.route("/api/latest")
def latest():

    try:

        response = (
            supabase
            .table("sensor_data")
            .select("*")
            .order("created_at", desc=True)
            .limit(1)
            .execute()
        )

        if response.data:
            return jsonify(response.data[0])

        return jsonify({})

    except Exception as e:

        return jsonify({
            "error": str(e)
        }), 500


@app.route("/api/history")
def history():

    try:
        minutes = int(request.args.get("minutes", 15))

    except (TypeError, ValueError):
        minutes = 15

    minutes = max(1, min(minutes, 1440))

    since = (
        datetime.now(timezone.utc) - timedelta(minutes=minutes)
    ).isoformat()

    try:

        response = (
            supabase
            .table("sensor_data")
            .select("*")
            .gte("created_at", since)
            .order("created_at", desc=False)
            .limit(5000)
            .execute()
        )

        return jsonify(downsample(response.data or []))

    except Exception as e:

        return jsonify({
            "error": str(e)
        }), 500


if __name__ == "__main__":

    app.run(
        host="0.0.0.0",
        port=int(os.environ.get("PORT", 5000))
    )
