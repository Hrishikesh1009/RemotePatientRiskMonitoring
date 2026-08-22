"""Generate a complete Node-RED dashboard (flows.json) for the
Remote Patient Risk Monitoring System.

Two dashboard tabs mirror the two MQTT topic groups, so the role separation the
firmware enforces at the transport layer is visible in the UI.
"""
import json, pathlib

PREFIX = "rprms"          # must equal IO_USERNAME in main.ino
BROKER = "broker.hivemq.com"
PORT   = "1883"

def T(feed):  # full MQTT topic for a feed key
    return f"{PREFIX}/feeds/{feed}"

nodes = []
def add(n):
    nodes.append(n); return n["id"]

FLOW = "f_rprms"
add({"id": FLOW, "type": "tab", "label": "RPRMS — Remote Patient Risk Monitoring",
     "disabled": False, "info": "Auto-generated dashboard for the ElevanceSkills "
     "internship project. Import via Menu -> Import -> paste this file."})

# ----------------------------------------------------------------- infrastructure
add({"id": "broker1", "type": "mqtt-broker", "name": "RPRMS broker",
     "broker": BROKER, "port": PORT, "clientid": "", "autoConnect": True,
     "usetls": False, "protocolVersion": "4", "keepalive": "60", "cleansession": True,
     "autoUnsubscribe": True, "birthTopic": "", "birthQos": "0", "birthPayload": "",
     "birthMsg": {}, "closeTopic": "", "closeQos": "0", "closePayload": "", "closeMsg": {},
     "willTopic": "", "willQos": "0", "willPayload": "", "willMsg": {},
     "userProps": "", "sessionExpiry": ""})

