/* =====================================================================================
 *  REMOTE PATIENT RISK MONITORING SYSTEM  --  Integrated Internship Build
 *  ElevanceSkills Internship  |  Intern: Hrishikesh Lokhande
 *
 *  Single integrated Wokwi/ESP32 project implementing all six internship tasks:
 *
 *    T1  Multi-Sensor Expansion Using FreeRTOS (concurrent system design)
 *    T2  Dual-Role IoT Monitoring System (role-based dashboards)
 *    T3  Smart Medication Dosage Adjustment System
 *    T4  Intelligent Remote Bed Elevation Control
 *    T5  Smart Dynamic Sampling Rate (adaptive monitoring)
 *    T6  Advanced Offline Detection (fault-tolerant system)
 *
 *  Every block below is tagged with the task(s) it implements, e.g. [T1][T5].
 *  Built on top of the original training sketch (main.ino) -- original pin map,
 *  Adafruit IO transport and OLED/DS18B20/PIR/buzzer wiring are preserved.
 * ===================================================================================== */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"

/* =====================================================================================
 *  SECTION 0 -- CONFIGURATION
 *  Edit ONLY this section before uploading / running in Wokwi.
 * ===================================================================================== */

#define WIFI_SSID   "Wokwi-GUEST"
#define WIFI_PASS   ""

/* ---- Adafruit IO credentials -------------------------------------------------------
 * Replace with your own username and Active Key from https://io.adafruit.com/
 * (Profile -> My Key). The system still boots and runs fully offline without them,
 * which is exactly the behaviour Task 6 demonstrates.
 * ----------------------------------------------------------------------------------- */
#define IO_USERNAME "[Username]"
#define IO_KEY      "[Key]"

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883

/* ---- Adafruit IO free tier allows only 10 feeds. ------------------------------------
 * Set to 0 to publish only the 10 essential feeds (see docs/DASHBOARD_SETUP.md).
 * Set to 1 for the full 20-feed build (needs Adafruit IO Plus, or any generic broker).
 * ----------------------------------------------------------------------------------- */
#define ENABLE_EXTENDED_FEEDS 1

/* ---- Optional SPIFFS persistence for the offline buffer [T6] ------------------------
 * The RAM ring buffer below always works. SPIFFS adds power-loss durability but needs a
 * SPIFFS partition; leave at 0 if your Wokwi board profile has no SPIFFS partition.
 * ----------------------------------------------------------------------------------- */
#define USE_SPIFFS 0

#if USE_SPIFFS
  #include <SPIFFS.h>
  #define OFFLINE_FILE "/backlog.csv"
#endif

/* =====================================================================================
 *  SECTION 1 -- PIN MAP
 *  GPIO 14/12/4/16/17/5/32/33/25/26/21/22 are inherited from the original build.
 *  GPIO 27/36/34/35/13/15/2/18 are new hardware added for T1..T4.
 * ===================================================================================== */

/* --- inherited --- */
#define TEMP_SENSOR      14   // DS18B20 -- patient body temperature
#define MOTION_DETECTOR  12   // PIR     -- patient movement / bed exit
#define BUZZER_PIN_1      4   // body temperature alarm
#define BUZZER_PIN_2     16   // heart rate alarm
#define BUZZER_PIN_3     17   // SpO2 / medication alarm
#define BUZZER_PIN_4      5   // motion + connectivity alarm            [T6]
#define SW_TEMP          32   // simulate body-temp sensor disconnect
#define SW_HR            33   // simulate heart-rate sensor disconnect
#define SW_SPO2          25   // simulate SpO2 sensor disconnect
#define SW_MOTION        26   // simulate PIR disconnect
#define I2C_SDA          21
#define I2C_SCL          22

/* --- new for this build --- */
#define DHT_PIN          27   // DHT22   -- facility room temp + humidity   [T1][T2]
#define PIN_ECG          36   // POT (ADC1_CH0, input-only) -- ECG amplitude [T1]
#define PIN_O2           34   // POT (ADC1_CH6) -- room oxygen level        [T2]
#define PIN_AQI          35   // POT (ADC1_CH7) -- air quality index        [T2]
#define SERVO_PIN        13   // SG90 servo -- bed elevation                [T4]
#define LED_DOSAGE       15   // RED   -- medication critical               [T3]
#define LED_OK            2   // GREEN -- system healthy heartbeat
#define LED_NET          18   // AMBER -- degraded / offline                [T6]

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS 0x3C

#define DHT_TYPE DHT22

/* =====================================================================================
 *  SECTION 2 -- CLINICAL & FACILITY THRESHOLDS
 *  NOTE: the original sketch tested `heartRate > 300` while the source was
 *  random(45,300) -- that branch could never fire. Replaced with clinical limits.
 * ===================================================================================== */

#define TEMP_WARN_C      37.5f
#define TEMP_CRIT_C      38.0f
#define HR_LOW_WARN        60
#define HR_LOW_CRIT        50
#define HR_HIGH_WARN      120
#define HR_HIGH_CRIT      150
#define SPO2_WARN          90
#define SPO2_CRIT          85
#define BP_SYS_HIGH_WARN  140
#define BP_SYS_LOW_WARN    90

#define ROOM_TEMP_MIN_C  18.0f
#define ROOM_TEMP_MAX_C  26.0f
#define O2_MIN_PCT       19.5f
#define O2_MAX_PCT       23.5f
#define AQI_WARN          150
#define AQI_CRIT          300

/* --- T3 medication dosage --- */
#define DOSAGE_MIN_MGH        0.0f
#define DOSAGE_MAX_MGH      100.0f
#define DOSAGE_WARN_MGH      60.0f   // warning band starts here
#define DOSAGE_CRIT_MGH      80.0f   // above this -> red LED + buzzer + confirmation
#define DOSAGE_RAMP_MGH_S     5.0f   // rate limit: max 5 mg/hr change per second
#define DOSAGE_CONFIRM_MS  15000UL   // window to confirm a critical dosage request

/* --- T4 bed elevation --- */
#define BED_MIN_DEG            0
#define BED_MAX_DEG           90
#define BED_STEP_DEG           1     // smooth transition granularity
#define BED_STEP_MS           25     // 1 deg / 25 ms  -> 0..90 deg in ~2.3 s
#define BED_EMERGENCY_STEP     3     // emergency preset moves faster but still ramps
#define BED_PRESET_SLEEP      10
#define BED_PRESET_BREATH     45
#define BED_PRESET_EMERGENCY  90

/* --- T5 adaptive sampling --- */
#define SAMPLING_MIN_S         5
#define SAMPLING_MAX_S        60
#define SAMPLING_DEFAULT_S    15
#define SAMPLING_ABNORMAL_S    5     // auto mode: fast poll when patient is abnormal
#define SAMPLING_STABLE_S     30     // auto mode: slow poll to conserve battery
#define SAMPLING_STABLE_CYCLES 3     // consecutive normal cycles before slowing down

/* --- T6 connectivity / fault tolerance --- */
#define OFFLINE_BUFFER_DEPTH  120    // ~30 min at 15 s sampling
#define BACKOFF_MIN_S           1
#define BACKOFF_MAX_S          60
#define RESYNC_GAP_MS         600    // throttle backlog replay (API rate limits)
#define DEGRADED_FAIL_COUNT     2    // publish failures before declaring DEGRADED
#define WDT_TIMEOUT_S          20

#define AGGREGATION_PERIOD_S   60    // [T2] periodic summary publication

/* =====================================================================================
 *  SECTION 3 -- MQTT FEED TOPICS  [T2] role-based separation via Adafruit IO groups
 *
 *    medical.*   -> Medical Staff Dashboard   (patient vitals, clinical alerts)
 *    facility.*  -> Facility Management Dashboard (environment, air, occupancy)
 *    system.*    -> device telemetry / actuator readback
 *    control.*   -> INBOUND commands from dashboard sliders and buttons
 *
 *  Because the three read groups use distinct topic prefixes, Adafruit IO (or any
 *  broker with topic ACLs) can grant clinical staff and facilities staff access to
 *  only their own subtree -- that is the "role-based access" the task asks for.
 * ===================================================================================== */

#define F_MED_TEMP    IO_USERNAME "/feeds/medical.body-temperature"
#define F_MED_HR      IO_USERNAME "/feeds/medical.heart-rate"
#define F_MED_SPO2    IO_USERNAME "/feeds/medical.spo2"
#define F_MED_BP      IO_USERNAME "/feeds/medical.blood-pressure"
#define F_MED_ECG     IO_USERNAME "/feeds/medical.ecg"
#define F_MED_STATUS  IO_USERNAME "/feeds/medical.patient-status"
#define F_MED_ALERT   IO_USERNAME "/feeds/medical.alerts"
#define F_MED_SUM     IO_USERNAME "/feeds/medical.summary"

