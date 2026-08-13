import os
from flask import Flask, request, jsonify, render_template
from supabase import create_client

app = Flask(__name__)

SUPABASE_URL = os.environ.get("SUPABASE_URL")
SUPABASE_KEY = os.environ.get("SUPABASE_KEY")

supabase = create_client(
    SUPABASE_URL,
    SUPABASE_KEY
)


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/data", methods=["POST"])
def receive_data():

    data = request.get_json()

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

        supabase.table("sensor_data").insert(record).execute()

        return jsonify({
            "success": True
        })

    except Exception as e:

        print("DATABASE ERROR:", e)

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

        response = (
            supabase
            .table("sensor_data")
            .select("*")
            .order("created_at", desc=True)
            .limit(100)
            .execute()
        )

        return jsonify(response.data)

    except Exception as e:

        return jsonify({
            "error": str(e)
        }), 500


if __name__ == "__main__":

    app.run(
        host="0.0.0.0",
        port=int(os.environ.get("PORT", 5000))
    )
