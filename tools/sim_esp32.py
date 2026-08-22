"""Simulate the ESP32's MQTT traffic so the Node-RED dashboard can be verified
(and screenshotted) without the Wokwi simulation running.

Publishes exactly the topics main.ino publishes, and subscribes to control.* so
you can confirm the dashboard's sliders/buttons reach the device.
"""
import time, math, random, sys, threading
import paho.mqtt.client as mqtt

PREFIX = "rprms-hl-8842"
BROKER, PORT = "broker.hivemq.com", 1883
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 60
SCENARIO = sys.argv[2] if len(sys.argv) > 2 else "normal"   # normal | critical

received = []

def on_connect(c, u, f, rc, props=None):
    print(f"[sim] connected rc={rc}")
    c.subscribe(f"{PREFIX}/feeds/+")   # dots are inside one level, so "+" not "#"

def on_message(c, u, msg):
    if ".control" in msg.topic or "/control." in msg.topic:
        received.append((msg.topic, msg.payload.decode()))
        print(f"[sim] <- CONTROL {msg.topic} = {msg.payload.decode()}")

cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"rprms-sim-{random.randint(1000,9999)}")
cli.on_connect, cli.on_message = on_connect, on_message
cli.connect(BROKER, PORT, 60)
cli.loop_start()
time.sleep(2)

def P(feed, val):
    cli.publish(f"{PREFIX}/feeds/{feed}", str(val), qos=0)

hr, spo2, temp = 78.0, 97.0, 36.8
room, o2, aqi = 22.0, 20.9, 60
dose, bed, rate = 0.0, 10, 15
t0 = time.time()
n = 0
while time.time() - t0 < DURATION:
    n += 1
    if SCENARIO == "critical" and n > 4:
        hr = min(165, hr + 9); spo2 = max(82, spo2 - 1.6); temp = min(38.9, temp + 0.22)
        aqi = min(420, aqi + 45); o2 = max(18.9, o2 - 0.25)
    else:
        hr += random.uniform(-3, 3); spo2 += random.uniform(-0.5, 0.5)
        temp += random.uniform(-0.08, 0.08); room += random.uniform(-0.2, 0.2)
        o2 += random.uniform(-0.1, 0.1); aqi += random.randint(-8, 8)
        hr, spo2 = max(55, min(120, hr)), max(92, min(100, spo2))
        temp, aqi = max(36.2, min(37.6, temp)), max(20, min(140, aqi))

    crit = temp >= 38.0 or spo2 < 85 or hr >= 150
    warn = temp >= 37.5 or spo2 < 90 or hr > 120
    status = "Severe hypoxaemia" if spo2 < 85 else ("High body temperature" if temp >= 38.0
             else ("Severe tachycardia" if hr >= 150 else ("Mild fever" if temp >= 37.5
             else ("SpO2 below normal" if spo2 < 90 else "Patient stable"))))
    fstat = ("Oxygen level unsafe" if o2 < 19.5 else "Air quality hazardous" if aqi >= 300
             else "Air quality poor" if aqi >= 150 else "Environment nominal")

    P("medical.heart-rate", int(hr)); P("medical.spo2", int(spo2))
    P("medical.body-temperature", f"{temp:.2f}")
    P("medical.blood-pressure", f"{int(112+(hr-78)*0.45)}/{int(72+(hr-78)*0.22)}")
    P("medical.ecg", f"{1.2+0.4*math.sin(n/3):.2f}")
    P("medical.patient-status", status)
    if crit or warn:
        P("medical.alerts", f"[{'CRITICAL' if crit else 'WARNING'}] {status}")

    P("facility.room-temperature", f"{room:.2f}"); P("facility.humidity", f"{45+random.uniform(-3,3):.1f}")
    P("facility.oxygen-level", f"{o2:.2f}"); P("facility.aqi", int(aqi))
    P("facility.motion", random.choice([0,0,0,1]))
    if fstat != "Environment nominal":
        P("facility.alerts", f"[{'CRITICAL' if aqi>=300 or o2<19.5 else 'WARNING'}] {fstat}")

    rate = 5 if (crit or warn) else 30
    dose = min(dose + 4.5, 72.0)
    bed = 45 if spo2 < 90 else (90 if spo2 < 85 else 10)
    P("system.state", "ONLINE"); P("system.dosage", f"{dose:.1f}")
    P("system.bed-angle", bed); P("system.sampling-rate", rate)

    if n % 6 == 0:
        P("medical.summary", f'{{"n":6,"hr_avg":{hr:.1f},"hr_min":{int(hr-8)},"hr_max":{int(hr+8)},"spo2_avg":{spo2:.1f},"temp_avg":{temp:.2f}}}')
        P("facility.summary", f'{{"n":6,"room_avg":{room:.2f},"o2_avg":{o2:.2f},"aqi_avg":{int(aqi)}}}')

    print(f"[sim] -> #{n:02d} HR={int(hr)} SpO2={int(spo2)} T={temp:.1f} AQI={int(aqi)} "
          f"dose={dose:.1f} bed={bed} rate={rate}s :: {status}")
    time.sleep(3)

cli.loop_stop(); cli.disconnect()
print(f"\n[sim] done. {n} cycles published. control.* messages received: {len(received)}")
for t, p in received[-10:]: print("   ", t, "=", p)