#define F_FAC_TEMP    IO_USERNAME "/feeds/facility.room-temperature"
#define F_FAC_HUM     IO_USERNAME "/feeds/facility.humidity"
#define F_FAC_O2      IO_USERNAME "/feeds/facility.oxygen-level"
#define F_FAC_AQI     IO_USERNAME "/feeds/facility.aqi"
#define F_FAC_MOTION  IO_USERNAME "/feeds/facility.motion"
#define F_FAC_ALERT   IO_USERNAME "/feeds/facility.alerts"
#define F_FAC_SUM     IO_USERNAME "/feeds/facility.summary"

#define F_SYS_STATE   IO_USERNAME "/feeds/system.state"
#define F_SYS_DOSAGE  IO_USERNAME "/feeds/system.dosage"
#define F_SYS_BED     IO_USERNAME "/feeds/system.bed-angle"
#define F_SYS_RATE    IO_USERNAME "/feeds/system.sampling-rate"
#define F_SYS_BACKLOG IO_USERNAME "/feeds/system.backlog"

#define C_DOSAGE      IO_USERNAME "/feeds/control.dosage"
#define C_DOSE_OK     IO_USERNAME "/feeds/control.dosage-confirm"
#define C_BED_ANGLE   IO_USERNAME "/feeds/control.bed-angle"
#define C_BED_PRESET  IO_USERNAME "/feeds/control.bed-preset"
#define C_BED_AUTO    IO_USERNAME "/feeds/control.bed-auto"
#define C_RATE        IO_USERNAME "/feeds/control.sampling-rate"
#define C_RATE_MODE   IO_USERNAME "/feeds/control.sampling-mode"

/* =====================================================================================
 *  SECTION 4 -- SHARED TYPES
 * ===================================================================================== */

typedef enum { RISK_NORMAL = 0, RISK_WARNING = 1, RISK_CRITICAL = 2 } RiskLevel;
typedef enum { NET_ONLINE = 0, NET_DEGRADED = 1, NET_OFFLINE = 2 } NetState;

/* Why the link is down -- drives the distinctive buzzer patterns in [T6]. */
typedef enum { FAULT_NONE = 0, FAULT_WIFI, FAULT_MQTT, FAULT_SENSOR } FaultKind;

typedef struct {
  float    bodyTempC;
  int      heartRate;
  int      spo2;
  int      bpSystolic;
  int      bpDiastolic;
  float    ecgMv;
  bool     motion;
  bool     tempValid, hrValid, spo2Valid, motionValid;
  uint32_t seq;
} VitalsPacket;

/* Extended haemodynamic parameters. Owned by advancedVitalsTask so that blood
 * pressure and ECG are, as Task 1 requires, managed by their own FreeRTOS task
 * rather than bolted onto the core vitals loop. */
typedef struct {
  int      bpSystolic;
  int      bpDiastolic;
  float    ecgMv;
  bool     valid;
  uint32_t seq;
} AdvancedPacket;

typedef struct {
  float    roomTempC;
  float    humidity;
  float    oxygenPct;
  int      aqi;
  bool     valid;
  uint32_t seq;
} EnvPacket;

typedef enum {
  CMD_DOSAGE, CMD_DOSAGE_CONFIRM, CMD_BED_ANGLE, CMD_BED_PRESET,
  CMD_BED_AUTO, CMD_SAMPLING_RATE, CMD_SAMPLING_MODE
} CommandType;

typedef struct { CommandType type; float value; } Command;

/* One buffered sample retained while offline [T6]. */
typedef struct {
  uint32_t tsMs;
  float    bodyTempC, roomTempC, oxygenPct;
  int16_t  heartRate, spo2, bpSystolic, aqi;
  uint8_t  risk;
} BufferedRecord;

/* Everything below is guarded by stateMutex. */
typedef struct {
  VitalsPacket vitals;
  EnvPacket    env;

  RiskLevel patientRisk;
  RiskLevel facilityRisk;
  char      patientStatus[48];
  char      facilityStatus[48];

  /* [T3] */
  float    dosageSetpoint;      // raw slider value from the dashboard
  float    dosageTarget;        // after the critical-confirmation gate
  float    dosageCurrent;       // after rate limiting -- what is actually infused
  bool     dosageConfirmPending;
  uint32_t dosageRequestMs;

  /* [T4] */
  int  bedTarget;
  int  bedCurrent;
  bool bedAuto;

  /* [T5] */
  int  samplingSeconds;
  bool samplingAuto;
  int  stableCycles;

  /* [T6] */
  NetState  net;
  FaultKind fault;
  bool      sensorFault;
  uint16_t  bufferedCount;
  uint32_t  publishFailures;
  uint32_t  backoffSeconds;
  uint32_t  totalResynced;
} SystemState;

static SystemState st;

/* =====================================================================================
 *  SECTION 5 -- GLOBALS: drivers, RTOS primitives
 * ===================================================================================== */

WiFiClient           wifiClient;
Adafruit_MQTT_Client mqtt(&wifiClient, AIO_SERVER, AIO_SERVERPORT, IO_USERNAME, IO_KEY);

/* Outbound */
Adafruit_MQTT_Publish pMedTemp  (&mqtt, F_MED_TEMP);
Adafruit_MQTT_Publish pMedHr    (&mqtt, F_MED_HR);
Adafruit_MQTT_Publish pMedSpo2  (&mqtt, F_MED_SPO2);
Adafruit_MQTT_Publish pMedBp    (&mqtt, F_MED_BP);
Adafruit_MQTT_Publish pMedEcg   (&mqtt, F_MED_ECG);
Adafruit_MQTT_Publish pMedStatus(&mqtt, F_MED_STATUS);
Adafruit_MQTT_Publish pMedAlert (&mqtt, F_MED_ALERT);
Adafruit_MQTT_Publish pMedSum   (&mqtt, F_MED_SUM);

Adafruit_MQTT_Publish pFacTemp  (&mqtt, F_FAC_TEMP);
Adafruit_MQTT_Publish pFacHum   (&mqtt, F_FAC_HUM);
Adafruit_MQTT_Publish pFacO2    (&mqtt, F_FAC_O2);
Adafruit_MQTT_Publish pFacAqi   (&mqtt, F_FAC_AQI);
Adafruit_MQTT_Publish pFacMotion(&mqtt, F_FAC_MOTION);
Adafruit_MQTT_Publish pFacAlert (&mqtt, F_FAC_ALERT);
Adafruit_MQTT_Publish pFacSum   (&mqtt, F_FAC_SUM);

Adafruit_MQTT_Publish pSysState (&mqtt, F_SYS_STATE);
Adafruit_MQTT_Publish pSysDosage(&mqtt, F_SYS_DOSAGE);
Adafruit_MQTT_Publish pSysBed   (&mqtt, F_SYS_BED);
Adafruit_MQTT_Publish pSysRate  (&mqtt, F_SYS_RATE);
Adafruit_MQTT_Publish pSysBacklog(&mqtt, F_SYS_BACKLOG);

/* Inbound -- dashboard sliders and buttons */
Adafruit_MQTT_Subscribe sDosage   (&mqtt, C_DOSAGE);
Adafruit_MQTT_Subscribe sDoseOk   (&mqtt, C_DOSE_OK);
Adafruit_MQTT_Subscribe sBedAngle (&mqtt, C_BED_ANGLE);
Adafruit_MQTT_Subscribe sBedPreset(&mqtt, C_BED_PRESET);
Adafruit_MQTT_Subscribe sBedAuto  (&mqtt, C_BED_AUTO);
Adafruit_MQTT_Subscribe sRate     (&mqtt, C_RATE);
Adafruit_MQTT_Subscribe sRateMode (&mqtt, C_RATE_MODE);

OneWire            oneWire(TEMP_SENSOR);
DallasTemperature  bodySensor(&oneWire);
DHT                dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306   display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo              bedServo;

/* --- Queues [T1] --- */
QueueHandle_t qVitals;        // depth 1, overwrite  -- latest core vitals
QueueHandle_t qAdvanced;      // depth 1, overwrite  -- latest BP / ECG          [T1]
QueueHandle_t qEnv;           // depth 1, overwrite  -- latest environment
QueueHandle_t qCommand;       // depth 12            -- dashboard commands
QueueHandle_t qOffline;       // depth OFFLINE_BUFFER_DEPTH [T6] -- store-and-forward

/* One wake queue PER sampling-driven task -- a shared one would starve all but the
 * first waiter, because xQueueReceive consumes the token. [T5] */
QueueHandle_t qWakeVitals;
QueueHandle_t qWakeAdvanced;
QueueHandle_t qWakeEnv;

