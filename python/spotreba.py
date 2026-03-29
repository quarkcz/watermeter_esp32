#!/usr/bin/python3
import requests
from datetime import timedelta
import datetime
import json

def parse_data(item):
    """
    Parsuje JSON položku a vrací slovník s datem a počtem impulsů.
    """
    datum_a_cas = datetime.datetime.fromisoformat(item["dt"])

    return {
        "datum": datum_a_cas,
        "impulsy": int(item["val"])
    }


def get_data(last_h=48):
    url = (
        "https://your-server.com/sensor/sensor-read.php"
        "?sensor_id=10001"
        "&sensor_key=xxx_your_key_xxx"
        "&mode=last"
        f"&last_h={last_h}"
        "&format=json"
    )

    response = requests.get(url)
    response.raise_for_status()

    data = response.json()

    vysledky = []
    for item in data:
        vysledky.append(parse_data(item))

    vysledky.sort(key=lambda x: x["datum"], reverse=True)

    return vysledky



def analyzuj_spotrebu():
    all_data = get_data()
    impulsy = [item['impulsy'] for item in all_data]
    datumy = [item['datum'] for item in all_data]
    impulsy.reverse()
    datumy.reverse()

    # Celkové informace
    min_date = datumy[0]
    max_date = datumy[-1]
    min_value = impulsy[0]
    max_value = impulsy[-1]
    spotreba_celkem = max_value - min_value
    date_diff_h = (datumy[-1] - datumy[0]).total_seconds() / 3600.0

    def vypocitej_spotrebu(hodiny):
        cas_pred = max_date - timedelta(hours=hodiny)
        index_start = None
        for i in range(len(datumy)):
            if datumy[i] > cas_pred:
                index_start = i - 1 if i > 0 else 0
                break
        if index_start is None:
            index_start = 0
        index_end = len(datumy) - 1
        if index_end > index_start:
            spotreba = impulsy[index_end] - impulsy[index_start]
            date_from = datumy[index_start]
            date_to = datumy[index_end]
        else:
            spotreba = 0
            date_from = date_to = datumy[index_end]
        return spotreba, date_from, date_to

    # Výpočty pro 2h, 12h, 24h
    spotreba_2h, date_from_2h, date_to_2h = vypocitej_spotrebu(2)
    spotreba_12h, date_from_12h, date_to_12h = vypocitej_spotrebu(12)
    spotreba_24h, date_from_24h, date_to_24h = vypocitej_spotrebu(24)

    # Vytvoření JSON výstupu
    vysledek = {
        "min_date": min_date.isoformat(),
        "max_date": max_date.isoformat(),
        "date_diff_h": date_diff_h,
        "min_value": min_value,
        "max_value": max_value,
        "spotreba_celkem": spotreba_celkem,
        "date_from_2h": date_from_2h.isoformat(),
        "date_to_2h": date_to_2h.isoformat(),
        "spotreba_2h": spotreba_2h,
        "date_from_12h": date_from_12h.isoformat(),
        "date_to_12h": date_to_12h.isoformat(),
        "spotreba_12h": spotreba_12h,
        "date_from_24h": date_from_24h.isoformat(),
        "date_to_24h": date_to_24h.isoformat(),
        "spotreba_24h": spotreba_24h
    }

    return json.dumps(vysledek, indent=2, ensure_ascii=False)

def application(environ, start_response):
    status = '200 OK'
    headers = [('Content-type', 'application/json; charset=utf-8')]
    start_response(status, headers)
    return [analyzuj_spotrebu().encode('utf-8')]

print(analyzuj_spotrebu().encode('utf-8'))
