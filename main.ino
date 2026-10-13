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

#define FW_VERSION "2026-10-13 final (10 tasks, fault-injection switches, patient override)"

/* =====================================================================================
 *  SECTION 0 -- CONFIGURATION
 *  Edit ONLY this section before uploading / running in Wokwi.
 * ===================================================================================== */

#define WIFI_SSID   "Wokwi-GUEST"
#define WIFI_PASS   ""

/* ---- MQTT broker selection ----------------------------------------------------------
 * The portal mandates no dashboard platform, so either broker satisfies the brief.
 *
 *   USE_HIVEMQ 1  ->  public HiveMQ broker + the bundled Node-RED dashboard.
 *                     Free, no feed limit. This is the only free route that can
 *                     evidence every clause of Task 2 (see docs/NODE_RED_SETUP.md).
 *
 *   USE_HIVEMQ 0  ->  Adafruit IO, as used by the original training project.
 *                     Free tier caps at 10 feeds; see docs/DASHBOARD_SETUP.md.
 *
 * The system boots and runs fully offline with either one unreachable, which is
 * exactly the behaviour Task 6 demonstrates.
 * ----------------------------------------------------------------------------------- */
#define USE_HIVEMQ 1

#if USE_HIVEMQ
  /* MQTT_PREFIX becomes the root of every topic: <prefix>/feeds/<group>.<feed>
   * broker.hivemq.com is PUBLIC -- anyone may subscribe to your prefix, and anyone
   * publishing to <prefix>/feeds/control.dosage would drive the simulated infusion
   * pump. Nothing here is real patient data, but keep the prefix unique to you.
   * If you change it, change it in dashboards/node-red-flows.json too. */
  #define IO_USERNAME     "rprms-hl-8842"
  #define IO_KEY          ""
  #define AIO_SERVER      "broker.hivemq.com"
  #define AIO_SERVERPORT  1883
#else
  /* Your Adafruit IO username and Active Key (Profile -> My Key). */
  #define IO_USERNAME     "[Username]"
  #define IO_KEY          "[Key]"
  #define AIO_SERVER      "io.adafruit.com"
  #define AIO_SERVERPORT  1883
#endif

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
 *  GPIO 27/36/34/35/13/19/2/18 are new hardware added for T1..T4.
 *  GPIO 23/39 are the two network fault-injection switches added for T6.
 *
 *  The medication LED is on GPIO19, deliberately NOT GPIO15: GPIO15 is a boot-strapping
 *  pin and an output driven on it never reached the LED in the Wokwi ESP32 model (found by
 *  toggling every pin, see docs/BUG_LOG.md B-14). Likewise Wokwi's DevKit-V1 calls GPIO16
 *  and GPIO17 "RX2" and "TX2" -- wires to "D16"/"D17" attach to nothing (B-15).
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
#define LED_DOSAGE       19   // RED   -- medication critical               [T3]
#define LED_OK            2   // GREEN -- system healthy heartbeat
#define LED_NET          18   // AMBER -- degraded / offline                [T6]

/* Network fault-injection switches [T6]. Wokwi has no "pull the network cable" control, so
 * these two slide switches let an operator break the link on demand, mid-run, and restore it
 * again -- the only way to demonstrate the OFFLINE -> buffer -> resync cycle repeatably.
 * They act on the REAL link (Wi-Fi is genuinely disassociated, the MQTT session is genuinely
 * closed), so the detection, buffering and backoff code is exercised exactly as in a real
 * outage. Same wiring convention as the sensor switches: HIGH = fault applied.            */
#define SW_WIFI_CUT      23   // HIGH = Wi-Fi link cut          -> OFFLINE, cause WIFI
#define SW_BROKER_CUT    39   // HIGH = broker unreachable      -> OFFLINE, cause MQTT (VN, input-only)

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
/* Servo pulse range for 0..180 deg, calibrated against Wokwi's SG90 model. With the nominal
 * datasheet range 500..2400 us the simulated horn was MEASURED showing 4/25/40/87 deg for
 * commanded 10/30/45/90 deg (docs/BUG_LOG.md). Wokwi maps 0 deg to ~565 us and 180 deg to
 * ~2396 us, so using that range makes the horn show exactly the commanded angle. Both ends
 * lie inside the valid pulse range of an SG90, so the same values are safe on hardware. */
#define SERVO_MIN_US         565
#define SERVO_MAX_US        2396
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
#define SAMPLING_SLICE_MS    100     // sampling delay is vTaskDelay()'d in slices: a rate change
                                     // posted on the wake queue is acted on within 100 ms

/* --- T6 connectivity / fault tolerance --- */
#define OFFLINE_BUFFER_DEPTH  120    // ~30 min at 15 s sampling
#define BACKOFF_MIN_S           1
#define BACKOFF_MAX_S          60
#define RESYNC_GAP_MS         600    // throttle backlog replay (API rate limits)
#define DEGRADED_FAIL_COUNT     2    // publish failures before declaring DEGRADED
#define WDT_TIMEOUT_S          20
#define PING_MISS_LIMIT         3    // unanswered MQTT keepalives before the broker is declared gone
#define MQTT_PING_IDLE_MS   10000UL  // ping only after this long without sending anything (the
                                     // library's CONNECT keepalive is 300 s, so the session never lapses)
#define MQTT_SUSPECT_PING_MS 1000UL  // after a FAILED publish ping this often until the broker answers
                                     // or PING_MISS_LIMIT misses declare the session dead

#define AGGREGATION_PERIOD_S   60    // [T2] periodic summary publication

/* =====================================================================================
 *  SECTION 3 -- MQTT FEED TOPICS  [T2] role separation by topic prefix (not ACLs)
 *
 *    medical.*   -> Medical Staff Dashboard   (patient vitals, clinical alerts)
 *    facility.*  -> Facility Management Dashboard (environment, air, occupancy)
 *    system.*    -> device telemetry / actuator readback
 *    control.*   -> INBOUND commands from dashboard sliders and buttons
 *
 *  The three read groups use distinct topic prefixes, which is the PRECONDITION for
 *  role-based access -- a broker configured with topic ACLs (Mosquitto ACL file,
 *  HiveMQ Cloud per-credential permissions, AWS IoT Core policies, ...) could grant
 *  clinical staff and facilities staff access to only their own subtree.
 *
 *  That ACL configuration is NOT part of this firmware and is not shipped. As
 *  configured by default (USE_HIVEMQ 1, the public broker.hivemq.com below), there is
 *  no access control at all: any client can subscribe to any prefix, including
 *  medical.*. Do not read this section as "role-based access implemented" -- it is
 *  "role-based access made possible, given broker-side ACLs this project does not
 *  configure." See docs/TASK_MAPPING.md Task 2 for the full caveat.
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

/* Synthetic-patient override. Wokwi has no pulse-oximeter part, so heart rate and SpO2 are
 * generated in software (a mean-reverting random walk with rare episodes). These two feeds
 * let an operator pin them to a chosen value -- the equivalent of holding a real patient at a
 * chosen vital sign -- so the risk engine, the adaptive sampling [T5] and the automatic bed
 * [T4] can be demonstrated ON DEMAND instead of waiting for a random episode.
 * 0 = automatic.                                                                         */
#define C_SIM_HR      IO_USERNAME "/feeds/control.sim-hr"
#define C_SIM_SPO2    IO_USERNAME "/feeds/control.sim-spo2"

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

/* Extended haemodynamic parameters [T1]. Task 1 asks for the additional health parameters to
 * be "each managed by separate FreeRTOS tasks", so blood pressure and ECG are NOT bolted onto
 * the core vitals loop and do NOT share a task either: bloodPressureTask owns BpPacket and
 * ecgTask owns EcgPacket, each publishing to its own queue. */
typedef struct {
  int      systolic;
  int      diastolic;
  bool     valid;               // false when the heart-rate probe is off (BP is HR-derived)
  uint32_t seq;
} BpPacket;