/* --- Mutexes [T1] --- */
SemaphoreHandle_t mState;     // guards SystemState st
SemaphoreHandle_t mI2C;       // guards the I2C bus / OLED
SemaphoreHandle_t mMqtt;      // guards the non-reentrant Adafruit_MQTT client

/* --- Binary semaphore [T1] --- */
SemaphoreHandle_t semAlert;   // processing -> alert task, "re-evaluate alarms now"

/* Convenience wrappers so lock/unlock is impossible to forget. */
static inline void lockState()   { xSemaphoreTake(mState, portMAX_DELAY); }
static inline void unlockState() { xSemaphoreGive(mState); }

/* =====================================================================================
 *  SECTION 6 -- HELPERS
 * ===================================================================================== */

static float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   clampI(int   v, int   lo, int   hi) { return v < lo ? lo : (v > hi ? hi : v); }

static const char *riskName(RiskLevel r) {
  switch (r) { case RISK_CRITICAL: return "CRITICAL"; case RISK_WARNING: return "WARNING"; default: return "NORMAL"; }
}
static const char *netName(NetState n) {
  switch (n) { case NET_ONLINE: return "ONLINE"; case NET_DEGRADED: return "DEGRADED"; default: return "OFFLINE"; }
}

/* Feed the task watchdog. Safe to call from any subscribed task. */
static inline void wdtFeed() { esp_task_wdt_reset(); }

/* A cancellable delay: sleeps up to `ms` on the caller's OWN wake queue, returning
 * early if the sampling rate changed. Chunked at 1 s so the watchdog is always fed
 * even on a 60 s sampling period. [T1 watchdog] [T5 dynamic rate]
 *
 * Each sensor task must pass its own queue. A single shared queue would be a bug:
 * xQueueReceive CONSUMES the token, so only the first task to wake would see the
 * rate change and the others would sleep out their full period.
 *
 * Blocking on a queue with a timeout is the FreeRTOS non-blocking-delay idiom -- the
 * task is descheduled and burns zero CPU exactly as vTaskDelay() would, but it can
 * also be woken early. vTaskDelay() itself is used directly in the six tasks that
 * have no rate-change dependency. */
static void interruptibleDelay(uint32_t ms, QueueHandle_t wakeQueue) {
  uint32_t left = ms;
  uint8_t  dummy;
  while (left > 0) {
    uint32_t slice = left > 1000 ? 1000 : left;
    if (xQueueReceive(wakeQueue, &dummy, pdMS_TO_TICKS(slice)) == pdTRUE) {
      wdtFeed();
      return;                     // rate changed -- resample immediately
    }
    wdtFeed();
    left -= slice;
  }
}

/* Wakes every sampling-driven task. One token per queue, so no task is starved. */
static void notifySamplingChanged() {
  uint8_t token = 1;
  xQueueOverwrite(qWakeVitals, &token);
  xQueueOverwrite(qWakeAdvanced, &token);
  xQueueOverwrite(qWakeEnv, &token);
}

/* =====================================================================================
 *  SECTION 7 -- TASK: VITALS SENSING                       [T1] priority 4
 *  Body temperature, heart rate, SpO2, blood pressure, ECG, motion.
 *  The four slide switches simulate individual sensor disconnects, which is what
 *  drives FAULT_SENSOR and the DEGRADED state in [T6].
 * ===================================================================================== */

static void vitalsSensorTask(void *param) {
  esp_task_wdt_add(NULL);

  VitalsPacket v = {};
  uint32_t seq = 0;

  /* Smooth random-walk baselines so the dashboard gauges look clinical, not noisy. */
  float hrBase   = 78.0f;
  float spo2Base = 97.0f;

  for (;;) {
    v.tempValid   = !digitalRead(SW_TEMP);
    v.hrValid     = !digitalRead(SW_HR);
    v.spo2Valid   = !digitalRead(SW_SPO2);
    v.motionValid = !digitalRead(SW_MOTION);

    /* ---- body temperature (DS18B20) ---- */
    if (v.tempValid) {
      bodySensor.requestTemperatures();
      float t = bodySensor.getTempCByIndex(0);
      v.bodyTempC = (t > -50.0f && t < 100.0f) ? t : NAN;
    } else {
      v.bodyTempC = NAN;
    }

    /* ---- heart rate (simulated pulse-oximeter channel) ---- */
    if (v.hrValid) {
      hrBase += random(-400, 401) / 100.0f;
      if (random(0, 100) < 4) hrBase += random(0, 2) ? 35.0f : -22.0f;   // occasional event
      hrBase = clampF(hrBase, 40.0f, 175.0f);
      v.heartRate = (int)(hrBase + 0.5f);
    } else {
      v.heartRate = 0;
    }

    /* ---- SpO2 ---- */
    if (v.spo2Valid) {
      spo2Base += random(-80, 81) / 100.0f;
      if (v.heartRate > HR_HIGH_WARN) spo2Base -= 0.6f;                  // tachycardia desat
      spo2Base = clampF(spo2Base, 78.0f, 100.0f);
      v.spo2 = (int)(spo2Base + 0.5f);
    } else {
      v.spo2 = 0;
    }

    /* BP and ECG are NOT read here -- advancedVitalsTask owns them. */

    /* ---- motion (PIR) ---- */
    v.motion = v.motionValid && digitalRead(MOTION_DETECTOR);

    v.seq = ++seq;
    xQueueOverwrite(qVitals, &v);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    interruptibleDelay((uint32_t)periodS * 1000UL, qWakeVitals);         // [T5]
  }
}

/* =====================================================================================
 *  SECTION 7b -- TASK: ADVANCED VITALS (BLOOD PRESSURE + ECG)   [T1] priority 3
 *
 *  Task 1 asks for the additional health parameters to be "managed by separate
 *  FreeRTOS tasks", so blood pressure and ECG get their own task rather than being
 *  appended to the core vitals loop.
 *
 *  It consumes heart rate from qVitals (BP is derived with physiological correlation)
 *  and produces qAdvanced -- a genuine task-to-task data dependency carried over a
 *  queue, which is exactly the concurrency pattern the task is asking to see.
 * ===================================================================================== */

static void advancedVitalsTask(void *param) {
  esp_task_wdt_add(NULL);

  AdvancedPacket a = {};
  VitalsPacket   v = {};
  uint32_t seq = 0;

  for (;;) {
    xQueuePeek(qVitals, &v, 0);            // upstream dependency: needs heart rate

    /* ---- non-invasive blood pressure, correlated with heart rate ---- */
    if (v.hrValid && v.heartRate > 0) {
      a.bpSystolic  = clampI((int)(112 + (v.heartRate - 78) * 0.45f + random(-4, 5)), 70, 210);
      a.bpDiastolic = clampI((int)(72  + (v.heartRate - 78) * 0.22f + random(-3, 4)), 40, 130);
      a.valid       = true;
    } else {
      a.bpSystolic = a.bpDiastolic = 0;
      a.valid      = false;                // HR probe off -> BP cannot be derived
    }

    /* ---- ECG amplitude from the potentiometer on GPIO36 (ADC1) ---- */
    a.ecgMv = (analogRead(PIN_ECG) / 4095.0f) * 5.0f;                    // 0.00 .. 5.00 mV

    a.seq = ++seq;
    xQueueOverwrite(qAdvanced, &a);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    interruptibleDelay((uint32_t)periodS * 1000UL, qWakeAdvanced);       // [T5]
  }
}

/* =====================================================================================
 *  SECTION 8 -- TASK: ENVIRONMENT SENSING                  [T1][T2] priority 3
 *  Facility-side parameters, published to a separate dashboard group.
 * ===================================================================================== */

static void envSensorTask(void *param) {
  esp_task_wdt_add(NULL);

  EnvPacket e = {};
  uint32_t seq = 0;

  for (;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    e.valid     = !(isnan(t) || isnan(h));
    e.roomTempC = e.valid ? t : NAN;
    e.humidity  = e.valid ? h : NAN;

    /* Oxygen concentration 15.0 .. 25.0 %, AQI 0 .. 500, from the two pots. */
    e.oxygenPct = 15.0f + (analogRead(PIN_O2)  / 4095.0f) * 10.0f;
    e.aqi       = (int)((analogRead(PIN_AQI) / 4095.0f) * 500.0f);

    e.seq = ++seq;
    xQueueOverwrite(qEnv, &e);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    /* Environment drifts slowly -- poll at half the vitals rate, floor 5 s. */
    interruptibleDelay((uint32_t)max(SAMPLING_MIN_S, periodS * 2) * 1000UL, qWakeEnv);
  }
}