add({"id": "ui_base", "type": "ui_base",
     "theme": {"name": "theme-dark",
               "lightTheme": {"default": "#0094CE", "baseColor": "#0094CE",
                              "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif",
                              "edited": True, "reset": False},
               "darkTheme": {"default": "#097479", "baseColor": "#1f7ae0",
                             "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif",
                             "edited": True, "reset": False},
               "customTheme": {"name": "Untitled Theme 1", "default": "#4B7930",
                               "baseColor": "#4B7930",
                               "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif"},
               "themeState": {"base-color": {"default": "#097479", "value": "#1f7ae0", "edited": True},
                              "page-titlebar-backgroundColor": {"value": "#1f7ae0", "edited": False},
                              "page-backgroundColor": {"value": "#111827", "edited": True},
                              "page-sidebar-backgroundColor": {"value": "#1f2937", "edited": True},
                              "group-textColor": {"value": "#e5e7eb", "edited": True},
                              "group-borderColor": {"value": "#374151", "edited": True},
                              "group-backgroundColor": {"value": "#1f2937", "edited": True},
                              "widget-textColor": {"value": "#f9fafb", "edited": True},
                              "widget-backgroundColor": {"value": "#1f7ae0", "edited": False},
                              "widget-borderColor": {"value": "#1f2937", "edited": True},
                              "base-font": {"value": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif"}},
               "angularTheme": {"primary": "indigo", "accents": "blue", "warn": "red",
                                "background": "grey", "palette": "light"}},
     "site": {"name": "RPRMS", "hideToolbar": "false", "allowSwipe": "false",
              "lockMenu": "false", "allowTempTheme": "true", "dateFormat": "DD/MM/YYYY",
              "sizes": {"sx": 48, "sy": 48, "gx": 6, "gy": 6, "cx": 6, "cy": 6,
                        "px": 0, "py": 0}}})

# ----------------------------------------------------------------- tabs & groups
add({"id": "tab_med",  "type": "ui_tab", "name": "Medical Staff",
     "icon": "fa-heartbeat", "order": 1, "disabled": False, "hidden": False})
add({"id": "tab_fac",  "type": "ui_tab", "name": "Facility Management",
     "icon": "fa-building", "order": 2, "disabled": False, "hidden": False})

GROUPS = [
    ("g_vitals",  "Patient Vitals",              "tab_med", 1, "12"),
    ("g_extra",   "Extended Vitals",             "tab_med", 2, "12"),
    ("g_trend",   "Vitals Trend",                "tab_med", 3, "12"),
    ("g_ctrl",    "Therapy Control (Tasks 3-5)", "tab_med", 4, "12"),
    ("g_ack",     "Actuator Readback",           "tab_med", 5, "12"),
    ("g_medalert","Clinical Alert Log",          "tab_med", 6, "12"),
    ("g_env",     "Room Environment",            "tab_fac", 1, "12"),
    ("g_envx",    "Occupancy & Humidity",        "tab_fac", 2, "12"),
    ("g_envtrend","Environment Trend",           "tab_fac", 3, "12"),
    ("g_sys",     "System Health (Task 6)",      "tab_fac", 4, "12"),
    ("g_facalert","Environmental Alert Log",     "tab_fac", 5, "12"),
]
for gid, name, tab, order, width in GROUPS:
    add({"id": gid, "type": "ui_group", "name": name, "tab": tab, "order": order,
         "disp": True, "width": width, "collapse": False, "className": ""})

GREEN, AMBER, RED = "#00b500", "#e6e600", "#ca3838"
x, y = 140, 80
def nextxy(step=60):
    global y
    y += step
    return x, y

# ----------------------------------------------------------------- gauges
# (feed, label, unit, group, order, min, max, seg1, seg2, colors, chart_series)
GAUGES = [
    ("medical.heart-rate",       "Heart Rate",  "bpm", "g_vitals", 1, 40, 180, 60,   120,  [AMBER, GREEN, RED], "Heart Rate"),
    ("medical.spo2",             "SpO2",        "%",   "g_vitals", 2, 70, 100, 85,   90,   [RED, AMBER, GREEN], "SpO2"),
    ("medical.body-temperature", "Body Temp",   "degC","g_vitals", 3, 33, 42,  37.5, 38.0, [GREEN, AMBER, RED], "Body Temp"),
    ("facility.room-temperature","Room Temp",   "degC","g_env",    1, 10, 35,  26,   30,   [GREEN, AMBER, RED], "Room Temp"),
    ("facility.oxygen-level",    "Oxygen",      "%",   "g_env",    2, 15, 25,  19.5, 23.5, [RED, GREEN, RED],   "Oxygen"),
    ("facility.aqi",             "Air Quality", "AQI", "g_env",    3, 0,  500, 150,  300,  [GREEN, AMBER, RED], "AQI"),
]
for feed, label, unit, group, order, mn, mx, s1, s2, colors, series in GAUGES:
    key = feed.replace(".", "_").replace("-", "_")
    gid, fid, mid = f"g_{key}", f"fn_{key}", f"in_{key}"
    chart = "chart_vitals" if group == "g_vitals" else "chart_env"
    cx, cy = nextxy()
    add({"id": mid, "type": "mqtt in", "z": FLOW, "name": feed, "topic": T(feed),
         "qos": "0", "datatype": "auto-detect", "broker": "broker1", "nl": False,
         "rap": True, "rh": 0, "inputs": 0, "x": cx, "y": cy, "wires": [[fid]]})
    add({"id": fid, "type": "function", "z": FLOW, "name": f"prep {series}",
         "func": f"msg.payload = Number(msg.payload);\n"
                 f"if (isNaN(msg.payload)) return null;\n"
                 f"msg.topic = {json.dumps(series)};\nreturn msg;",
         "outputs": 1, "noerr": 0, "initialize": "", "finalize": "", "libs": [],
         "x": cx + 190, "y": cy, "wires": [[gid, chart]]})
    add({"id": gid, "type": "ui_gauge", "z": FLOW, "name": label, "group": group,
         "order": order, "width": "4", "height": "4", "gtype": "gage", "title": label,
         "label": unit, "format": "{{value}}", "min": mn, "max": str(mx),
         "colors": colors, "seg1": str(s1), "seg2": str(s2), "diff": False,
         "className": "", "x": cx + 400, "y": cy, "wires": []})

# ----------------------------------------------------------------- text widgets
TEXTS = [
    ("medical.blood-pressure", "Blood Pressure", "{{msg.payload}} mmHg", "g_extra", 1),
    ("medical.ecg",            "ECG amplitude",  "{{msg.payload}} mV",   "g_extra", 2),
    ("medical.patient-status", "Clinical Status","{{msg.payload}}",      "g_extra", 3),
    ("medical.summary",        "60 s Aggregate", "{{msg.payload}}",      "g_extra", 4),
    ("facility.humidity",      "Humidity",       "{{msg.payload}} %",    "g_envx",  1),
    ("facility.motion",        "Room Occupancy", "{{msg.payload}}",      "g_envx",  2),
    ("facility.summary",       "60 s Aggregate", "{{msg.payload}}",      "g_envx",  3),
    ("system.state",           "Link State",     "{{msg.payload}}",      "g_sys",   1),
    ("system.sampling-rate",   "Sampling Interval", "{{msg.payload}} s", "g_sys",   2),
    ("system.backlog",         "Last Resynced Record", "{{msg.payload}}","g_sys",   3),
]
for feed, label, fmt, group, order in TEXTS:
    key = feed.replace(".", "_").replace("-", "_")
    tid, mid = f"t_{key}", f"in_{key}"
    cx, cy = nextxy()
    add({"id": mid, "type": "mqtt in", "z": FLOW, "name": feed, "topic": T(feed),
         "qos": "0", "datatype": "auto-detect", "broker": "broker1", "nl": False,
         "rap": True, "rh": 0, "inputs": 0, "x": cx, "y": cy, "wires": [[tid]]})
    add({"id": tid, "type": "ui_text", "z": FLOW, "group": group, "order": order,
         "width": "6", "height": "1", "name": label, "label": label, "format": fmt,
         "layout": "row-spread", "className": "", "style": False, "font": "",
         "fontSize": 16, "color": "#000000", "x": cx + 300, "y": cy, "wires": []})

# ----------------------------------------------------------------- readback gauges
for feed, label, group, order, mn, mx, s1, s2, colors in [
    ("system.dosage",    "Delivered Dose mg/hr", "g_ack", 1, 0, 100, 60, 80, [GREEN, AMBER, RED]),
    ("system.bed-angle", "Actual Bed Angle deg", "g_ack", 2, 0, 90,  91, 92, [GREEN, GREEN, GREEN]),
]:
    key = feed.replace(".", "_").replace("-", "_")
    gid, fid, mid = f"g_{key}", f"fn_{key}", f"in_{key}"
    cx, cy = nextxy()
    add({"id": mid, "type": "mqtt in", "z": FLOW, "name": feed, "topic": T(feed),
         "qos": "0", "datatype": "auto-detect", "broker": "broker1", "nl": False,
         "rap": True, "rh": 0, "inputs": 0, "x": cx, "y": cy, "wires": [[fid]]})
    add({"id": fid, "type": "function", "z": FLOW, "name": "toNumber",
         "func": "msg.payload = Number(msg.payload);\nreturn isNaN(msg.payload) ? null : msg;",
         "outputs": 1, "noerr": 0, "initialize": "", "finalize": "", "libs": [],
         "x": cx + 190, "y": cy, "wires": [[gid]]})
    add({"id": gid, "type": "ui_gauge", "z": FLOW, "name": label, "group": group,
         "order": order, "width": "6", "height": "4", "gtype": "gage", "title": label,
         "label": "", "format": "{{value}}", "min": mn, "max": str(mx),
         "colors": colors, "seg1": str(s1), "seg2": str(s2), "diff": False,
         "className": "", "x": cx + 400, "y": cy, "wires": []})

# ----------------------------------------------------------------- charts
for cid, group, label in [("chart_vitals", "g_trend", "Vitals (1 h)"),
                          ("chart_env", "g_envtrend", "Environment (1 h)")]:
    cx, cy = nextxy()
    add({"id": cid, "type": "ui_chart", "z": FLOW, "name": label, "group": group,
         "order": 1, "width": "12", "height": "6", "label": label, "chartType": "line",
         "legend": "true", "xformat": "HH:mm:ss", "interpolate": "linear",
         "nodata": "waiting for data...", "dot": False, "ymin": "", "ymax": "",
         "removeOlder": 1, "removeOlderPoints": "", "removeOlderUnit": "3600",
         "cutout": 0, "useOneColor": False, "useUTC": False,
         "colors": ["#1f7ae0", "#aec7e8", "#ff7f0e", "#2ca02c", "#98df8a",
                    "#d62728", "#ff9896", "#9467bd", "#c5b0d5"],
         "outputs": 1, "useDifferentColor": False, "className": "",
         "x": cx + 620, "y": cy, "wires": [[]]})

# ----------------------------------------------------------------- outbound control
cx, cy = nextxy(80)
OUT = "out_ctrl"
add({"id": OUT, "type": "mqtt out", "z": FLOW, "name": "-> control.*", "topic": "",
     "qos": "0", "retain": "false", "respTopic": "", "contentType": "",
     "userProps": "", "correl": "", "expiry": "", "broker": "broker1",
     "x": cx + 620, "y": cy, "wires": []})

SLIDERS = [
    ("control.dosage",        "Medication mg/hr",   "g_ctrl", 1, 0, 100, 1),
    ("control.bed-angle",     "Bed Angle deg",      "g_ctrl", 3, 0, 90,  1),
    ("control.sampling-rate", "Sampling Interval s","g_ctrl", 5, 5, 60,  1),
]
for feed, label, group, order, mn, mx, step in SLIDERS:
    key = feed.replace(".", "_").replace("-", "_")
    cx, cy = nextxy()
    add({"id": f"sl_{key}", "type": "ui_slider", "z": FLOW, "name": label,
         "label": label, "tooltip": "", "group": group, "order": order,
         "width": "8", "height": "1", "passthru": False, "outs": "end",
         "topic": T(feed), "topicType": "str", "min": mn, "max": str(mx),
         "step": step, "className": "", "x": cx, "y": cy, "wires": [[OUT]]})

SWITCHES = [
    ("control.dosage-confirm", "CONFIRM CRITICAL DOSE", "g_ctrl", 2),
    ("control.bed-auto",       "Auto Bed Control",      "g_ctrl", 4),
    ("control.sampling-mode",  "Adaptive Sampling",     "g_ctrl", 6),
]
for feed, label, group, order in SWITCHES:
    key = feed.replace(".", "_").replace("-", "_")
    cx, cy = nextxy()
    add({"id": f"sw_{key}", "type": "ui_switch", "z": FLOW, "name": label,
         "label": label, "tooltip": "", "group": group, "order": order,
         "width": "6", "height": "1", "passthru": False, "decouple": "false",
         "topic": T(feed), "topicType": "str", "style": "", "onvalue": "1",
         "onvalueType": "str", "onicon": "", "oncolor": "", "offvalue": "0",
         "offvalueType": "str", "officon": "", "offcolor": "", "animate": True,
         "className": "", "x": cx, "y": cy, "wires": [[OUT]]})

for i, (payload, label) in enumerate(
        [("sleep", "Sleep 10deg"), ("breathing", "Breathing 45deg"), ("emergency", "EMERGENCY 90deg")]):
    cx, cy = nextxy()
    add({"id": f"btn_{payload}", "type": "ui_button", "z": FLOW, "name": label,
         "group": "g_ctrl", "order": 7 + i, "width": "4", "height": "1",
         "passthru": False, "label": label, "tooltip": "",
         "color": "", "bgcolor": "#c0392b" if payload == "emergency" else "",
         "className": "", "icon": "", "payload": payload, "payloadType": "str",
         "topic": T("control.bed-preset"), "topicType": "str",
         "x": cx, "y": cy, "wires": [[OUT]]})

# ----------------------------------------------------------------- alert logs
ALERT_TMPL = (
    '<div style="max-height:200px;overflow-y:auto;font-family:Consolas,monospace;'
    'font-size:12px;line-height:1.6">\n'
    '  <div ng-repeat="line in msg.payload track by $index"\n'
    '       style="padding:2px 6px;border-bottom:1px solid rgba(255,255,255,.08)">\n'
    '    {{line}}\n  </div>\n'
    '  <div ng-if="!msg.payload.length" style="opacity:.6">no alerts yet</div>\n'
    '</div>'
)
for feed, group, name in [("medical.alerts", "g_medalert", "clinical"),
                          ("facility.alerts", "g_facalert", "environmental")]:
    key = feed.replace(".", "_")
    cx, cy = nextxy()
    add({"id": f"in_{key}", "type": "mqtt in", "z": FLOW, "name": feed, "topic": T(feed),
         "qos": "0", "datatype": "auto-detect", "broker": "broker1", "nl": False,
         "rap": True, "rh": 0, "inputs": 0, "x": cx, "y": cy, "wires": [[f"fn_{key}"]]})
    add({"id": f"fn_{key}", "type": "function", "z": FLOW, "name": f"{name} log (last 30)",
         "func": "let log = context.get('log') || [];\n"
                 "const ts = new Date().toTimeString().slice(0,8);\n"
                 "log.unshift(ts + '  ' + msg.payload);\n"
                 "if (log.length > 30) log = log.slice(0,30);\n"
                 "context.set('log', log);\n"
                 "msg.payload = log;\nreturn msg;",
         "outputs": 1, "noerr": 0, "initialize": "", "finalize": "", "libs": [],
         "x": cx + 210, "y": cy, "wires": [[f"tpl_{key}"]]})
    add({"id": f"tpl_{key}", "type": "ui_template", "z": FLOW, "group": group,
         "name": f"{name} alert log", "order": 1, "width": "12", "height": "5",
         "format": ALERT_TMPL, "storeOutMessages": True, "fwdInMessages": True,
         "resendOnRefresh": True, "templateScope": "local", "className": "",
         "x": cx + 440, "y": cy, "wires": [[]]})

out = pathlib.Path(r"D:\elevanceskills\RemotePatientRiskMonitoring\dashboards\node-red-flows.json")
out.write_text(json.dumps(nodes, indent=2), encoding="utf-8")

# ---- validation ----
ids = [n["id"] for n in nodes]
assert len(ids) == len(set(ids)), "duplicate node ids"
known = set(ids)
dangling = []
for n in nodes:
    for w in n.get("wires", []):
        for t in w:
            if t not in known:
                dangling.append((n["id"], t))
    for ref in ("group", "tab", "broker", "z"):
        v = n.get(ref)
        if isinstance(v, str) and v and v not in known:
            dangling.append((n["id"], f"{ref}={v}"))

print(f"nodes: {len(nodes)}")
print(f"dangling refs: {dangling or 'none'}")
from collections import Counter
print("by type:", dict(Counter(n["type"] for n in nodes)))
print("bytes:", out.stat().st_size)