typedef struct {
  float    mv;                  // ECG amplitude 0.00 .. 5.00 mV
  uint32_t seq;
} EcgPacket;

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
  CMD_BED_AUTO, CMD_SAMPLING_RATE, CMD_SAMPLING_MODE,
  CMD_SIM_HR, CMD_SIM_SPO2
} CommandType;

typedef struct { CommandType type; float value; } Command;

/* Rolling 60 s statistics for the periodic dashboard summaries [T2].
 *
 * IMPORTANT: every struct used in a function signature must be declared HERE, above
 * the first function definition. The Arduino .ino preprocessor auto-generates forward
 * prototypes and inserts them near the top of the file -- if the type is defined
 * further down, those prototypes reference an undeclared type and the build fails with
 * "variable or field declared void". Declaring all types up front avoids that. */
typedef struct {
  float hrSum, spo2Sum, tempSum, roomSum, o2Sum, aqiSum;
  int   hrMin, hrMax, spo2Min, aqiMax;
  uint16_t n;
} Aggregate;

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
  bool bedFast;                 // emergency mode: faster (but still ramped) travel to 90 deg

  /* [T5] */
  int  samplingSeconds;
  bool samplingAuto;
  int  stableCycles;

  /* synthetic-patient override, 0 = automatic (see C_SIM_HR) */
  int  simHr;
  int  simSpo2;

  /* [T6] */
  NetState  net;
  FaultKind fault;
  bool      sensorFault;
  uint16_t  bufferedCount;
  uint32_t  publishFailures;
  uint32_t  backoffSeconds;     // current backoff interval (1,2,4,..,60 s)
  uint32_t  retryInSeconds;     // live countdown to the next recovery attempt
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
Adafruit_MQTT_Subscribe sSimHr    (&mqtt, C_SIM_HR);
Adafruit_MQTT_Subscribe sSimSpo2  (&mqtt, C_SIM_SPO2);

OneWire            oneWire(TEMP_SENSOR);
DallasTemperature  bodySensor(&oneWire);
DHT                dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306   display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo              bedServo;

/* --- Queues [T1] --- */
QueueHandle_t qVitals;        // depth 1, overwrite  -- latest core vitals
QueueHandle_t qBp;            // depth 1, overwrite  -- latest blood pressure    [T1]
QueueHandle_t qEcg;           // depth 1, overwrite  -- latest ECG amplitude     [T1]
QueueHandle_t qEnv;           // depth 1, overwrite  -- latest environment
QueueHandle_t qCommand;       // depth 12            -- dashboard commands
QueueHandle_t qOffline;       // depth OFFLINE_BUFFER_DEPTH [T6] -- store-and-forward

/* One wake queue PER sampling-driven task -- a shared one would starve all but the
 * first waiter, because xQueueReceive consumes the token. [T5] */
QueueHandle_t qWakeVitals;
QueueHandle_t qWakeBp;
QueueHandle_t qWakeEcg;
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

/* The sampling delay used by every sampling-driven task. [T1 watchdog] [T5 dynamic rate]
 *
 * Task 5 asks for "non-blocking delays (vTaskDelay) and dynamic updates through queues".
 * This is exactly that: the wait is made of short vTaskDelay() slices -- the task is
 * descheduled for each slice and burns no CPU, so it never busy-waits -- and between slices
 * it polls its OWN wake queue WITHOUT blocking. A token on that queue means the sampling
 * rate changed, so the task stops waiting and resamples immediately. Worst-case reaction
 * time to a rate change is one slice (SAMPLING_SLICE_MS = 100 ms), however long the
 * configured period is, and the watchdog is fed every slice, so even a 60 s period can
 * never starve it.
 *
 * Each sampling task must pass its own queue. A single shared queue would be a bug:
 * xQueueReceive CONSUMES the token, so only the first task to wake would see the rate
 * change and the others would sleep out their full period. */
static void interruptibleDelay(uint32_t ms, QueueHandle_t wakeQueue) {
  uint8_t  dummy;
  uint32_t waited = 0;
  while (waited < ms) {
    vTaskDelay(pdMS_TO_TICKS(SAMPLING_SLICE_MS));       // non-blocking: yields the CPU
    waited += SAMPLING_SLICE_MS;
    wdtFeed();
    if (xQueueReceive(wakeQueue, &dummy, 0) == pdTRUE)  // dynamic update arrived via queue
      return;                                           // rate changed -- resample now
  }
}

/* Wakes every sampling-driven task. One token per queue, so no task is starved. */
static void notifySamplingChanged() {
  uint8_t token = 1;
  xQueueOverwrite(qWakeVitals, &token);
  xQueueOverwrite(qWakeBp,     &token);
  xQueueOverwrite(qWakeEcg,    &token);
  xQueueOverwrite(qWakeEnv,    &token);
}

/* Human-readable fault name for the serial trace. */
static const char *faultName(FaultKind f) {
  switch (f) { case FAULT_WIFI: return "WIFI"; case FAULT_MQTT: return "MQTT";
               case FAULT_SENSOR: return "SENSOR"; default: return "none"; }
}

/* =====================================================================================
 *  SECTION 7 -- TASK: VITALS SENSING                       [T1] priority 4
 *  Body temperature, heart rate, SpO2, motion. (Blood pressure and ECG are NOT here --
 *  each has its own task, see sections 7b and 7c.)
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
  int   prevSimHr = 0, prevSimSpo2 = 0;     // to snap back to a normal baseline on release

  for (;;) {
    uint32_t loopStartMs = millis();
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

    /* Operator override of the synthetic patient (0 = automatic random walk). */
    int simHr, simSpo2;
    lockState(); simHr = st.simHr; simSpo2 = st.simSpo2; unlockState();
    if (simHr   == 0 && prevSimHr   > 0) hrBase   = 78.0f;    // override released -> normal again
    if (simSpo2 == 0 && prevSimSpo2 > 0) spo2Base = 97.0f;
    prevSimHr = simHr; prevSimSpo2 = simSpo2;

    /* ---- heart rate (simulated pulse-oximeter channel) ---- */
    if (v.hrValid) {
      if (simHr > 0) {
        hrBase = clampF((float)simHr, 30.0f, 220.0f);                    // pinned by the operator
      } else {
        /* Mean-reverting walk: heart rate hovers around 78 bpm and decays back after a rare
         * episode. A plain random walk (the first revision) wandered into the 150-175 bpm
         * critical band and stayed there, so a "stable" simulated patient did not stay stable. */
        hrBase += (78.0f - hrBase) * 0.12f + random(-300, 301) / 100.0f;
        if (random(0, 100) < 4) hrBase += random(0, 2) ? 48.0f : -32.0f;   // rare tachy / brady episode
        hrBase = clampF(hrBase, 40.0f, 175.0f);
      }
      v.heartRate = (int)(hrBase + 0.5f);
    } else {
      v.heartRate = 0;
    }

    /* ---- SpO2 ---- */
    if (v.spo2Valid) {
      if (simSpo2 > 0) {
        spo2Base = clampF((float)simSpo2, 50.0f, 100.0f);                // pinned by the operator
      } else {
        spo2Base += (97.0f - spo2Base) * 0.20f + random(-60, 61) / 100.0f;   // hovers around 97 %
        if (v.heartRate > HR_HIGH_WARN) spo2Base -= 0.6f;                // tachycardia desat
        spo2Base = clampF(spo2Base, 78.0f, 100.0f);
      }
      v.spo2 = (int)(spo2Base + 0.5f);
    } else {
      v.spo2 = 0;
    }

    /* BP and ECG are NOT read here -- bloodPressureTask and ecgTask own them. */

    /* ---- motion (PIR) ---- */
    v.motion = v.motionValid && digitalRead(MOTION_DETECTOR);

    v.seq = ++seq;
    xQueueOverwrite(qVitals, &v);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    /* Trace: lets the sampling cadence [T5] be read straight off the serial log in simulated
     * time (millis()), independent of how fast the simulator happens to be running. */
    Serial.printf("[SAMPLE] vitals #%lu at t=%lu ms: hr=%d spo2=%d (period %d s)\n",
                  (unsigned long)v.seq, (unsigned long)millis(), v.heartRate, v.spo2, periodS);

    /* Hold the configured period: the DS18B20 conversion alone takes ~750 ms, which must not
     * be added on top of a 5 s period. */
    uint32_t periodMs = (uint32_t)periodS * 1000UL;
    uint32_t spentMs  = millis() - loopStartMs;
    interruptibleDelay(periodMs > spentMs ? periodMs - spentMs : 0, qWakeVitals);   // [T5]
  }
}