/* =====================================================================================
 *  SECTION 9 -- TASK: PROCESSING / RISK ENGINE             [T1][T5][T6] priority 5
 *  Fuses both sensor streams, scores clinical + facility risk, drives adaptive
 *  sampling, triggers the alert task, and buffers samples while offline.
 * ===================================================================================== */

static void bufferSample(const VitalsPacket &v, const EnvPacket &e, RiskLevel risk);

static void processingTask(void *param) {
  esp_task_wdt_add(NULL);

  VitalsPacket   v = {};
  AdvancedPacket a = {};
  EnvPacket      e = {};
  uint32_t lastBufferMs = 0;         // [T6] paces offline buffering to the sampling rate

  for (;;) {
    xQueuePeek(qVitals,   &v, 0);
    xQueuePeek(qAdvanced, &a, 0);
    xQueuePeek(qEnv,      &e, 0);

    /* Fold the advanced task's output into the vitals record the rest of the system
     * consumes, so display / MQTT / buffering all see one coherent patient snapshot. */
    v.bpSystolic  = a.bpSystolic;
    v.bpDiastolic = a.bpDiastolic;
    v.ecgMv       = a.ecgMv;

    /* ------------------ clinical risk ------------------ */
    RiskLevel pRisk = RISK_NORMAL;
    char pMsg[48];
    strcpy(pMsg, "Patient stable");

    if (v.tempValid && !isnan(v.bodyTempC) && v.bodyTempC >= TEMP_CRIT_C) {
      pRisk = RISK_CRITICAL; strcpy(pMsg, "High body temperature");
    } else if (v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_CRIT) {
      pRisk = RISK_CRITICAL; strcpy(pMsg, "Severe hypoxaemia");
    } else if (v.hrValid && v.heartRate > 0 &&
              (v.heartRate <= HR_LOW_CRIT || v.heartRate >= HR_HIGH_CRIT)) {
      pRisk = RISK_CRITICAL;
      strcpy(pMsg, v.heartRate <= HR_LOW_CRIT ? "Severe bradycardia" : "Severe tachycardia");
    } else if (v.tempValid && !isnan(v.bodyTempC) && v.bodyTempC >= TEMP_WARN_C) {
      pRisk = RISK_WARNING;  strcpy(pMsg, "Mild fever");
    } else if (v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_WARN) {
      pRisk = RISK_WARNING;  strcpy(pMsg, "SpO2 below normal");
    } else if (v.hrValid && v.heartRate > 0 &&
              (v.heartRate < HR_LOW_WARN || v.heartRate > HR_HIGH_WARN)) {
      pRisk = RISK_WARNING;  strcpy(pMsg, "Heart rate abnormal");
    } else if (v.hrValid && (v.bpSystolic > BP_SYS_HIGH_WARN || v.bpSystolic < BP_SYS_LOW_WARN)) {
      pRisk = RISK_WARNING;  strcpy(pMsg, "Blood pressure abnormal");
    } else if (v.motion) {
      strcpy(pMsg, "Patient movement");
    }

    /* A disconnected sensor is itself a clinical risk -- never report "stable" blind. */
    bool sensorFault = !(v.tempValid && v.hrValid && v.spo2Valid);
    if (sensorFault && pRisk == RISK_NORMAL) {
      pRisk = RISK_WARNING; strcpy(pMsg, "Sensor disconnected");
    }

    /* ------------------ facility risk ------------------ [T2] */
    RiskLevel fRisk = RISK_NORMAL;
    char fMsg[48];
    strcpy(fMsg, "Environment nominal");

    if (e.oxygenPct < O2_MIN_PCT || e.oxygenPct > O2_MAX_PCT) {
      fRisk = RISK_CRITICAL; strcpy(fMsg, "Oxygen level unsafe");
    } else if (e.aqi >= AQI_CRIT) {
      fRisk = RISK_CRITICAL; strcpy(fMsg, "Air quality hazardous");
    } else if (e.aqi >= AQI_WARN) {
      fRisk = RISK_WARNING;  strcpy(fMsg, "Air quality poor");
    } else if (e.valid && (e.roomTempC < ROOM_TEMP_MIN_C || e.roomTempC > ROOM_TEMP_MAX_C)) {
      fRisk = RISK_WARNING;  strcpy(fMsg, "Room temp out of range");
    } else if (!e.valid) {
      fRisk = RISK_WARNING;  strcpy(fMsg, "Env sensor fault");
    }

    /* ------------------ commit shared state ------------------ */
    NetState  netNow;
    RiskLevel worst = pRisk > fRisk ? pRisk : fRisk;

    lockState();
      st.vitals       = v;
      st.env          = e;
      st.patientRisk  = pRisk;
      st.facilityRisk = fRisk;
      strncpy(st.patientStatus,  pMsg, sizeof(st.patientStatus) - 1);
      strncpy(st.facilityStatus, fMsg, sizeof(st.facilityStatus) - 1);

      /* Reported to the connectivity supervisor, which owns st.fault / st.net. */
      st.sensorFault = sensorFault;

      /* -------- [T5] adaptive sampling -------- */
      int prevRate = st.samplingSeconds;
      if (st.samplingAuto) {
        if (worst != RISK_NORMAL) {
          st.stableCycles = 0;
          st.samplingSeconds = SAMPLING_ABNORMAL_S;      // deteriorating -> watch closely
        } else if (++st.stableCycles >= SAMPLING_STABLE_CYCLES) {
          st.samplingSeconds = SAMPLING_STABLE_S;        // stable -> conserve battery
        }
      }
      int newRate = st.samplingSeconds;

      /* -------- [T4] automatic bed elevation -------- */
      if (st.bedAuto) {
        int want = st.bedTarget;
        if (pRisk == RISK_CRITICAL && v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_CRIT) {
          want = BED_PRESET_EMERGENCY;                   // severe desat -> full upright
        } else if (v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_WARN) {
          want = BED_PRESET_BREATH;                      // breathing support
        } else if (pRisk == RISK_NORMAL) {
          want = BED_PRESET_SLEEP;                       // resting position
        }
        st.bedTarget = clampI(want, BED_MIN_DEG, BED_MAX_DEG);
      }

      netNow = st.net;
    unlockState();

    if (newRate != prevRate) notifySamplingChanged();

    /* -------- [T6] store-and-forward while the link is down --------
     * This task runs at 1 Hz, but the buffer must hold one record per SAMPLING
     * period -- otherwise a 15 s configured rate would still fill the 120-slot
     * buffer in two minutes instead of thirty. */
    uint32_t nowMs = millis();
    if (netNow == NET_OFFLINE) {
      if (lastBufferMs == 0 || (nowMs - lastBufferMs) >= (uint32_t)newRate * 1000UL) {
        lastBufferMs = nowMs;
        bufferSample(v, e, worst);
      }
    } else {
      lastBufferMs = 0;              // re-arm so the first offline sample is immediate
    }

    xSemaphoreGive(semAlert);          // wake the alert task
    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(1000));   // risk engine runs at a fixed 1 Hz
  }
}

/* =====================================================================================
 *  SECTION 10 -- TASK: ALERTS                              [T1][T3][T6] priority 6 (highest)
 *  Owns every buzzer and LED. Nothing else in the system touches them, so alarm
 *  timing can never be distorted by display refreshes or network stalls.
 * ===================================================================================== */

static void beep(int pin, int onMs, int offMs, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH); vTaskDelay(pdMS_TO_TICKS(onMs));
    digitalWrite(pin, LOW);  vTaskDelay(pdMS_TO_TICKS(offMs));
  }
}

