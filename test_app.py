"""Calistir: python test_app.py

Supabase'e baglanmaz. Sadece saf mantik ve auth kontrol edilir --
komut hattinin karar veren kisimlari burada.
"""

import os

os.environ.setdefault("API_TOKEN", "test-token")

import app as srv


def test_downsample():

    rows = list(range(720))

    out = srv.downsample(rows, max_points=120)

    assert len(out) == 120, len(out)
    assert out[0] == 0, out[0]
    assert out[-1] == 719, out[-1]

    # Kisa liste dokunulmadan gecer.
    short = [1, 2, 3]
    assert srv.downsample(short, max_points=120) is short


def test_alarm_edge():

    assert srv.alarm_edge(0, 1) == "rise"
    assert srv.alarm_edge(1, 0) == "fall"
    assert srv.alarm_edge(1, 1) is None
    assert srv.alarm_edge(0, 0) is None

    # Ilk kayit: onceki satir yok.
    assert srv.alarm_edge(None, 1) == "rise"
    assert srv.alarm_edge(None, 0) is None

    # NodeMCU string gonderirse de ayni sonuc.
    assert srv.alarm_edge("0", "1") == "rise"
    assert srv.alarm_edge(False, True) == "rise"


def test_command_requires_token():

    client = srv.app.test_client()

    # Tokensiz yazma reddedilir -- supabase'e hic gidilmez.
    assert client.post("/api/command", json={"fan": 1}).status_code == 401

    # Yanlis token da reddedilir.
    response = client.post(
        "/api/command",
        json={"fan": 1},
        headers={"X-Auth": "yanlis"}
    )
    assert response.status_code == 401


def test_threshold_validation():

    client = srv.app.test_client()

    headers = {"X-Auth": os.environ["API_TOKEN"]}

    # Sinir disi esik supabase'e ulasmadan reddedilir.
    response = client.post(
        "/api/command",
        json={"gas_threshold": 5000},
        headers=headers
    )
    assert response.status_code == 400, response.status_code

    response = client.post(
        "/api/command",
        json={"gas_threshold": "abc"},
        headers=headers
    )
    assert response.status_code == 400, response.status_code

    # Bos govde de reddedilir.
    response = client.post("/api/command", json={}, headers=headers)
    assert response.status_code == 400, response.status_code


def test_device_command_format():

    out = srv.device_command({
        "fan_request": True,
        "mute": False,
        "gas_threshold": 350,
        "flame_threshold": 90
    })

    assert out == {"fan": 1, "mute": 0, "gt": 350, "ft": 90}, out

    # Eksik satir varsayilanlara duser, cihaza None gitmez.
    out = srv.device_command({})
    assert out == {"fan": 0, "mute": 0, "gt": 400, "ft": 80}, out


if __name__ == "__main__":

    for name, fn in sorted(globals().items()):

        if name.startswith("test_"):
            fn()
            print("ok", name)

    print("hepsi gecti")