/* =====================================================================================
 *  SECTION 7b -- TASK: BLOOD PRESSURE                      [T1] priority 3
 *
 *  Task 1 asks for the additional health parameters (blood pressure, ECG) to be "each
 *  managed by separate FreeRTOS tasks". Blood pressure therefore gets its OWN task rather
 *  than being appended to the core vitals loop, and ECG gets a different one (7c).
 *
 *  It consumes heart rate from qVitals (BP is derived with physiological correlation)
 *  and produces qBp -- a genuine task-to-task data dependency carried over a queue,
 *  which is exactly the concurrency pattern the task is asking to see.
 * ===================================================================================== */

static void bloodPressureTask(void *param) {
  esp_task_wdt_add(NULL);

  BpPacket     b = {};
  VitalsPacket v = {};
  uint32_t seq = 0;

  for (;;) {
    xQueuePeek(qVitals, &v, 0);            // upstream dependency: needs heart rate

    /* ---- non-invasive blood pressure, correlated with heart rate ---- */
    if (v.hrValid && v.heartRate > 0) {
      b.systolic  = clampI((int)(112 + (v.heartRate - 78) * 0.45f + random(-4, 5)), 70, 210);
      b.diastolic = clampI((int)(72  + (v.heartRate - 78) * 0.22f + random(-3, 4)), 40, 130);
      b.valid     = true;
    } else {
      b.systolic = b.diastolic = 0;
      b.valid    = false;                  // HR probe off -> BP cannot be derived
    }

    b.seq = ++seq;
    xQueueOverwrite(qBp, &b);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    interruptibleDelay((uint32_t)periodS * 1000UL, qWakeBp);             // [T5]
  }
}

/* =====================================================================================
 *  SECTION 7c -- TASK: ECG                                 [T1] priority 3
 *
 *  The second additional parameter, in its own FreeRTOS task: ECG amplitude from the
 *  potentiometer on GPIO36 (ADC1). Independent of every other sensing task -- it neither
 *  reads nor writes any shared state except its own queue, qEcg.
 * ===================================================================================== */