static void alertTask(void *param) {
  esp_task_wdt_add(NULL);

  uint32_t lastNetBeepMs = 0;
  bool     okLed = false;

  for (;;) {
    /* Wait for the risk engine, but never block forever -- alarms must keep sounding. */
    xSemaphoreTake(semAlert, pdMS_TO_TICKS(500));

    VitalsPacket v; RiskLevel pRisk; NetState nState; FaultKind fault;
    float dose; RiskLevel fRisk;

    lockState();
      v      = st.vitals;
      pRisk  = st.patientRisk;
      fRisk  = st.facilityRisk;
      nState = st.net;
      fault  = st.fault;
      dose   = st.dosageCurrent;
    unlockState();

    /* ---- clinical alarms (inherited buzzer assignment) ---- */
    if (v.tempValid && !isnan(v.bodyTempC) && v.bodyTempC >= TEMP_CRIT_C)
      beep(BUZZER_PIN_1, 60, 60, 2);

    if (v.hrValid && v.heartRate > 0 &&
        (v.heartRate <= HR_LOW_CRIT || v.heartRate >= HR_HIGH_CRIT))
      beep(BUZZER_PIN_2, 60, 60, 3);
    else if (v.hrValid && v.heartRate > 0 &&
        (v.heartRate < HR_LOW_WARN || v.heartRate > HR_HIGH_WARN))
      beep(BUZZER_PIN_2, 50, 200, 1);

    if (v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_CRIT)
      beep(BUZZER_PIN_3, 80, 60, 3);

    if (v.motion)
      beep(BUZZER_PIN_4, 40, 40, 1);

    /* ---- [T3] medication dosage safety ----
     * Above the critical threshold the red LED blinks rapidly and the buzzer sounds. */
    if (dose > DOSAGE_CRIT_MGH) {
      for (int i = 0; i < 5; i++) {
        digitalWrite(LED_DOSAGE, HIGH); vTaskDelay(pdMS_TO_TICKS(60));
        digitalWrite(LED_DOSAGE, LOW);  vTaskDelay(pdMS_TO_TICKS(60));
      }
      beep(BUZZER_PIN_3, 120, 80, 2);
    } else if (dose >= DOSAGE_WARN_MGH) {
      digitalWrite(LED_DOSAGE, HIGH);   // steady amber-equivalent warning
      vTaskDelay(pdMS_TO_TICKS(150));
      digitalWrite(LED_DOSAGE, LOW);
    } else {
      digitalWrite(LED_DOSAGE, LOW);
    }

    /* ---- [T6] connectivity alarms, one distinct pattern per failure type ---- */
    uint32_t nowMs = millis();
    if (nState != NET_ONLINE && nowMs - lastNetBeepMs > 5000UL) {
      lastNetBeepMs = nowMs;
      switch (fault) {
        case FAULT_WIFI:   beep(BUZZER_PIN_4, 80,  80,  3); break;  // 3 short  = Wi-Fi lost
        case FAULT_MQTT:   beep(BUZZER_PIN_4, 400, 150, 2); break;  // 2 long   = broker lost
        case FAULT_SENSOR: beep(BUZZER_PIN_4, 60, 300,  1); break;  // 1 chirp  = sensor fault
        default:           beep(BUZZER_PIN_4, 60, 300,  1); break;
      }
    }
    digitalWrite(LED_NET, nState == NET_ONLINE ? LOW : HIGH);

    /* ---- facility alarm ---- [T2] */
    if (fRisk == RISK_CRITICAL) beep(BUZZER_PIN_1, 200, 100, 1);

    /* ---- healthy-system heartbeat ---- */
    okLed = (pRisk == RISK_NORMAL && nState == NET_ONLINE) ? !okLed : false;
    digitalWrite(LED_OK, okLed);

    wdtFeed();
  }
}

/* =====================================================================================
 *  SECTION 11 -- TASK: OLED DISPLAY                        [T1][T3][T4][T6] priority 2
 *  Five rotating pages. Offline mode pre-empts the rotation with a fixed banner.
 * ===================================================================================== */

static void displayTask(void *param) {
  esp_task_wdt_add(NULL);

  uint8_t page = 0;

  for (;;) {
    SystemState s;
    lockState(); s = st; unlockState();

    xSemaphoreTake(mI2C, portMAX_DELAY);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (s.net == NET_OFFLINE) {
      /* ---- [T6] mandated offline banner ---- */
      display.setTextSize(1);
      display.setCursor(0, 0);  display.print(F("*** LINK FAILURE ***"));
      display.setTextSize(1);
      display.setCursor(0, 16); display.print(F("LOGGING OFFLINE"));
      display.setCursor(0, 30);
      display.print(F("Cause: "));
      display.print(s.fault == FAULT_WIFI ? F("WIFI")
                   : s.fault == FAULT_MQTT ? F("MQTT") : F("SENSOR"));
      display.setCursor(0, 42);
      display.print(F("Buffered: ")); display.print(s.bufferedCount);
      display.setCursor(0, 54);
      display.print(F("Retry in ")); display.print(s.backoffSeconds); display.print(F("s"));
    } else {
      switch (page) {
        case 0:   /* patient vitals */
          display.setCursor(0, 0);  display.print(F("PATIENT VITALS"));
          display.setCursor(0, 14); display.print(F("Temp: "));
          if (s.vitals.tempValid && !isnan(s.vitals.bodyTempC)) {
            display.print(s.vitals.bodyTempC, 1); display.print(F(" C"));
          } else display.print(F("--"));
          display.setCursor(0, 26); display.print(F("HR  : "));
          if (s.vitals.hrValid) display.print(s.vitals.heartRate); else display.print(F("--"));
          display.print(F(" bpm"));
          display.setCursor(0, 38); display.print(F("SpO2: "));
          if (s.vitals.spo2Valid) display.print(s.vitals.spo2); else display.print(F("--"));
          display.print(F(" %"));
          display.setCursor(0, 52); display.print(s.patientStatus);
          break;

        case 1:   /* extended vitals [T1] */
          display.setCursor(0, 0);  display.print(F("EXTENDED VITALS"));
          display.setCursor(0, 14); display.print(F("BP  : "));
          display.print(s.vitals.bpSystolic); display.print(F("/"));
          display.print(s.vitals.bpDiastolic); display.print(F(" mmHg"));
          display.setCursor(0, 26); display.print(F("ECG : "));
          display.print(s.vitals.ecgMv, 2); display.print(F(" mV"));
          display.setCursor(0, 38); display.print(F("Move: "));
          display.print(s.vitals.motion ? F("DETECTED") : F("none"));
          display.setCursor(0, 52); display.print(F("Risk: "));
          display.print(riskName(s.patientRisk));
          break;

        case 2:   /* facility [T2] */
          display.setCursor(0, 0);  display.print(F("FACILITY / ROOM"));
          display.setCursor(0, 14); display.print(F("Room: "));
          if (s.env.valid) { display.print(s.env.roomTempC, 1); display.print(F(" C")); }
          else display.print(F("--"));
          display.setCursor(0, 26); display.print(F("O2  : "));
          display.print(s.env.oxygenPct, 1); display.print(F(" %"));
          display.setCursor(0, 38); display.print(F("AQI : "));
          display.print(s.env.aqi);
          display.setCursor(0, 52); display.print(s.facilityStatus);
          break;

        case 3:   /* actuators [T3][T4] */
          display.setCursor(0, 0);  display.print(F("THERAPY CONTROL"));
          display.setCursor(0, 14); display.print(F("Dose: "));
          display.print(s.dosageCurrent, 1); display.print(F(" mg/hr"));
          display.setCursor(0, 26);
          if (s.dosageConfirmPending) {
            display.print(F(">CONFIRM "));
            display.print(s.dosageSetpoint, 0); display.print(F(" mg/hr"));
          } else {
            display.print(F("Level: "));
            display.print(s.dosageCurrent > DOSAGE_CRIT_MGH ? F("CRITICAL")
                        : s.dosageCurrent >= DOSAGE_WARN_MGH ? F("WARNING") : F("NORMAL"));
          }
          display.setCursor(0, 38); display.print(F("Bed : "));
          display.print(s.bedCurrent); display.print(F(" deg -> "));
          display.print(s.bedTarget);
          display.setCursor(0, 52); display.print(F("Auto bed: "));
          display.print(s.bedAuto ? F("ON") : F("OFF"));
          break;

        default:  /* system [T5][T6] */
          display.setCursor(0, 0);  display.print(F("SYSTEM STATUS"));
          display.setCursor(0, 14); display.print(F("Link: "));
          display.print(netName(s.net));
          display.setCursor(0, 26); display.print(F("Rate: "));
          display.print(s.samplingSeconds); display.print(F("s "));
          display.print(s.samplingAuto ? F("(auto)") : F("(man)"));
          display.setCursor(0, 38); display.print(F("Buf : "));
          display.print(s.bufferedCount);
          display.print(F("  Sync:")); display.print(s.totalResynced);
          display.setCursor(0, 52); display.print(F("Heap: "));
          display.print(ESP.getFreeHeap() / 1024); display.print(F(" KB"));
          break;
      }
      page = (page + 1) % 5;
    }

    display.display();
    xSemaphoreGive(mI2C);

    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

/* =====================================================================================
 *  SECTION 12 -- TASK: ACTUATORS                           [T3][T4] priority 4
 *  Ramps medication dosage and bed angle. Both are rate limited so a dashboard
 *  slider dragged from 0 to 100 can never produce an instantaneous jump.
 * ===================================================================================== */

static void actuatorTask(void *param) {
  esp_task_wdt_add(NULL);

  uint32_t lastMs = millis();

  for (;;) {
    uint32_t nowMs  = millis();
    float    dtSec  = (nowMs - lastMs) / 1000.0f;
    lastMs = nowMs;

    lockState();
      /* ---------------- [T3] dosage confirmation gate ---------------- */
      if (st.dosageConfirmPending &&
          (nowMs - st.dosageRequestMs) > DOSAGE_CONFIRM_MS) {
        /* Doctor never confirmed -- fall back to the safe ceiling. */
        st.dosageConfirmPending = false;
        st.dosageTarget = DOSAGE_CRIT_MGH;
      }

      /* ---------------- [T3] dosage rate limiting ---------------- */
      float maxStep = DOSAGE_RAMP_MGH_S * dtSec;
      float delta   = st.dosageTarget - st.dosageCurrent;
      if (fabsf(delta) <= maxStep) st.dosageCurrent = st.dosageTarget;
      else                         st.dosageCurrent += (delta > 0 ? maxStep : -maxStep);
      st.dosageCurrent = clampF(st.dosageCurrent, DOSAGE_MIN_MGH, DOSAGE_MAX_MGH);

      /* ---------------- [T4] smooth servo transition ---------------- */
      int step = (st.bedTarget == BED_PRESET_EMERGENCY) ? BED_EMERGENCY_STEP : BED_STEP_DEG;
      if (st.bedCurrent < st.bedTarget)      st.bedCurrent = min(st.bedTarget, st.bedCurrent + step);
      else if (st.bedCurrent > st.bedTarget) st.bedCurrent = max(st.bedTarget, st.bedCurrent - step);
      int bedNow = clampI(st.bedCurrent, BED_MIN_DEG, BED_MAX_DEG);
    unlockState();

    bedServo.write(bedNow);

    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(BED_STEP_MS));
  }
}

/* =====================================================================================
 *  SECTION 13 -- OFFLINE BUFFER                            [T6]
 * ===================================================================================== */

static void bufferSample(const VitalsPacket &v, const EnvPacket &e, RiskLevel risk) {
  BufferedRecord r;
  r.tsMs        = millis();
  r.bodyTempC   = v.bodyTempC;
  r.heartRate   = (int16_t)v.heartRate;
  r.spo2        = (int16_t)v.spo2;
  r.bpSystolic  = (int16_t)v.bpSystolic;
  r.roomTempC   = e.roomTempC;
  r.oxygenPct   = e.oxygenPct;
  r.aqi         = (int16_t)e.aqi;
  r.risk        = (uint8_t)risk;

  /* Ring behaviour: when full, drop the OLDEST sample so recent data always survives. */
  if (xQueueSend(qOffline, &r, 0) != pdTRUE) {
    BufferedRecord discard;
    xQueueReceive(qOffline, &discard, 0);
    xQueueSend(qOffline, &r, 0);
  }

#if USE_SPIFFS
  File f = SPIFFS.open(OFFLINE_FILE, FILE_APPEND);
  if (f) {
    f.printf("%lu,%.2f,%d,%d,%d,%.2f,%.2f,%d,%u\n",
             (unsigned long)r.tsMs, r.bodyTempC, r.heartRate, r.spo2, r.bpSystolic,
             r.roomTempC, r.oxygenPct, r.aqi, r.risk);
    f.close();
  }
#endif

  lockState(); st.bufferedCount = uxQueueMessagesWaiting(qOffline); unlockState();
}

/* Replays the backlog once the link is restored. Throttled to respect API limits. */
static uint16_t drainOfflineBuffer() {
  BufferedRecord r;
  uint16_t sent = 0;
  uint8_t  failures = 0;
  char payload[176];

  while (xQueueReceive(qOffline, &r, 0) == pdTRUE) {
    snprintf(payload, sizeof(payload),
             "{\"t\":%lu,\"bt\":%.2f,\"hr\":%d,\"spo2\":%d,\"sys\":%d,"
             "\"rt\":%.2f,\"o2\":%.2f,\"aqi\":%d,\"risk\":%u}",
             (unsigned long)r.tsMs, r.bodyTempC, r.heartRate, r.spo2, r.bpSystolic,
             r.roomTempC, r.oxygenPct, r.aqi, (unsigned)r.risk);

    if (!pSysBacklog.publish(payload)) {
      xQueueSendToFront(qOffline, &r, 0);   // link dropped again -- keep the record
      failures++;
      /* Give up this round after 3 consecutive failures. Without this, a build whose
       * system.backlog feed does not exist (free tier) would retry the same record on
       * every reconnect and never make progress. The records are retained either way. */
      if (failures >= 3) {
        Serial.println(F("[NET] backlog replay stalled -- records retained for next attempt"));
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(RESYNC_GAP_MS));
      wdtFeed();
      continue;
    }
    failures = 0;
    sent++;
    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(RESYNC_GAP_MS));
  }

#if USE_SPIFFS
  if (uxQueueMessagesWaiting(qOffline) == 0) SPIFFS.remove(OFFLINE_FILE);
#endif

  lockState();
    st.bufferedCount  = uxQueueMessagesWaiting(qOffline);
    st.totalResynced += sent;
  unlockState();
  return sent;
}

/* =====================================================================================
 *  SECTION 14 -- TASK: CONNECTIVITY STATE MACHINE          [T6] priority 3
 *
 *    ONLINE    Wi-Fi associated AND broker connected AND publishes succeeding
 *    DEGRADED  link technically up but publishes failing, or a sensor is faulted
 *    OFFLINE   Wi-Fi down or broker unreachable  -> buffer everything
 *
 *  Reconnection uses exponential backoff 1,2,4,8,16,32,60 s (capped).
 * ===================================================================================== */

static void connectivityTask(void *param) {
  esp_task_wdt_add(NULL);

  uint32_t backoff = BACKOFF_MIN_S;
  uint32_t nextAttemptMs = 0;

  for (;;) {
    bool wifiUp = (WiFi.status() == WL_CONNECTED);

    bool mqttUp = false;
    if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(2000)) == pdTRUE) {
      mqttUp = mqtt.connected();
      xSemaphoreGive(mMqtt);
    }

    uint32_t fails; bool sensorFault;
    lockState(); fails = st.publishFailures; sensorFault = st.sensorFault; unlockState();

    NetState  newState;
    FaultKind newFault = FAULT_NONE;

    if (!wifiUp) {
      newState = NET_OFFLINE;  newFault = FAULT_WIFI;
    } else if (!mqttUp) {
      newState = NET_OFFLINE;  newFault = FAULT_MQTT;
    } else if (fails >= DEGRADED_FAIL_COUNT) {
      newState = NET_DEGRADED; newFault = FAULT_MQTT;
    } else if (sensorFault) {
      newState = NET_DEGRADED; newFault = FAULT_SENSOR;
    } else {
      newState = NET_ONLINE;
    }

    NetState prevState;
    lockState();
      prevState         = st.net;
      st.net            = newState;
      st.fault          = newFault;
      st.backoffSeconds = backoff;
    unlockState();

    uint32_t nowMs = millis();

    if (newState == NET_OFFLINE) {
      if (nowMs >= nextAttemptMs) {
        Serial.printf("[NET] recovery attempt, backoff=%lus\n", (unsigned long)backoff);

        if (!wifiUp) {
          WiFi.disconnect();
          WiFi.begin(WIFI_SSID, WIFI_PASS);
          for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            wdtFeed();
          }
        } else if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(5000)) == pdTRUE) {
          mqtt.connect();                       // one shot; backoff governs the retry rate
          xSemaphoreGive(mMqtt);
        }

        /* Exponential backoff, capped. */
        backoff = (backoff * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : backoff * 2;
        nextAttemptMs = millis() + backoff * 1000UL;
      }
    } else {
      /* Link is healthy again -- reset backoff and flush anything we buffered. */
      backoff = BACKOFF_MIN_S;
      nextAttemptMs = 0;

      if (prevState == NET_OFFLINE && uxQueueMessagesWaiting(qOffline) > 0) {
        Serial.printf("[NET] restored -- replaying %u buffered samples\n",
                      (unsigned)uxQueueMessagesWaiting(qOffline));
        if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(10000)) == pdTRUE) {
          uint16_t n = drainOfflineBuffer();
          xSemaphoreGive(mMqtt);
          Serial.printf("[NET] resynced %u records\n", (unsigned)n);
        }
      }
    }

    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* =====================================================================================
 *  SECTION 15 -- TASK: MQTT                                [T2][T3][T4][T5][T6] priority 3
 *  Sole owner of the Adafruit_MQTT client (the library is not re-entrant).
 *  Publishes both dashboard groups and consumes inbound slider commands.
 * ===================================================================================== */

static void applyCommand(const Command &c);