static void ecgTask(void *param) {
  esp_task_wdt_add(NULL);

  EcgPacket e = {};
  uint32_t seq = 0;

  for (;;) {
    e.mv  = (analogRead(PIN_ECG) / 4095.0f) * 5.0f;                      // 0.00 .. 5.00 mV
    e.seq = ++seq;
    xQueueOverwrite(qEcg, &e);

    int periodS;
    lockState(); periodS = st.samplingSeconds; unlockState();
    interruptibleDelay((uint32_t)periodS * 1000UL, qWakeEcg);            // [T5]
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
  BpPacket       bp = {};
  EcgPacket      ecg = {};
  EnvPacket      e = {};
  uint32_t lastBufferMs = 0;         // [T6] paces offline buffering to the sampling rate
  uint32_t lastSeenSeq  = 0;         // [T5] last vitals sample already counted toward hysteresis
  uint8_t  swPrev = 0xFF;            // [T1][T6] last seen state of the four probe switches

  for (;;) {
    /* ---- probe watcher [T1][T6] ----
     * The vitals task reads its four probe-disconnect switches once per SAMPLING period -- 30 s
     * when the patient is stable, far too slow to notice a lead coming off ("continuously
     * monitors ... system health"). Watch them here at 1 Hz and, on any change, wake the
     * patient-sensing tasks so the fault is reported within a second or two, not a period
     * later. HIGH = probe disconnected. */
    uint8_t swNow = (digitalRead(SW_TEMP) ? 1 : 0) | (digitalRead(SW_HR)    ? 2 : 0) |
                    (digitalRead(SW_SPO2) ? 4 : 0) | (digitalRead(SW_MOTION) ? 8 : 0);
    if (swNow != swPrev) {
      if (swPrev != 0xFF) {
        Serial.printf("[SENSOR] probe change -> temp:%s hr:%s spo2:%s pir:%s -- resampling now\n",
                      (swNow & 1) ? "OFF" : "ok", (swNow & 2) ? "OFF" : "ok",
                      (swNow & 4) ? "OFF" : "ok", (swNow & 8) ? "OFF" : "ok");
        uint8_t token = 1;
        xQueueOverwrite(qWakeVitals, &token);
        xQueueOverwrite(qWakeBp,     &token);
      }
      swPrev = swNow;
    }

    xQueuePeek(qVitals, &v,   0);
    xQueuePeek(qBp,     &bp,  0);
    xQueuePeek(qEcg,    &ecg, 0);
    xQueuePeek(qEnv,    &e,   0);

    /* The queues are seeded with zeros in setup(). Until the vitals and environment tasks have
     * each produced a first real sample (seq > 0) there is nothing to judge -- scoring the
     * zeros would raise a phantom "oxygen level unsafe" alarm, flip the sampling rate and buffer
     * a record of zeros. Hold off for a second instead. */
    if (v.seq == 0 || e.seq == 0) {
      wdtFeed();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    /* This loop runs at 1 Hz but the vitals queue only changes once per SAMPLING period.
     * Hysteresis must count genuinely new samples, not loop iterations, otherwise a single
     * normal reading would satisfy "3 stable cycles" within three seconds. [T5] */
    bool freshSample = (v.seq != lastSeenSeq);
    lastSeenSeq = v.seq;

    /* Fold the blood-pressure and ECG tasks' output into the vitals record the rest of the
     * system consumes, so display / MQTT / buffering all see one coherent patient snapshot. */
    v.bpSystolic  = bp.systolic;
    v.bpDiastolic = bp.diastolic;
    v.ecgMv       = ecg.mv;

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
    } else if (bp.valid && (bp.systolic > BP_SYS_HIGH_WARN || bp.systolic < BP_SYS_LOW_WARN)) {
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
        } else if (freshSample && ++st.stableCycles >= SAMPLING_STABLE_CYCLES) {
          st.samplingSeconds = SAMPLING_STABLE_S;        // 3 stable SAMPLES -> conserve battery
        }
      }
      int newRate = st.samplingSeconds;

      /* -------- [T4] automatic bed elevation -------- */
      if (st.bedAuto) {
        int  want = st.bedTarget;
        bool fast = st.bedFast;
        if (pRisk == RISK_CRITICAL && v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_CRIT) {
          want = BED_PRESET_EMERGENCY; fast = true;      // severe desat -> full upright, quickly
        } else if (v.spo2Valid && v.spo2 > 0 && v.spo2 < SPO2_WARN) {
          want = BED_PRESET_BREATH;    fast = false;     // breathing support
        } else if (pRisk == RISK_NORMAL) {
          want = BED_PRESET_SLEEP;     fast = false;     // resting position
        }
        st.bedTarget = clampI(want, BED_MIN_DEG, BED_MAX_DEG);
        st.bedFast   = fast;
      }

      netNow = st.net;
    unlockState();

    if (newRate != prevRate) {
      Serial.printf("[RATE] auto-adapt: %d s -> %d s\n", prevRate, newRate);
      notifySamplingChanged();
    }

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

  uint32_t  lastNetBeepMs = 0;
  FaultKind lastBeepFault = FAULT_NONE;  // edge detector for the [ALERT] trace line
  bool      okLed = false;
  bool     doseAlarmWasOn = false;       // edge detector for the [ALARM] trace line

  for (;;) {
    /* Wait for the risk engine, but never block forever -- alarms must keep sounding. */
    xSemaphoreTake(semAlert, pdMS_TO_TICKS(500));

    VitalsPacket v; RiskLevel pRisk; NetState nState; FaultKind fault;
    float dose; RiskLevel fRisk; bool dosePending;

    lockState();
      v           = st.vitals;
      pRisk       = st.patientRisk;
      fRisk       = st.facilityRisk;
      nState      = st.net;
      fault       = st.fault;
      dose        = st.dosageCurrent;
      dosePending = st.dosageConfirmPending;
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
     * Above the critical threshold the red LED blinks rapidly and the buzzer sounds.
     * The delivered dose is pinned AT the ceiling while a >80 request awaits confirmation,
     * so it never exceeds it on its own -- the pending request itself must raise the alarm,
     * otherwise staff get no audible warning that a critical dose is waiting on them. */
    bool doseCritical = (dose > DOSAGE_CRIT_MGH || dosePending);
    if (doseCritical && !doseAlarmWasOn)
      Serial.printf("[ALARM] critical dosage -- delivered %.1f mg/hr, confirmation %s\n",
                    dose, dosePending ? "PENDING" : "n/a");
    doseAlarmWasOn = doseCritical;
    if (doseCritical) {
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
    if (nState == NET_ONLINE) lastBeepFault = FAULT_NONE;          // re-arm the trace for the next fault
    if (nState != NET_ONLINE && nowMs - lastNetBeepMs > 5000UL) {
      lastNetBeepMs = nowMs;
      if (fault != lastBeepFault) {          // trace once per fault type, not every 5 s
        Serial.printf("[ALERT] buzzer 4, %s fault: %s\n", faultName(fault),
                      fault == FAULT_WIFI ? "3 short beeps" :
                      fault == FAULT_MQTT ? "2 long beeps" : "1 chirp");
        lastBeepFault = fault;
      }
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
 *
 *  Every screen -- every page AND the offline banner -- ends with a persistent footer
 *  showing the medication dosage and its safety level, and the whole screen is redrawn once
 *  per second, so a dashboard slider move is visible on the OLED within about a second no
 *  matter which page is showing ("displayed in real-time on the OLED", Task 3). The page
 *  itself still rotates every 3 s.
 * ===================================================================================== */

/* Five content rows, 10 px pitch (8 px glyph + 2 px gap); y = 52 is the footer rule. */
static const uint8_t OLED_ROW[5] = { 0, 11, 21, 31, 41 };

/* Persistent dosage read-out [T3]. 21 characters = the full 128 px line at text size 1. */
static void oledFooter(const SystemState &s) {
  const char *level = s.dosageConfirmPending             ? "CONF"      // >80 request awaiting OK
                    : s.dosageCurrent >  DOSAGE_CRIT_MGH ? "CRIT"
                    : s.dosageCurrent >= DOSAGE_WARN_MGH ? "WARN" : "NORM";
  display.drawFastHLine(0, 52, SCREEN_WIDTH, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.printf("DOSE %5.1f mg/hr %s", s.dosageCurrent, level);
}

static void displayTask(void *param) {
  esp_task_wdt_add(NULL);

  uint8_t page = 0;
  uint8_t tick = 0;                  // redraw every second, rotate the page every 3rd redraw

  for (;;) {
    SystemState s;
    lockState(); s = st; unlockState();

    xSemaphoreTake(mI2C, portMAX_DELAY);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (s.net == NET_OFFLINE) {
      /* ---- [T6] mandated offline banner ---- */
      display.setCursor(0, OLED_ROW[0]); display.print(F("*** LINK FAILURE ***"));
      display.fillRect(0, OLED_ROW[1] - 1, SCREEN_WIDTH, 10, SSD1306_WHITE);   // inverted bar
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(2, OLED_ROW[1]); display.print(F("LOGGING OFFLINE"));
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, OLED_ROW[2]);
      display.print(F("Cause: "));
      display.print(s.fault == FAULT_WIFI ? F("WIFI")
                   : s.fault == FAULT_MQTT ? F("MQTT") : F("SENSOR"));
      display.setCursor(0, OLED_ROW[3]);
      display.print(F("Buffered: ")); display.print(s.bufferedCount);
      display.setCursor(0, OLED_ROW[4]);
      display.print(F("Retry in ")); display.print(s.retryInSeconds); display.print(F("s"));
    } else {
      switch (page) {
        case 0:   /* patient vitals */
          display.setCursor(0, OLED_ROW[0]); display.print(F("PATIENT VITALS"));
          display.setCursor(0, OLED_ROW[1]); display.print(F("Temp: "));
          if (s.vitals.tempValid && !isnan(s.vitals.bodyTempC)) {
            display.print(s.vitals.bodyTempC, 1); display.print(F(" C"));
          } else display.print(F("--"));
          display.setCursor(0, OLED_ROW[2]); display.print(F("HR  : "));
          if (s.vitals.hrValid) display.print(s.vitals.heartRate); else display.print(F("--"));
          display.print(F(" bpm"));
          display.setCursor(0, OLED_ROW[3]); display.print(F("SpO2: "));
          if (s.vitals.spo2Valid) display.print(s.vitals.spo2); else display.print(F("--"));
          display.print(F(" %"));
          display.setCursor(0, OLED_ROW[4]); display.print(s.patientStatus);
          break;

        case 1:   /* extended vitals [T1] */
          display.setCursor(0, OLED_ROW[0]); display.print(F("EXTENDED VITALS"));
          display.setCursor(0, OLED_ROW[1]); display.print(F("BP  : "));
          display.print(s.vitals.bpSystolic); display.print(F("/"));
          display.print(s.vitals.bpDiastolic); display.print(F(" mmHg"));
          display.setCursor(0, OLED_ROW[2]); display.print(F("ECG : "));
          display.print(s.vitals.ecgMv, 2); display.print(F(" mV"));
          display.setCursor(0, OLED_ROW[3]); display.print(F("Move: "));
          display.print(s.vitals.motion ? F("DETECTED") : F("none"));
          display.setCursor(0, OLED_ROW[4]); display.print(F("Risk: "));
          display.print(riskName(s.patientRisk));
          break;

        case 2:   /* facility [T2] */
          display.setCursor(0, OLED_ROW[0]); display.print(F("FACILITY / ROOM"));
          display.setCursor(0, OLED_ROW[1]); display.print(F("Room: "));
          if (s.env.valid) { display.print(s.env.roomTempC, 1); display.print(F(" C")); }
          else display.print(F("--"));
          display.setCursor(0, OLED_ROW[2]); display.print(F("O2  : "));
          display.print(s.env.oxygenPct, 1); display.print(F(" %"));
          display.setCursor(0, OLED_ROW[3]); display.print(F("AQI : "));
          display.print(s.env.aqi);
          display.setCursor(0, OLED_ROW[4]); display.print(s.facilityStatus);
          break;

        case 3:   /* actuators [T3][T4] */
          display.setCursor(0, OLED_ROW[0]); display.print(F("THERAPY CONTROL"));
          display.setCursor(0, OLED_ROW[1]); display.print(F("Dose: "));
          display.print(s.dosageCurrent, 1); display.print(F(" mg/hr"));
          display.setCursor(0, OLED_ROW[2]);
          if (s.dosageConfirmPending) {
            display.print(F(">CONFIRM "));
            display.print(s.dosageSetpoint, 0); display.print(F(" mg/hr"));
          } else {
            display.print(F("Level: "));
            display.print(s.dosageCurrent > DOSAGE_CRIT_MGH ? F("CRITICAL")
                        : s.dosageCurrent >= DOSAGE_WARN_MGH ? F("WARNING") : F("NORMAL"));
          }
          display.setCursor(0, OLED_ROW[3]); display.print(F("Bed : "));
          display.print(s.bedCurrent); display.print(F(" deg -> "));
          display.print(s.bedTarget);
          display.setCursor(0, OLED_ROW[4]); display.print(F("Auto bed: "));
          display.print(s.bedAuto ? F("ON") : F("OFF"));
          break;

        default:  /* system [T5][T6] */
          display.setCursor(0, OLED_ROW[0]); display.print(F("SYSTEM STATUS"));
          display.setCursor(0, OLED_ROW[1]); display.print(F("Link: "));
          display.print(netName(s.net));
          display.setCursor(0, OLED_ROW[2]); display.print(F("Rate: "));
          display.print(s.samplingSeconds); display.print(F("s "));
          display.print(s.samplingAuto ? F("(auto)") : F("(man)"));
          display.setCursor(0, OLED_ROW[3]); display.print(F("Buf : "));
          display.print(s.bufferedCount);
          display.print(F("  Sync:")); display.print(s.totalResynced);
          display.setCursor(0, OLED_ROW[4]); display.print(F("Heap: "));
          display.print(ESP.getFreeHeap() / 1024); display.print(F(" KB"));
          break;
      }
    }

    oledFooter(s);                   // [T3] dosage visible on every screen
    display.display();
    xSemaphoreGive(mI2C);

    /* Redraw at 1 Hz; advance the page every third redraw (= 3 s per page). */
    if (s.net != NET_OFFLINE && ++tick >= 3) { tick = 0; page = (page + 1) % 5; }

    wdtFeed();
    vTaskDelay(pdMS_TO_TICKS(1000));
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

  /* Verification trace: lets the ramp timing be measured from the serial log (millis() is
   * simulated time) instead of judged by eye. Output only -- no effect on control. */
  uint32_t lastDoseLogMs = 0, lastBedLogMs = 0;
  bool     doseWasRamping = false, bedWasMoving = false;

  for (;;) {
    uint32_t nowMs  = millis();
    float    dtSec  = (nowMs - lastMs) / 1000.0f;
    lastMs = nowMs;

    float doseNow, doseTgt; int bedTgt;
    bool     gateExpired = false;        // trace only: printed after the lock is released
    uint32_t gateAgeMs   = 0;

    lockState();
      /* ---------------- [T3] dosage confirmation gate ---------------- */
      if (st.dosageConfirmPending &&
          (nowMs - st.dosageRequestMs) > DOSAGE_CONFIRM_MS) {
        /* Doctor never confirmed -- fall back to the safe ceiling. */
        st.dosageConfirmPending = false;
        st.dosageTarget = DOSAGE_CRIT_MGH;
        gateExpired = true;
        gateAgeMs   = nowMs - st.dosageRequestMs;
      }

      /* ---------------- [T3] dosage rate limiting ---------------- */
      float maxStep = DOSAGE_RAMP_MGH_S * dtSec;
      float delta   = st.dosageTarget - st.dosageCurrent;
      if (fabsf(delta) <= maxStep) st.dosageCurrent = st.dosageTarget;
      else                         st.dosageCurrent += (delta > 0 ? maxStep : -maxStep);
      st.dosageCurrent = clampF(st.dosageCurrent, DOSAGE_MIN_MGH, DOSAGE_MAX_MGH);

      /* ---------------- [T4] smooth servo transition ---------------- */
      /* The faster ramp belongs to the EMERGENCY MODE, not to the number 90: a slider dragged to
       * 90 deg moves at the normal 1 deg / 25 ms like any other angle. */
      int step = st.bedFast ? BED_EMERGENCY_STEP : BED_STEP_DEG;
      if (st.bedCurrent < st.bedTarget)      st.bedCurrent = min(st.bedTarget, st.bedCurrent + step);
      else if (st.bedCurrent > st.bedTarget) st.bedCurrent = max(st.bedTarget, st.bedCurrent - step);
      int bedNow = clampI(st.bedCurrent, BED_MIN_DEG, BED_MAX_DEG);
      doseNow = st.dosageCurrent;  doseTgt = st.dosageTarget;  bedTgt = st.bedTarget;
    unlockState();

    if (gateExpired)
      Serial.printf("[DOSE] t=%lu ms confirmation expired after %lu ms -- holding at %.0f mg/hr\n",
                    (unsigned long)nowMs, (unsigned long)gateAgeMs, DOSAGE_CRIT_MGH);

    bedServo.write(bedNow);

    bool doseRamping = (doseNow != doseTgt);
    if ((doseRamping && nowMs - lastDoseLogMs >= 1000UL) || (doseWasRamping && !doseRamping)) {
      lastDoseLogMs = nowMs;
      Serial.printf("[DOSE] t=%lu ms delivered %.1f -> target %.1f mg/hr\n",
                    (unsigned long)nowMs, doseNow, doseTgt);
    }
    doseWasRamping = doseRamping;

    bool bedMoving = (bedNow != bedTgt);
    if ((bedMoving && nowMs - lastBedLogMs >= 200UL) || (bedWasMoving && !bedMoving)) {
      lastBedLogMs = nowMs;
      Serial.printf("[BED] t=%lu ms %d -> %d deg\n", (unsigned long)nowMs, bedNow, bedTgt);
    }
    bedWasMoving = bedMoving;

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
 *  Reconnection uses exponential backoff 1,2,4,8,16,32,60 s (capped). The OLED shows the
 *  live countdown to the next attempt, and every state change is written to the serial log.
 *
 *  Fault injection: SW_WIFI_CUT / SW_BROKER_CUT (section 1) let an operator break and
 *  restore the link mid-run. They work on the real link -- Wi-Fi is genuinely disassociated
 *  and the MQTT session genuinely closed -- and the recovery attempt that would have
 *  re-opened it is reported as failed for as long as the switch is held. Releasing a switch
 *  is treated like a link-up event: one immediate retry, then the normal schedule resumes.
 * ===================================================================================== */

static void connectivityTask(void *param) {
  esp_task_wdt_add(NULL);

  uint32_t backoff = BACKOFF_MIN_S;
  uint32_t nextAttemptMs = 0;
  uint32_t drainBackoff = BACKOFF_MIN_S;   // retry schedule for a stalled backlog replay
  uint32_t nextDrainMs  = 0;

  bool      wifiCutPrev = false, brokerCutPrev = false;       // switch edge detectors
  bool      lastMqttUp  = false;
  NetState  loggedState;                                      // last state written to the log,
  FaultKind loggedFault;                                      // seeded from the state set in setup()
  lockState(); loggedState = st.net; loggedFault = st.fault; unlockState();

  for (;;) {
    /* ---- fault-injection switches: HIGH = fault applied ---- */
    bool wifiCut   = digitalRead(SW_WIFI_CUT);
    bool brokerCut = digitalRead(SW_BROKER_CUT);

    if (wifiCut != wifiCutPrev) {
      Serial.printf("[FAULT] SW_WIFI_CUT %s -- Wi-Fi link %s\n",
                    wifiCut ? "ON" : "OFF", wifiCut ? "CUT" : "restored");
      wifiCutPrev = wifiCut;
      if (!wifiCut) nextAttemptMs = 0;       // link-up event: retry at once
    }
    if (brokerCut != brokerCutPrev) {
      Serial.printf("[FAULT] SW_BROKER_CUT %s -- broker %s\n",
                    brokerCut ? "ON" : "OFF", brokerCut ? "UNREACHABLE" : "reachable again");
      brokerCutPrev = brokerCut;
      if (!brokerCut) nextAttemptMs = 0;
    }

    if (wifiCut && WiFi.status() == WL_CONNECTED) WiFi.disconnect();     // really drop Wi-Fi

    bool wifiUp = (WiFi.status() == WL_CONNECTED);

    /* Without Wi-Fi any MQTT session is dead whether or not the socket has noticed yet.
     * Close it explicitly so it is re-opened cleanly when the link returns, rather than
     * publishing into a stale socket. The broker fault switch does the same to the session. */
    bool mqttUp = lastMqttUp;            // client busy (lock not free)? keep the last known value
    if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(2000)) == pdTRUE) {
      if ((!wifiUp || brokerCut) && mqtt.connected()) mqtt.disconnect();
      mqttUp = mqtt.connected();
      lastMqttUp = mqttUp;
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
      /* DEGRADED means "link up but publishes failing". While the link is down publishing is
       * suspended, so failures are not counted -- otherwise the first seconds after a restore
       * would be misreported as DEGRADED because of attempts made during the outage. */
      if (newState == NET_OFFLINE) st.publishFailures = 0;
    unlockState();

    if (newState != loggedState) {
      Serial.printf("[NET] state %s -> %s (fault: %s)\n",
                    netName(loggedState), netName(newState), faultName(newFault));
    } else if (newFault != loggedFault) {
      Serial.printf("[NET] still %s, fault changed %s -> %s\n",
                    netName(newState), faultName(loggedFault), faultName(newFault));
    }
    loggedState = newState;
    loggedFault = newFault;

    uint32_t nowMs = millis();

    if (newState == NET_OFFLINE) {
      if (nowMs >= nextAttemptMs) {
        Serial.printf("[NET] recovery attempt, backoff=%lus\n", (unsigned long)backoff);
        bool stageRecovered = false;       // this attempt brought a layer back up

        if (!wifiUp) {
          if (wifiCut) {
            Serial.println(F("[NET]   attempt failed: Wi-Fi link is down (SW_WIFI_CUT held)"));
          } else {
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
              vTaskDelay(pdMS_TO_TICKS(500));
              wdtFeed();
            }
            if (WiFi.status() == WL_CONNECTED) {
              Serial.println(F("[NET]   Wi-Fi re-associated -- reconnecting to the broker next"));
              stageRecovered = true;
            }
          }
        } else if (brokerCut) {
          Serial.println(F("[NET]   attempt failed: broker unreachable (SW_BROKER_CUT held)"));
        } else if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(5000)) == pdTRUE) {
          /* mqtt.connect() blocks on DNS + TCP. Against an unreachable broker the
           * socket timeout can exceed WDT_TIMEOUT_S, which would panic-reset the
           * device for what is really just a slow network. Step out of the watchdog
           * for the duration of the blocking call and re-enter straight after --
           * the retry rate stays bounded by the exponential backoff either way. */
          esp_task_wdt_delete(NULL);
          int8_t rc = mqtt.connect();           // one shot; backoff governs the retry rate
          esp_task_wdt_add(NULL);
          xSemaphoreGive(mMqtt);
          if (rc == 0) stageRecovered = true;
          else Serial.printf("[NET]   broker connect failed: %s\n",
                             (const char *)mqtt.connectErrorString(rc));   // flash string = plain char* on ESP32
        }

        if (stageRecovered) {
          /* Progress was made, so the failure streak is over: the next layer is attempted on the
           * next tick instead of waiting out a backoff earned by the previous layer's failures. */
          backoff = BACKOFF_MIN_S;
          nextAttemptMs = 0;
        } else {
          /* Exponential backoff, capped. */
          backoff = (backoff * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : backoff * 2;
          nextAttemptMs = millis() + backoff * 1000UL;
        }
      }
    } else {
      /* Link is healthy again -- reset backoff and flush anything we buffered. */
      backoff = BACKOFF_MIN_S;
      nextAttemptMs = 0;

      /* Replay the backlog immediately on OFFLINE -> up. If that replay stalls (publish
       * failures), the records are retained -- and retried here on an exponential schedule
       * for as long as the link itself is healthy, instead of waiting for the next outage.
       * A DEGRADED link caused by broker publish failures is skipped: a replay would only
       * fail again. [T6] */
      bool backlog     = uxQueueMessagesWaiting(qOffline) > 0;
      bool justRestore = (prevState == NET_OFFLINE);
      bool linkHealthy = (newFault != FAULT_MQTT);

      if (backlog && (justRestore || (linkHealthy && nowMs >= nextDrainMs))) {
        Serial.printf(justRestore ? "[NET] restored -- replaying %u buffered samples\n"
                                  : "[NET] retrying backlog replay -- %u buffered samples\n",
                      (unsigned)uxQueueMessagesWaiting(qOffline));
        if (xSemaphoreTake(mMqtt, pdMS_TO_TICKS(10000)) == pdTRUE) {
          uint16_t n = drainOfflineBuffer();
          xSemaphoreGive(mMqtt);
          Serial.printf("[NET] resynced %u records\n", (unsigned)n);
        }
        if (uxQueueMessagesWaiting(qOffline) > 0) {
          drainBackoff = (drainBackoff * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : drainBackoff * 2;
          nextDrainMs  = millis() + drainBackoff * 1000UL;
        } else {
          drainBackoff = BACKOFF_MIN_S;
          nextDrainMs  = 0;
        }
      }
    }

    /* Live countdown to the next recovery attempt, shown on the OLED banner. */
    uint32_t tNow = millis();
    lockState();
      st.backoffSeconds = backoff;
      st.retryInSeconds = (st.net == NET_OFFLINE && nextAttemptMs > tNow)
                        ? (uint32_t)((nextAttemptMs - tNow + 999UL) / 1000UL) : 0;
    unlockState();

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

/* Publish one value unless an earlier publish in the same round has already failed.
 *  - When the TCP link stalls (the broker, or Wokwi's IoT gateway, stops reading) one
 *    WiFiClient::write() can block for ~10 s. A round is ~15 writes and the watchdog used to be fed
 *    only once per pass of mqttTask, so a stall tripped the 20 s task watchdog and rebooted the chip --
 *    wiping the RAM buffer that Task 6 exists to protect (BUG_LOG B-38).
 *  - A failed write means the session is suspect; sending the rest of the round only adds stalls.
 * The watchdog is fed before every write, so a single stalled write can never use up the whole
 * timeout on its own, and the round ends at the first failure (the caller then probes the session). */
#define MQTT_PUBLISH(okVar, call)  do { if (okVar) { wdtFeed(); okVar = (call); } } while (0)

/* Returns true when every publish of the round succeeded (false = a write failed; the rest were skipped). */
static bool publishAll(const SystemState &s) {
  bool ok = true;

  /* -------- Medical Staff Dashboard -------- [T2] */
  if (s.vitals.tempValid && !isnan(s.vitals.bodyTempC)) MQTT_PUBLISH(ok, pMedTemp.publish(s.vitals.bodyTempC));
  if (s.vitals.hrValid)   MQTT_PUBLISH(ok, pMedHr.publish((int32_t)s.vitals.heartRate));
  if (s.vitals.spo2Valid) MQTT_PUBLISH(ok, pMedSpo2.publish((int32_t)s.vitals.spo2));
  MQTT_PUBLISH(ok, pMedStatus.publish(s.patientStatus));

#if ENABLE_EXTENDED_FEEDS
  char bp[16];
  snprintf(bp, sizeof(bp), "%d/%d", s.vitals.bpSystolic, s.vitals.bpDiastolic);
  MQTT_PUBLISH(ok, pMedBp.publish(bp));
  MQTT_PUBLISH(ok, pMedEcg.publish(s.vitals.ecgMv));
  if (s.patientRisk != RISK_NORMAL) {
    char alert[80];
    snprintf(alert, sizeof(alert), "[%s] %s", riskName(s.patientRisk), s.patientStatus);
    MQTT_PUBLISH(ok, pMedAlert.publish(alert));
  }
#endif

  /* -------- Facility Management Dashboard -------- [T2] */
  if (s.env.valid) MQTT_PUBLISH(ok, pFacTemp.publish(s.env.roomTempC));
  MQTT_PUBLISH(ok, pFacO2.publish(s.env.oxygenPct));
  MQTT_PUBLISH(ok, pFacAqi.publish((int32_t)s.env.aqi));

#if ENABLE_EXTENDED_FEEDS
  if (s.env.valid) MQTT_PUBLISH(ok, pFacHum.publish(s.env.humidity));
  MQTT_PUBLISH(ok, pFacMotion.publish((int32_t)(s.vitals.motion ? 1 : 0)));
  if (s.facilityRisk != RISK_NORMAL) {
    char alert[80];
    snprintf(alert, sizeof(alert), "[%s] %s", riskName(s.facilityRisk), s.facilityStatus);
    MQTT_PUBLISH(ok, pFacAlert.publish(alert));
  }
#endif

  /* -------- System / actuator readback -------- */
  MQTT_PUBLISH(ok, pSysState.publish(netName(s.net)));
#if ENABLE_EXTENDED_FEEDS
  MQTT_PUBLISH(ok, pSysDosage.publish(s.dosageCurrent));
  MQTT_PUBLISH(ok, pSysBed.publish((int32_t)s.bedCurrent));
  MQTT_PUBLISH(ok, pSysRate.publish((int32_t)s.samplingSeconds));
#endif

  lockState();
    if (ok) st.publishFailures = 0;
    else    st.publishFailures++;
  unlockState();
  return ok;
}

/* [T2] periodic aggregation -- summarised insight rather than raw firehose.
 * (The Aggregate type itself lives in Section 4 -- see the note there on why.) */

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

/* Returns false when a summary write failed (the caller treats the session as suspect). */
static bool aggPublish(Aggregate &a) {
  if (a.n == 0) return true;

#if !ENABLE_EXTENDED_FEEDS
  /* The free-tier build has no medical.summary / facility.summary feeds. Publishing to
   * a feed that does not exist fails every time, which would drive publishFailures past
   * the DEGRADED threshold and pin the system there forever. Aggregation is therefore
   * compiled out rather than left to fail silently.
   * NOTE: Task 2's aggregation clause needs the extended build (see DASHBOARD_SETUP.md). */
  aggReset(a);
  return true;
#else
  char buf[192];
  bool ok = true;

  snprintf(buf, sizeof(buf),
           "{\"n\":%u,\"hr_avg\":%.1f,\"hr_min\":%d,\"hr_max\":%d,"
           "\"spo2_avg\":%.1f,\"spo2_min\":%d,\"temp_avg\":%.2f}",
           (unsigned)a.n, a.hrSum / a.n, a.hrMin, a.hrMax,
           a.spo2Sum / a.n, a.spo2Min, a.tempSum / a.n);
  MQTT_PUBLISH(ok, pMedSum.publish(buf));

  snprintf(buf, sizeof(buf),
           "{\"n\":%u,\"room_avg\":%.2f,\"o2_avg\":%.2f,\"aqi_avg\":%.0f,\"aqi_max\":%d}",
           (unsigned)a.n, a.roomSum / a.n, a.o2Sum / a.n, a.aqiSum / a.n, a.aqiMax);
  MQTT_PUBLISH(ok, pFacSum.publish(buf));

  aggReset(a);
  return ok;
#endif
}

static void mqttTask(void *param) {
  esp_task_wdt_add(NULL);

  Aggregate agg; aggReset(agg);
  uint32_t lastAggMs = millis();
  uint32_t lastTrafficMs = millis();     // last time a packet was sent (publish or ping)
  uint32_t lastPubSeq = 0;               // vitals sample number last published
  uint8_t  pingMisses = 0;
  bool     suspect    = false;           // a publish failed: probe the session every MQTT_SUSPECT_PING_MS

  for (;;) {
    NetState nState;
    lockState(); nState = st.net; unlockState();
    if (nState == NET_OFFLINE) lastPubSeq = 0;      // on restore, publish the current sample at once

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
        else if (sub == &sSimHr)     { c.type = CMD_SIM_HR;         c.value = val; }
        else if (sub == &sSimSpo2)   { c.type = CMD_SIM_SPO2;       c.value = val; }
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
      /* Application-level keepalive. mqtt.connected() only reflects the LOCAL socket, which
       * stays "open" for minutes after a silent network failure (cable pulled, upstream lost).
       * PING_MISS_LIMIT consecutive unanswered PINGREQs => treat the broker as gone and drop
       * the socket, so the supervisor sees OFFLINE (MQTT) and starts the exponential-backoff
       * recovery instead of publishing into the void. [T6]
       *
       * The ping is sent only when the link has been idle for MQTT_PING_IDLE_MS. Pinging on
       * every pass of this 200 ms loop (as an earlier revision did) sent ~5 PINGREQ/s and held
       * the client lock for up to PING_TIMEOUT_MS (500 ms) each time; together with a separate
       * publish timer the measured publish cadence was 2-3x the configured sampling period.
       * Published samples count as traffic, so at the 5-15 s sampling periods a ping is rarely
       * needed at all (the library's CONNECT keepalive is 300 s, so the session never lapses). */
      bool brokerAlive = true;
      uint32_t nowMs = millis();
      /* After a FAILED publish the session is suspect, so it is probed every second instead of
       * every 10 s: a dead session is then dropped within PING_MISS_LIMIT probes and the
       * supervisor starts buffering, instead of the next rounds stalling on it again (B-38). */
      if (nowMs - lastTrafficMs >= (suspect ? MQTT_SUSPECT_PING_MS : MQTT_PING_IDLE_MS)) {
        lastTrafficMs = nowMs;
        wdtFeed();
        if (mqtt.ping()) {
          pingMisses = 0;
          suspect    = false;
        } else if (++pingMisses >= PING_MISS_LIMIT) {
          Serial.println(F("[NET] broker not answering keepalive -- dropping connection"));
          mqtt.disconnect();
          pingMisses  = 0;
          suspect     = false;
          brokerAlive = false;    // skip this publish round; the supervisor takes over next tick
        }
      }

      /* Publish when a NEW vitals sample exists -- not on a second, independent timer. A free-
       * running publish timer is never in phase with the sampling task: it re-sends the same
       * sample, skips one, or drifts whenever the rate changes. Publishing per sample makes
       * the dashboard cadence exactly the sampling rate [T5], and makes the 60 s aggregate's
       * "n" the true number of samples in the window [T2]. */
      SystemState s;
      lockState(); s = st; unlockState();
      if (brokerAlive && s.vitals.seq != 0 && s.vitals.seq != lastPubSeq) {
        lastPubSeq = s.vitals.seq;
        bool pubOk = publishAll(s);
        aggAdd(agg, s);
        lastTrafficMs = millis();
        if (pubOk) {
          suspect = false;
          Serial.printf("[MQTT] published sample #%lu at t=%lu ms (period %d s)\n",
                        (unsigned long)s.vitals.seq, (unsigned long)lastTrafficMs, s.samplingSeconds);
        } else {
          suspect = true;
          Serial.printf("[MQTT] publish of sample #%lu FAILED at t=%lu ms -- probing the session\n",
                        (unsigned long)s.vitals.seq, (unsigned long)lastTrafficMs);
        }
      }
      nowMs = millis();

      if (brokerAlive && nowMs - lastAggMs >= AGGREGATION_PERIOD_S * 1000UL) {
        lastAggMs = nowMs;
        if (!aggPublish(agg)) suspect = true;              // [T2]
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
  bool patientChanged = false;       // synthetic-patient override moved -> resample at once

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
      st.bedFast   = false;                    // slider moves always use the normal ramp
      st.bedTarget = clampI((int)c.value, BED_MIN_DEG, BED_MAX_DEG);
      break;

    case CMD_BED_PRESET:
      st.bedAuto   = false;
      st.bedFast   = (c.value >= BED_PRESET_EMERGENCY);   // only the emergency preset is "fast"
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
      Serial.printf("[RATE] manual: %d s (auto off)\n", st.samplingSeconds);
      break;

    case CMD_SAMPLING_MODE:
      st.samplingAuto = (c.value >= 1.0f);
      st.stableCycles = 0;
      if (!st.samplingAuto) st.samplingSeconds = clampI(st.samplingSeconds,
                                                       SAMPLING_MIN_S, SAMPLING_MAX_S);
      rateChanged = true;
      Serial.printf("[RATE] mode: %s\n", st.samplingAuto ? "auto" : "manual");
      break;

    /* ---------------- synthetic-patient override (simulation aid) ---------------- */
    case CMD_SIM_HR:
      st.simHr = (c.value >= 1.0f) ? clampI((int)c.value, 30, 220) : 0;
      patientChanged = true;
      if (st.simHr) Serial.printf("[SIM] heart rate pinned at %d bpm\n", st.simHr);
      else          Serial.println(F("[SIM] heart rate back to automatic"));
      break;

    case CMD_SIM_SPO2:
      st.simSpo2 = (c.value >= 1.0f) ? clampI((int)c.value, 50, 100) : 0;
      patientChanged = true;
      if (st.simSpo2) Serial.printf("[SIM] SpO2 pinned at %d %%\n", st.simSpo2);
      else            Serial.println(F("[SIM] SpO2 back to automatic"));
      break;
  }
  unlockState();

  if (rateChanged) notifySamplingChanged();
  if (patientChanged) {              // wake only the patient-sensing tasks (BP is derived from HR)
    uint8_t token = 1;
    xQueueOverwrite(qWakeVitals, &token);
    xQueueOverwrite(qWakeBp,     &token);
  }
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
  Serial.println(F("[BOOT] firmware " FW_VERSION));

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
  pinMode(SW_WIFI_CUT,   INPUT);          // [T6] fault injection (SPDT switch drives both levels)
  pinMode(SW_BROKER_CUT, INPUT);          // [T6] GPIO39 is input-only, which is all it needs here

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
  bedServo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  bedServo.write(0);

#if USE_SPIFFS
  if (!SPIFFS.begin(true)) Serial.println(F("[SPIFFS] mount failed -- RAM buffer only"));
#endif

  /* ---- RTOS primitives ---- [T1] */
  qVitals       = xQueueCreate(1, sizeof(VitalsPacket));
  qBp           = xQueueCreate(1, sizeof(BpPacket));
  qEcg          = xQueueCreate(1, sizeof(EcgPacket));
  qEnv          = xQueueCreate(1, sizeof(EnvPacket));
  qCommand      = xQueueCreate(12, sizeof(Command));
  qOffline      = xQueueCreate(OFFLINE_BUFFER_DEPTH, sizeof(BufferedRecord));

  qWakeVitals   = xQueueCreate(1, sizeof(uint8_t));
  qWakeBp       = xQueueCreate(1, sizeof(uint8_t));
  qWakeEcg      = xQueueCreate(1, sizeof(uint8_t));
  qWakeEnv      = xQueueCreate(1, sizeof(uint8_t));

  mState   = xSemaphoreCreateMutex();
  mI2C     = xSemaphoreCreateMutex();
  mMqtt    = xSemaphoreCreateMutex();
  semAlert = xSemaphoreCreateBinary();

  if (!qVitals || !qBp || !qEcg || !qEnv || !qCommand || !qOffline ||
      !qWakeVitals || !qWakeBp || !qWakeEcg || !qWakeEnv ||
      !mState || !mI2C || !mMqtt || !semAlert) {
    Serial.println(F("[FATAL] RTOS object allocation failed"));
    while (1) { delay(1000); }
  }

  /* Seed the data queues so peeking tasks never read uninitialised memory. */
  { VitalsPacket v = {}; xQueueOverwrite(qVitals, &v); }
  { BpPacket     b = {}; xQueueOverwrite(qBp,     &b); }
  { EcgPacket    g = {}; xQueueOverwrite(qEcg,    &g); }
  { EnvPacket    e = {}; xQueueOverwrite(qEnv,    &e); }

  /* ---- network (non-blocking: the connectivity task owns recovery) ---- [T6] */
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);       // connectivityTask is the single owner of recovery, so
                                      // reconnects follow the exponential-backoff schedule
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  Serial.println(WiFi.status() == WL_CONNECTED
                 ? F("[WIFI] connected") : F("[WIFI] not connected -- starting offline"));

  /* The system starts OFFLINE (the broker is not connected yet). Record the REAL first cause --
   * Wi-Fi if it did not associate, otherwise the broker -- so the alarm and the log do not
   * report a Wi-Fi fault that does not exist. */
  lockState();
    st.fault = (WiFi.status() == WL_CONNECTED) ? FAULT_MQTT : FAULT_WIFI;
  unlockState();

  mqtt.subscribe(&sDosage);
  mqtt.subscribe(&sDoseOk);
  mqtt.subscribe(&sBedAngle);
  mqtt.subscribe(&sBedPreset);
  mqtt.subscribe(&sBedAuto);
  mqtt.subscribe(&sRate);
  mqtt.subscribe(&sRateMode);
  mqtt.subscribe(&sSimHr);
  mqtt.subscribe(&sSimSpo2);

  initWatchdog();
  Serial.printf("[BOOT] task watchdog armed: %d s timeout, panic reset\n", WDT_TIMEOUT_S);

  /* ---- tasks ---- [T1] priority-based preemptive scheduling across both cores ----
   *
   *   prio 6  alertTask          core 1   hard real-time alarms
   *   prio 5  processingTask     core 0   risk engine
   *   prio 4  vitalsSensorTask   core 1   core patient sensing (temp / HR / SpO2 / PIR)
   *   prio 4  actuatorTask       core 1   servo / dosage ramp
   *   prio 3  bloodPressureTask  core 1   blood pressure            (its own task)
   *   prio 3  ecgTask            core 1   ECG amplitude             (its own task)
   *   prio 3  envSensorTask      core 1   facility sensing
   *   prio 3  mqttTask           core 0   network I/O (blocking -- kept off the sensor core)
   *   prio 3  connectivityTask   core 0   link supervision
   *   prio 2  displayTask        core 1   OLED refresh
   * -------------------------------------------------------------------------------- */
  xTaskCreatePinnedToCore(alertTask,          "Alert",   4096, NULL, 6, NULL, 1);
  xTaskCreatePinnedToCore(processingTask,     "Process", 8192, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(vitalsSensorTask,   "Vitals",  4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(actuatorTask,       "Actuate", 3072, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(bloodPressureTask,  "BpTask",  3072, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(ecgTask,            "EcgTask", 3072, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(envSensorTask,      "EnvSens", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(mqttTask,           "Mqtt",    8192, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(connectivityTask,   "NetSup",  4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(displayTask,        "Display", 4096, NULL, 2, NULL, 1);

  Serial.println(F("[BOOT] 10 FreeRTOS tasks started"));
}

void loop() {
  /* All work happens in the ten FreeRTOS tasks above. The Arduino loop task has
   * nothing to do, so unsubscribe it from the watchdog (parking it forever would
   * otherwise look like a hang) and delete it to reclaim its 8 KB stack.
   *
   * Only unsubscribe if it was actually subscribed -- whether the Arduino core adds
   * loopTask to the TWDT depends on CONFIG_ARDUINO_LOOP_TASK_WDT. Calling delete
   * unconditionally logs "task_wdt: delete_entry(): task not found" on builds where
   * it wasn't, which looks like a fault in the serial log during a demo. */
  if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_delete(NULL);
  vTaskDelete(NULL);
}