static void publishAll(const SystemState &s) {
  bool ok = true;

  /* -------- Medical Staff Dashboard -------- [T2] */
  if (s.vitals.tempValid && !isnan(s.vitals.bodyTempC)) ok &= pMedTemp.publish(s.vitals.bodyTempC);
  if (s.vitals.hrValid)   ok &= pMedHr.publish((int32_t)s.vitals.heartRate);
  if (s.vitals.spo2Valid) ok &= pMedSpo2.publish((int32_t)s.vitals.spo2);
  ok &= pMedStatus.publish(s.patientStatus);

#if ENABLE_EXTENDED_FEEDS
  char bp[16];
  snprintf(bp, sizeof(bp), "%d/%d", s.vitals.bpSystolic, s.vitals.bpDiastolic);
  ok &= pMedBp.publish(bp);
  ok &= pMedEcg.publish(s.vitals.ecgMv);
  if (s.patientRisk != RISK_NORMAL) {
    char alert[80];
    snprintf(alert, sizeof(alert), "[%s] %s", riskName(s.patientRisk), s.patientStatus);
    ok &= pMedAlert.publish(alert);
  }
#endif

  /* -------- Facility Management Dashboard -------- [T2] */
  if (s.env.valid) ok &= pFacTemp.publish(s.env.roomTempC);
  ok &= pFacO2.publish(s.env.oxygenPct);
  ok &= pFacAqi.publish((int32_t)s.env.aqi);

#if ENABLE_EXTENDED_FEEDS
  if (s.env.valid) ok &= pFacHum.publish(s.env.humidity);
  ok &= pFacMotion.publish((int32_t)(s.vitals.motion ? 1 : 0));
  if (s.facilityRisk != RISK_NORMAL) {
    char alert[80];
    snprintf(alert, sizeof(alert), "[%s] %s", riskName(s.facilityRisk), s.facilityStatus);
    ok &= pFacAlert.publish(alert);
  }
#endif

  /* -------- System / actuator readback -------- */
  ok &= pSysState.publish(netName(s.net));
#if ENABLE_EXTENDED_FEEDS
  ok &= pSysDosage.publish(s.dosageCurrent);
  ok &= pSysBed.publish((int32_t)s.bedCurrent);
  ok &= pSysRate.publish((int32_t)s.samplingSeconds);
#endif

  lockState();
    if (ok) st.publishFailures = 0;
    else    st.publishFailures++;
  unlockState();
}

/* [T2] periodic aggregation -- summarised insight rather than raw firehose. */
typedef struct {
  float hrSum, spo2Sum, tempSum, roomSum, o2Sum, aqiSum;
  int   hrMin, hrMax, spo2Min, aqiMax;
  uint16_t n;
} Aggregate;

static void aggReset(Aggregate &a) {
  a.hrSum = a.spo2Sum = a.tempSum = a.roomSum = a.o2Sum = a.aqiSum = 0;
  a.hrMin = 9999; a.hrMax = -9999; a.spo2Min = 9999; a.aqiMax = -9999; a.n = 0;
}

static void aggAdd(Aggregate &a, const SystemState &s) {
  if (!isnan(s.vitals.bodyTempC)) a.tempSum += s.vitals.bodyTempC;
  a.hrSum   += s.vitals.heartRate;
  a.spo2Sum += s.vitals.spo2;
  if (!isnan(s.env.roomTempC)) a.roomSum += s.env.roomTempC;
  a.o2Sum   += s.env.oxygenPct;
  a.aqiSum  += s.env.aqi;
  if (s.vitals.heartRate < a.hrMin && s.vitals.heartRate > 0) a.hrMin = s.vitals.heartRate;
  if (s.vitals.heartRate > a.hrMax) a.hrMax = s.vitals.heartRate;
  if (s.vitals.spo2 < a.spo2Min && s.vitals.spo2 > 0) a.spo2Min = s.vitals.spo2;
  if (s.env.aqi > a.aqiMax) a.aqiMax = s.env.aqi;
  a.n++;
}

static void aggPublish(Aggregate &a) {
  if (a.n == 0) return;

#if !ENABLE_EXTENDED_FEEDS
  /* The free-tier build has no medical.summary / facility.summary feeds. Publishing to
   * a feed that does not exist fails every time, which would drive publishFailures past
   * the DEGRADED threshold and pin the system there forever. Aggregation is therefore
   * compiled out rather than left to fail silently.
   * NOTE: Task 2's aggregation clause needs the extended build (see DASHBOARD_SETUP.md). */
  aggReset(a);
  return;
#else
  char buf[192];

  snprintf(buf, sizeof(buf),
           "{\"n\":%u,\"hr_avg\":%.1f,\"hr_min\":%d,\"hr_max\":%d,"
           "\"spo2_avg\":%.1f,\"spo2_min\":%d,\"temp_avg\":%.2f}",
           (unsigned)a.n, a.hrSum / a.n, a.hrMin, a.hrMax,
           a.spo2Sum / a.n, a.spo2Min, a.tempSum / a.n);
  pMedSum.publish(buf);

  snprintf(buf, sizeof(buf),
           "{\"n\":%u,\"room_avg\":%.2f,\"o2_avg\":%.2f,\"aqi_avg\":%.0f,\"aqi_max\":%d}",
           (unsigned)a.n, a.roomSum / a.n, a.o2Sum / a.n, a.aqiSum / a.n, a.aqiMax);
  pFacSum.publish(buf);

  aggReset(a);
#endif
}

static void mqttTask(void *param) {
  esp_task_wdt_add(NULL);

  Aggregate agg; aggReset(agg);
  uint32_t lastPublishMs = 0, lastAggMs = millis();

  for (;;) {
    NetState nState; int periodS;
    lockState(); nState = st.net; periodS = st.samplingSeconds; unlockState();

    if (nState != NET_OFFLINE && xSemaphoreTake(mMqtt, pdMS_TO_TICKS(3000)) == pdTRUE) {

      /* ---- inbound: dashboard sliders and buttons ---- [T3][T4][T5] */
      Adafruit_MQTT_Subscribe *sub;
      while ((sub = mqtt.readSubscription(20))) {
        Command c;
        float   val = strtof((char *)sub->lastread, NULL);
        bool    valid = true;

        if      (sub == &sDosage)    { c.type = CMD_DOSAGE;         c.value = val; }
        else if (sub == &sDoseOk)    { c.type = CMD_DOSAGE_CONFIRM; c.value = val; }
        else if (sub == &sBedAngle)  { c.type = CMD_BED_ANGLE;      c.value = val; }
        else if (sub == &sBedAuto)   { c.type = CMD_BED_AUTO;       c.value = val; }
        else if (sub == &sRate)      { c.type = CMD_SAMPLING_RATE;  c.value = val; }
        else if (sub == &sRateMode)  { c.type = CMD_SAMPLING_MODE;  c.value = val; }
        else if (sub == &sBedPreset) {
          c.type = CMD_BED_PRESET;
          const char *p = (char *)sBedPreset.lastread;
          if      (strcasecmp(p, "sleep")     == 0) c.value = BED_PRESET_SLEEP;
          else if (strcasecmp(p, "breathing") == 0) c.value = BED_PRESET_BREATH;
          else if (strcasecmp(p, "emergency") == 0) c.value = BED_PRESET_EMERGENCY;
          else c.value = val;
        }
        else valid = false;

        if (valid) xQueueSend(qCommand, &c, 0);
        wdtFeed();
      }

      /* ---- outbound ---- */
      mqtt.ping();

      uint32_t nowMs = millis();
      if (nowMs - lastPublishMs >= (uint32_t)periodS * 1000UL) {
        lastPublishMs = nowMs;
        SystemState s;
        lockState(); s = st; unlockState();
        publishAll(s);
        aggAdd(agg, s);
      }

      if (nowMs - lastAggMs >= AGGREGATION_PERIOD_S * 1000UL) {
        lastAggMs = nowMs;
        aggPublish(agg);                                   // [T2]
      }

      xSemaphoreGive(mMqtt);
    }

    /* ---- apply queued commands (also runs while offline, so local control survives) ---- */
    Command c;
    while (xQueueReceive(qCommand, &c, 0) == pdTRUE) applyCommand(c);

    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/* =====================================================================================
 *  SECTION 16 -- COMMAND APPLICATION                       [T3][T4][T5]
 *  All dashboard-originated changes funnel through here so every safety rule is
 *  enforced in exactly one place.
 * ===================================================================================== */

static void applyCommand(const Command &c) {
  bool rateChanged = false;

  lockState();
  switch (c.type) {

    /* ---------------- [T3] medication dosage ---------------- */
    case CMD_DOSAGE: {
      float req = clampF(c.value, DOSAGE_MIN_MGH, DOSAGE_MAX_MGH);
      st.dosageSetpoint = req;
      if (req > DOSAGE_CRIT_MGH) {
        /* Critical request: hold at the ceiling until a doctor confirms. */
        st.dosageConfirmPending = true;
        st.dosageRequestMs      = millis();
        st.dosageTarget         = DOSAGE_CRIT_MGH;
        Serial.printf("[DOSE] %.1f mg/hr exceeds %.0f -- awaiting confirmation\n",
                      req, DOSAGE_CRIT_MGH);
      } else {
        st.dosageConfirmPending = false;
        st.dosageTarget         = req;
      }
      break;
    }

    case CMD_DOSAGE_CONFIRM:
      if (st.dosageConfirmPending && c.value >= 1.0f) {
        st.dosageConfirmPending = false;
        st.dosageTarget = st.dosageSetpoint;   // ramp still applies -- never a step change
        Serial.printf("[DOSE] confirmed %.1f mg/hr\n", st.dosageTarget);
      }
      break;

    /* ---------------- [T4] bed elevation ---------------- */
    case CMD_BED_ANGLE:
      st.bedAuto   = false;                    // a manual move takes control back
      st.bedTarget = clampI((int)c.value, BED_MIN_DEG, BED_MAX_DEG);
      break;

    case CMD_BED_PRESET:
      st.bedAuto   = false;
      st.bedTarget = clampI((int)c.value, BED_MIN_DEG, BED_MAX_DEG);
      break;

    case CMD_BED_AUTO:
      st.bedAuto = (c.value >= 1.0f);
      break;

    /* ---------------- [T5] sampling rate ---------------- */
    case CMD_SAMPLING_RATE:
      st.samplingSeconds = clampI((int)c.value, SAMPLING_MIN_S, SAMPLING_MAX_S);
      st.samplingAuto    = false;
      rateChanged        = true;
      break;

    case CMD_SAMPLING_MODE:
      st.samplingAuto = (c.value >= 1.0f);
      st.stableCycles = 0;
      if (!st.samplingAuto) st.samplingSeconds = clampI(st.samplingSeconds,
                                                       SAMPLING_MIN_S, SAMPLING_MAX_S);
      rateChanged = true;
      break;
  }
  unlockState();

  if (rateChanged) notifySamplingChanged();
}

/* =====================================================================================
 *  SECTION 17 -- SETUP
 * ===================================================================================== */

static void initState() {
  memset(&st, 0, sizeof(st));
  st.patientRisk      = RISK_NORMAL;
  st.facilityRisk     = RISK_NORMAL;
  strcpy(st.patientStatus,  "Initialising");
  strcpy(st.facilityStatus, "Initialising");
  st.dosageSetpoint   = 0.0f;
  st.dosageTarget     = 0.0f;
  st.dosageCurrent    = 0.0f;
  st.bedTarget        = BED_PRESET_SLEEP;
  st.bedCurrent       = 0;
  st.bedAuto          = true;
  st.samplingSeconds  = SAMPLING_DEFAULT_S;
  st.samplingAuto     = true;
  st.net              = NET_OFFLINE;
  st.fault            = FAULT_WIFI;
  st.backoffSeconds   = BACKOFF_MIN_S;
}

static void initWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  /* Arduino-ESP32 3.x already initialises the TWDT -- just retune it.
   * Field-by-field assignment (not a designated initialiser) so this compiles
   * cleanly under gnu++11/17 as well as C++20. */
  esp_task_wdt_config_t cfg;
  cfg.timeout_ms     = WDT_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic  = true;
  esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Remote Patient Risk Monitoring System ==="));
  Serial.println(F("ElevanceSkills internship build -- 6 integrated tasks"));

  initState();

  /* ---- GPIO ---- */
  pinMode(BUZZER_PIN_1, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);
  pinMode(BUZZER_PIN_3, OUTPUT);
  pinMode(BUZZER_PIN_4, OUTPUT);
  pinMode(LED_DOSAGE,   OUTPUT);
  pinMode(LED_OK,       OUTPUT);
  pinMode(LED_NET,      OUTPUT);

  pinMode(MOTION_DETECTOR, INPUT);
  pinMode(SW_TEMP,   INPUT);
  pinMode(SW_HR,     INPUT);
  pinMode(SW_SPO2,   INPUT);
  pinMode(SW_MOTION, INPUT);

  analogReadResolution(12);

  /* ---- drivers ---- */
  bodySensor.begin();
  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("[OLED] init failed"));
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("RPRMS booting..."));
  display.display();

  ESP32PWM::allocateTimer(0);
  bedServo.setPeriodHertz(50);
  bedServo.attach(SERVO_PIN, 500, 2400);
  bedServo.write(0);

#if USE_SPIFFS
  if (!SPIFFS.begin(true)) Serial.println(F("[SPIFFS] mount failed -- RAM buffer only"));
#endif

  /* ---- RTOS primitives ---- [T1] */
  qVitals       = xQueueCreate(1, sizeof(VitalsPacket));
  qAdvanced     = xQueueCreate(1, sizeof(AdvancedPacket));
  qEnv          = xQueueCreate(1, sizeof(EnvPacket));
  qCommand      = xQueueCreate(12, sizeof(Command));
  qOffline      = xQueueCreate(OFFLINE_BUFFER_DEPTH, sizeof(BufferedRecord));

  qWakeVitals   = xQueueCreate(1, sizeof(uint8_t));
  qWakeAdvanced = xQueueCreate(1, sizeof(uint8_t));
  qWakeEnv      = xQueueCreate(1, sizeof(uint8_t));

  mState   = xSemaphoreCreateMutex();
  mI2C     = xSemaphoreCreateMutex();
  mMqtt    = xSemaphoreCreateMutex();
  semAlert = xSemaphoreCreateBinary();

  if (!qVitals || !qAdvanced || !qEnv || !qCommand || !qOffline ||
      !qWakeVitals || !qWakeAdvanced || !qWakeEnv ||
      !mState || !mI2C || !mMqtt || !semAlert) {
    Serial.println(F("[FATAL] RTOS object allocation failed"));
    while (1) { delay(1000); }
  }

  /* Seed the data queues so peeking tasks never read uninitialised memory. */
  { VitalsPacket   v = {}; xQueueOverwrite(qVitals,   &v); }
  { AdvancedPacket a = {}; xQueueOverwrite(qAdvanced, &a); }
  { EnvPacket      e = {}; xQueueOverwrite(qEnv,      &e); }

  /* ---- network (non-blocking: the connectivity task owns recovery) ---- [T6] */
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  Serial.println(WiFi.status() == WL_CONNECTED
                 ? F("[WIFI] connected") : F("[WIFI] not connected -- starting offline"));

  mqtt.subscribe(&sDosage);
  mqtt.subscribe(&sDoseOk);
  mqtt.subscribe(&sBedAngle);
  mqtt.subscribe(&sBedPreset);
  mqtt.subscribe(&sBedAuto);
  mqtt.subscribe(&sRate);
  mqtt.subscribe(&sRateMode);

  initWatchdog();

  /* ---- tasks ---- [T1] priority-based preemptive scheduling across both cores ----
   *
   *   prio 6  alertTask          core 1   hard real-time alarms
   *   prio 5  processingTask     core 0   risk engine
   *   prio 4  vitalsSensorTask   core 1   core patient sensing (temp / HR / SpO2 / PIR)
   *   prio 4  actuatorTask       core 1   servo / dosage ramp
   *   prio 3  advancedVitalsTask core 1   blood pressure + ECG
   *   prio 3  envSensorTask      core 1   facility sensing
   *   prio 3  mqttTask           core 0   network I/O (blocking -- kept off the sensor core)
   *   prio 3  connectivityTask   core 0   link supervision
   *   prio 2  displayTask        core 1   OLED refresh
   * -------------------------------------------------------------------------------- */
  xTaskCreatePinnedToCore(alertTask,          "Alert",   4096, NULL, 6, NULL, 1);
  xTaskCreatePinnedToCore(processingTask,     "Process", 8192, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(vitalsSensorTask,   "Vitals",  4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(actuatorTask,       "Actuate", 3072, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(advancedVitalsTask, "AdvVit",  3072, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(envSensorTask,      "EnvSens", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(mqttTask,           "Mqtt",    8192, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(connectivityTask,   "NetSup",  4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(displayTask,        "Display", 4096, NULL, 2, NULL, 1);

  Serial.println(F("[BOOT] 9 FreeRTOS tasks started"));
}

void loop() {
  /* All work happens in the eight FreeRTOS tasks above. The Arduino loop task has
   * nothing to do, so unsubscribe it from the watchdog (parking it forever would
   * otherwise look like a hang) and delete it to reclaim its 8 KB stack. */
  esp_task_wdt_delete(NULL);
  vTaskDelete(NULL);
}
