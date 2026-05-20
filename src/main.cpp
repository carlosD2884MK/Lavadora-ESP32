/*
 * Lavadora ESP32 - Fase 5 completa
 * Incluye:
 * - Fase 2: maquina de estados + botones + serial
 * - Fase 3: persistencia de configuracion con Preferences
 * - Fase 4: WiFi AP + portal cautivo + API web
 * - Fase 5: OLED opcional (activar con -D USE_OLED)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include "pinout.h"
#include "config.h"
#include "machine.h"

#ifdef USE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

// -----------------------------------------------------------------------------
// Estado principal
WashingMachine washer;

static CycleConfig selectedCycle = CycleConfig::FULL;
static WashMode selectedMode = WashMode::NORMAL;
static WaterFillMode selectedWater = WaterFillMode::BOTH;

// -----------------------------------------------------------------------------
// Pulsadores con debounce
static const uint32_t DEBOUNCE_MS = 50;
static const uint32_t LONG_PRESS_MS = 2000;

struct Button {
    int pin;
    bool currentState;
    bool lastRaw;
    uint32_t lastChange;
    uint32_t pressStart;
    bool longHandled;
};

static Button btnCiclo = {Pinout::BTN_CICLO, false, false, 0, 0, false};
static Button btnModo = {Pinout::BTN_MODO, false, false, 0, 0, false};
static Button btnInicio = {Pinout::BTN_INICIO, false, false, 0, 0, false};

static uint32_t lastStatusPrint = 0;
static const uint32_t MODE_PREVIEW_MS = 3000;
static uint32_t modePreviewUntil = 0;
static bool showModeView = false;
static bool waterComboLatched = false;
static bool waterComboActive = false;

struct BuzzerStep {
    uint16_t freq;
    uint16_t durationMs;
};

static const uint8_t BUZZER_CHANNEL = 0;
static const uint32_t FINISH_ON_MS = 3000;
static const uint32_t FINISH_OFF_MS = 2000;
static const uint8_t FINISH_REPEAT = 10;
static const BuzzerStep BUZZER_START[]  = {{2200, 70}, {0, 30}};
static const BuzzerStep BUZZER_PAUSE[]  = {{1800, 60}, {0, 40}, {1800, 60}};
static const BuzzerStep BUZZER_STOP[]   = {{1200, 160}};
static const BuzzerStep BUZZER_FINISH[] = {
    {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}, {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS},
    {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}, {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS},
    {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}, {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS},
    {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}, {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS},
    {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}, {2200, FINISH_ON_MS}, {0, FINISH_OFF_MS}
};
static const BuzzerStep BUZZER_ERROR[]   = {{900, 140}, {0, 50}, {700, 140}, {0, 50}, {900, 140}};

static const BuzzerStep* buzzerPattern = nullptr;
static size_t buzzerPatternLen = 0;
static size_t buzzerStepIndex = 0;
static uint32_t buzzerStepUntil = 0;
static bool buzzerRunning = false;
static bool stopBeepRequested = false;
static CyclePhase lastPhase = CyclePhase::IDLE;
static uint32_t finishAlertStart = 0;
static uint32_t finishAlertUntil = 0;

// -----------------------------------------------------------------------------
// Persistencia (Fase 3)
static Preferences prefs;
static const char* PREF_NS = "lavadora";
static const char* PREF_KEY = "cfg";
static const uint32_t CONFIG_MAGIC = 0x4C564A53;  // LVJS
static const uint16_t CONFIG_VERSION = 2;

struct PersistedConfig {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t cycle;
    uint8_t water;
    WashParams params[static_cast<uint8_t>(WashMode::MODE_COUNT)];
};

// -----------------------------------------------------------------------------
// WiFi + portal cautivo + web (Fase 4)
static const char* AP_SSID = WIFI_AP_SSID;
static const char* AP_PASSWORD = WIFI_AP_PASSWORD;
static const IPAddress AP_IP(4, 3, 2, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static DNSServer dnsServer;
static AsyncWebServer server(80);

// -----------------------------------------------------------------------------
// OLED opcional (Fase 5)
#ifdef USE_OLED
static Adafruit_SSD1306 display(128, 64, &Wire, -1);
static bool oledReady = false;
static uint32_t lastOledUpdate = 0;
#endif

// -----------------------------------------------------------------------------
// Prototipos
void setupPins();
void setupBuzzer();
void updateBuzzer();
void playBuzzerPattern(const BuzzerStep* pattern, size_t len);
void beepStart();
void beepPause();
void beepStop();
void beepFinish();
void beepError();
void checkButton(Button& btn, void (*onShortPress)(), void (*onLongPress)() = nullptr);
void handleWaterComboButtons();
void updateLeds();
void updateWaterLeds();
void handleSerial();
void printStatus();
void printMenu();
void printModeTimes();
void onCicloPress();
void onModoPress();
void onInicioPress();
void onInicioLong();
bool finishAlertActive();
bool finishAlertOn();
void stopFinishAlert();

void loadPersistentConfig();
void savePersistentConfig();
void applyDefaultConfig();

void setupNetworking();
void setupWebServer();
String htmlPage();
String jsonStatus();
String jsonConfig();

#ifdef USE_OLED
void setupOled();
void renderOled();
#endif

static uint32_t parseUInt(const String& value, uint32_t fallback) {
    if (value.length() == 0) return fallback;
    long v = value.toInt();
    return (v < 0) ? fallback : static_cast<uint32_t>(v);
}

static bool parseMode(const String& value, WashMode& out) {
    int v = value.toInt();
    if (v < 0 || v >= static_cast<int>(WashMode::MODE_COUNT)) return false;
    out = static_cast<WashMode>(v);
    return true;
}

static bool parseCycle(const String& value, CycleConfig& out) {
    int v = value.toInt();
    if (v < 0 || v >= static_cast<int>(CycleConfig::CYCLE_COUNT)) return false;
    out = static_cast<CycleConfig>(v);
    return true;
}

static bool parseWater(const String& value, WaterFillMode& out) {
    int v = value.toInt();
    if (v < 0 || v >= static_cast<int>(WaterFillMode::WATER_COUNT)) return false;
    out = static_cast<WaterFillMode>(v);
    return true;
}

// -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    setupPins();
    setupBuzzer();
    washer.begin();

    applyDefaultConfig();
    loadPersistentConfig();

    setupNetworking();
    setupWebServer();

#ifdef USE_OLED
    setupOled();
#endif

    Serial.printf("\n=== %s - Fase 5 ===\n", DEVICE_DISPLAY_NAME);
    washer.setWaterMode(selectedWater);
    Serial.printf("Ciclo: %s | Modo: %s | Agua: %s\n",
                  cycleLabel(selectedCycle), modeLabel(selectedMode), waterFillLabel(selectedWater));
    Serial.printf("AP: %s | PASS: %s | IP: %s\n", AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
    printMenu();
}

void loop() {
    washer.update();

    CyclePhase currentPhase = washer.phase();
    if (currentPhase == CyclePhase::ERROR && lastPhase != CyclePhase::ERROR) {
        beepError();
    } else if (currentPhase == CyclePhase::IDLE && lastPhase != CyclePhase::IDLE) {
        if (stopBeepRequested) {
            stopBeepRequested = false;
            beepStop();
        } else if (lastPhase != CyclePhase::PAUSED && lastPhase != CyclePhase::ERROR) {
            beepFinish();
        }
    }
    lastPhase = currentPhase;

    updateBuzzer();

    handleWaterComboButtons();
    checkButton(btnCiclo, onCicloPress);
    checkButton(btnModo, onModoPress);
    checkButton(btnInicio, onInicioPress, onInicioLong);

    updateLeds();
    dnsServer.processNextRequest();

#ifdef USE_OLED
    renderOled();
#endif

    if (washer.isRunning() && millis() - lastStatusPrint >= 5000) {
        printStatus();
        lastStatusPrint = millis();
    }

    if (Serial.available()) handleSerial();
}

// -----------------------------------------------------------------------------
void setupPins() {
    const int relays[] = {
        Pinout::RELAY_MOTOR_ON, Pinout::RELAY_HORARIO,
        Pinout::RELAY_ANTIHORARIO, Pinout::RELAY_VALVULA_FRIA,
        Pinout::RELAY_VALVULA_CALIENTE, Pinout::RELAY_DRAIN,
        Pinout::RELAY_7, Pinout::RELAY_8
    };
    for (int pin : relays) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, RELAY_OFF);
    }

    const int leds[] = {
        Pinout::LED_LAVADO, Pinout::LED_ENJUAGUE,
        Pinout::LED_CENTRIFUGADO, Pinout::LED_ERROR,
        Pinout::LED_AGUA_FRIA, Pinout::LED_AGUA_CALIENTE
    };
    for (int pin : leds) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    pinMode(Pinout::BTN_CICLO, INPUT_PULLUP);
    pinMode(Pinout::BTN_MODO, INPUT_PULLUP);
    pinMode(Pinout::BTN_INICIO, INPUT_PULLUP);
    pinMode(Pinout::SENSOR_NIVEL, INPUT_PULLUP);
}

void setupBuzzer() {
    pinMode(Pinout::BUZZER_PIN, OUTPUT);
    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(Pinout::BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void playBuzzerPattern(const BuzzerStep* pattern, size_t len) {
    if (!pattern || len == 0) return;
    buzzerPattern = pattern;
    buzzerPatternLen = len;
    buzzerStepIndex = 0;
    buzzerStepUntil = 0;
    buzzerRunning = true;
}

bool finishAlertActive() {
    return finishAlertUntil != 0 && millis() < finishAlertUntil;
}

bool finishAlertOn() {
    if (!finishAlertActive() || finishAlertStart == 0) return false;
    uint32_t elapsed = millis() - finishAlertStart;
    uint32_t cycleMs = FINISH_ON_MS + FINISH_OFF_MS;
    uint32_t slot = elapsed / cycleMs;
    if (slot >= FINISH_REPEAT) return false;
    return (elapsed % cycleMs) < FINISH_ON_MS;
}

void stopFinishAlert() {
    finishAlertStart = 0;
    finishAlertUntil = 0;

    if (buzzerPattern == BUZZER_FINISH) {
        ledcWriteTone(BUZZER_CHANNEL, 0);
        buzzerRunning = false;
        buzzerPattern = nullptr;
        buzzerPatternLen = 0;
        buzzerStepIndex = 0;
        buzzerStepUntil = 0;
    }
}

void updateBuzzer() {
    if (!buzzerRunning || !buzzerPattern) return;

    uint32_t now = millis();
    if (buzzerStepUntil != 0 && now < buzzerStepUntil) return;

    if (buzzerStepIndex >= buzzerPatternLen) {
        ledcWriteTone(BUZZER_CHANNEL, 0);
        buzzerRunning = false;
        buzzerPattern = nullptr;
        buzzerPatternLen = 0;
        buzzerStepIndex = 0;
        buzzerStepUntil = 0;
        return;
    }

    const BuzzerStep& step = buzzerPattern[buzzerStepIndex++];
    ledcWriteTone(BUZZER_CHANNEL, step.freq);
    buzzerStepUntil = now + step.durationMs;
}

void beepStart()  { playBuzzerPattern(BUZZER_START,  sizeof(BUZZER_START)  / sizeof(BUZZER_START[0])); }
void beepPause()  { playBuzzerPattern(BUZZER_PAUSE,  sizeof(BUZZER_PAUSE)  / sizeof(BUZZER_PAUSE[0])); }
void beepStop()   { playBuzzerPattern(BUZZER_STOP,   sizeof(BUZZER_STOP)   / sizeof(BUZZER_STOP[0])); }
void beepFinish() {
    playBuzzerPattern(BUZZER_FINISH, sizeof(BUZZER_FINISH) / sizeof(BUZZER_FINISH[0]));
    finishAlertStart = millis();
    finishAlertUntil = finishAlertStart + (FINISH_REPEAT * (FINISH_ON_MS + FINISH_OFF_MS));
}
void beepError()  { playBuzzerPattern(BUZZER_ERROR,  sizeof(BUZZER_ERROR)  / sizeof(BUZZER_ERROR[0])); }

void handleWaterComboButtons() {
    bool cicloPressed = (digitalRead(Pinout::BTN_CICLO) == LOW);
    bool modoPressed = (digitalRead(Pinout::BTN_MODO) == LOW);
    bool comboPressed = cicloPressed && modoPressed;

    if (comboPressed) {
        stopFinishAlert();
    }

    if (comboPressed) {
        waterComboActive = true;

        if (!waterComboLatched) {
            waterComboLatched = true;

            if (!washer.isRunning()) {
                selectedWater = static_cast<WaterFillMode>(
                    (static_cast<uint8_t>(selectedWater) + 1) % static_cast<uint8_t>(WaterFillMode::WATER_COUNT));
                washer.setWaterMode(selectedWater);

                // Vuelve a la vista de ciclo al cambiar el tipo de agua.
                showModeView = false;
                modePreviewUntil = 0;

                Serial.printf("Agua: %s\n", waterFillLabel(selectedWater));
            }
        }
    } else {
        waterComboActive = false;
        waterComboLatched = false;
    }
}

void checkButton(Button& btn, void (*onShortPress)(), void (*onLongPress)()) {
    // --- BLOQUEO DE ACCIONES DURANTE ALERTA DE FINALIZACIÓN ---
    if (finishAlertActive()) {
        bool raw = (digitalRead(btn.pin) == LOW);
        uint32_t now = millis();

        if (raw != btn.lastRaw) {
            btn.lastRaw = raw;
            btn.lastChange = now;
        }
        if (now - btn.lastChange < DEBOUNCE_MS) return;

        if (raw != btn.currentState) {
            btn.currentState = raw;
            if (raw) {
                stopFinishAlert();
                btn.pressStart = now;
                btn.longHandled = false;
            }
        }
        // No ejecuta ninguna acción más
        return;
    }

    if ((btn.pin == Pinout::BTN_CICLO || btn.pin == Pinout::BTN_MODO) && waterComboActive) {
        bool rawCombo = (digitalRead(btn.pin) == LOW);
        uint32_t now = millis();

        if (rawCombo != btn.lastRaw) {
            btn.lastRaw = rawCombo;
            btn.lastChange = now;
        }
        if (now - btn.lastChange < DEBOUNCE_MS) return;

        btn.currentState = rawCombo;
        if (rawCombo) {
            stopFinishAlert();
            btn.pressStart = now;
        }
        btn.longHandled = true;
        return;
    }

    bool raw = (digitalRead(btn.pin) == LOW);
    uint32_t now = millis();

    if (raw != btn.lastRaw) {
        btn.lastRaw = raw;
        btn.lastChange = now;
    }
    if (now - btn.lastChange < DEBOUNCE_MS) return;

    if (raw != btn.currentState) {
        btn.currentState = raw;
        if (raw) {
            stopFinishAlert();
            btn.pressStart = now;
            btn.longHandled = false;
        } else {
            if (!btn.longHandled) onShortPress();
        }
    }

    if (raw && !btn.longHandled && onLongPress) {
        if (now - btn.pressStart >= LONG_PRESS_MS) {
            btn.longHandled = true;
            onLongPress();
        }
    }
}

// -----------------------------------------------------------------------------
void onCicloPress() {
    if (washer.isRunning()) return;
    if (waterComboActive) return;
    modePreviewUntil = 0;
    showModeView = false;
    selectedCycle = static_cast<CycleConfig>(
        (static_cast<uint8_t>(selectedCycle) + 1) % static_cast<uint8_t>(CycleConfig::CYCLE_COUNT));
    Serial.printf("Ciclo: %s\n", cycleLabel(selectedCycle));
}

void onModoPress() {
    if (washer.isRunning()) return;
    if (waterComboActive) return;
    selectedMode = static_cast<WashMode>(
        (static_cast<uint8_t>(selectedMode) + 1) % static_cast<uint8_t>(WashMode::MODE_COUNT));
    showModeView = true;
    modePreviewUntil = millis() + MODE_PREVIEW_MS;
    Serial.printf("Modo: %s\n", modeLabel(selectedMode));
}

void onInicioPress() {
    stopFinishAlert();
    switch (washer.phase()) {
        case CyclePhase::IDLE:
            washer.setWaterMode(selectedWater);
            washer.start(selectedCycle, selectedMode);
            beepStart();
            Serial.printf("INICIANDO - %s / %s / Agua: %s\n",
                          cycleLabel(selectedCycle), modeLabel(selectedMode), waterFillLabel(selectedWater));
            lastStatusPrint = 0;
            break;
        case CyclePhase::PAUSED:
            washer.resume();
            beepStart();
            Serial.println("REANUDANDO");
            break;
        case CyclePhase::ERROR:
            washer.cancel();
            Serial.printf("Error cancelado (%s). Listo.\n",
                          washer.error() == ErrorCode::FILL_TIMEOUT ? "Timeout de llenado" : "desconocido");
            break;
        default:
            if (washer.isRunning()) {
                washer.pause();
                beepPause();
                Serial.println("PAUSADO (mantener 2 s para cancelar)");
            }
            break;
    }
}

void onInicioLong() {
    stopFinishAlert();
    washer.cancel();
    stopBeepRequested = true;
    Serial.println("CANCELADO");
}

// -----------------------------------------------------------------------------
void updateLeds() {
    bool blink = ((millis() / 400) % 2);
    auto led = [](int pin, bool state) { digitalWrite(pin, state ? LOW : HIGH); };
    auto applyCycleProgress = [&](bool lavado, bool enjuague, bool centrifugado) {
        led(Pinout::LED_LAVADO, lavado);
        led(Pinout::LED_ENJUAGUE, enjuague);
        led(Pinout::LED_CENTRIFUGADO, centrifugado);
        led(Pinout::LED_ERROR, false);
    };

    if (finishAlertActive()) {
        bool on = finishAlertOn();
        led(Pinout::LED_LAVADO, on);
        led(Pinout::LED_ENJUAGUE, on);
        led(Pinout::LED_CENTRIFUGADO, on);
        led(Pinout::LED_ERROR, on);
        digitalWrite(Pinout::LED_AGUA_FRIA, on ? LOW : HIGH);
        digitalWrite(Pinout::LED_AGUA_CALIENTE, on ? LOW : HIGH);
        return;
    }

    switch (washer.phase()) {
        case CyclePhase::IDLE:
            if (showModeView && millis() >= modePreviewUntil) {
                showModeView = false;
            }

            if (showModeView) {
                bool l = false;
                bool e = false;
                bool c = false;

                switch (selectedMode) {
                    case WashMode::SOFT:
                        c = true;
                        break;
                    case WashMode::NORMAL:
                        e = true;
                        break;
                    case WashMode::STRONG:
                        e = true;
                        c = true;
                        break;
                    case WashMode::XSTRONG:
                        l = true;
                        e = true;
                        c = true;
                        break;
                    default:
                        break;
                }

                led(Pinout::LED_LAVADO, l);
                led(Pinout::LED_ENJUAGUE, e);
                led(Pinout::LED_CENTRIFUGADO, c);
                led(Pinout::LED_ERROR, blink);
                break;
            }

            led(Pinout::LED_LAVADO,
                selectedCycle == CycleConfig::FULL || selectedCycle == CycleConfig::WASH_ONLY);
            led(Pinout::LED_ENJUAGUE,
                selectedCycle == CycleConfig::FULL || selectedCycle == CycleConfig::RINSE_SPIN ||
                selectedCycle == CycleConfig::RINSE_ONLY);
            led(Pinout::LED_CENTRIFUGADO,
                selectedCycle == CycleConfig::FULL || selectedCycle == CycleConfig::RINSE_SPIN ||
                selectedCycle == CycleConfig::SPIN_ONLY);
            led(Pinout::LED_ERROR, true);
            break;

        case CyclePhase::FILLING_WASH:
        case CyclePhase::WASHING:
        case CyclePhase::DRAINING_WASH:
            applyCycleProgress(
                blink,
                washer.cycle() == CycleConfig::FULL,
                washer.cycle() == CycleConfig::FULL
            );
            break;

        case CyclePhase::FILLING_RINSE:
        case CyclePhase::RINSING:
        case CyclePhase::DRAINING_RINSE:
            applyCycleProgress(
                false,
                blink,
                washer.cycle() == CycleConfig::FULL || washer.cycle() == CycleConfig::RINSE_SPIN
            );
            break;

        case CyclePhase::SPINNING:
            applyCycleProgress(false, false, blink);
            break;

        case CyclePhase::PAUSED:
            led(Pinout::LED_LAVADO, blink);
            led(Pinout::LED_ENJUAGUE, blink);
            led(Pinout::LED_CENTRIFUGADO, blink);
            led(Pinout::LED_ERROR, blink);
            break;

        case CyclePhase::ERROR:
            led(Pinout::LED_LAVADO, false);
            led(Pinout::LED_ENJUAGUE, false);
            led(Pinout::LED_CENTRIFUGADO, false);
            led(Pinout::LED_ERROR, blink);
            break;
    }

    updateWaterLeds();
}

void updateWaterLeds() {
    if (washer.phase() == CyclePhase::ERROR && washer.error() == ErrorCode::FILL_TIMEOUT) {
        bool blink = ((millis() / 300) % 2);
        // Error de llenado: destellar ambos LEDs de agua
        digitalWrite(Pinout::LED_AGUA_FRIA, blink ? LOW : HIGH);
        digitalWrite(Pinout::LED_AGUA_CALIENTE, blink ? LOW : HIGH);
        return;
    }

    if (washer.phase() == CyclePhase::FILLING_WASH || washer.phase() == CyclePhase::FILLING_RINSE) {
        bool blink = ((millis() / 300) % 2);
        // Durante llenado: destellar solo los LEDs de agua seleccionados
        bool aguaFriaOn = (selectedWater == WaterFillMode::COLD || selectedWater == WaterFillMode::BOTH);
        bool aguaCalienteOn = (selectedWater == WaterFillMode::HOT || selectedWater == WaterFillMode::BOTH);
        digitalWrite(Pinout::LED_AGUA_FRIA, aguaFriaOn ? (blink ? LOW : HIGH) : HIGH);
        digitalWrite(Pinout::LED_AGUA_CALIENTE, aguaCalienteOn ? (blink ? LOW : HIGH) : HIGH);
        return;
    }

    // Estado normal: encender según selección
    bool aguaFriaOn = (selectedWater == WaterFillMode::COLD || selectedWater == WaterFillMode::BOTH);
    bool aguaCalienteOn = (selectedWater == WaterFillMode::HOT || selectedWater == WaterFillMode::BOTH);
    digitalWrite(Pinout::LED_AGUA_FRIA, aguaFriaOn ? LOW : HIGH);
    digitalWrite(Pinout::LED_AGUA_CALIENTE, aguaCalienteOn ? LOW : HIGH);
}

// -----------------------------------------------------------------------------
void printMenu() {
    Serial.println("\n--- COMANDOS ---");
    Serial.println(" G  Iniciar            +  Sig. ciclo (reposo)");
    Serial.println(" P  Pausa / Reanudar   *  Sig. modo  (reposo)");
    Serial.println(" C+M juntos: Agua (Fria/Caliente/Ambas)");
    Serial.println(" S  Cancelar           D  Estado actual");
    Serial.println(" T  Tiempos por modo");
    Serial.println(" ?  Este menu");
    Serial.println("----------------");
}

void printModeTimes() {
    Serial.println("\n--- TIEMPOS POR MODO ---");
    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        const WashParams& p = washer.params[i];
        Serial.printf("%s | total %lu s, Lavado: giro %lu ms, pausa %lu ms,  | Enjuague: total %lu s, giro %lu ms | Spin: %lu s | Drenaje: %lu s | Llenado TO: %lu s\n",
                      modeLabel(static_cast<WashMode>(i)),
                      (unsigned long)(p.washTotal_ms / 1000),
                      (unsigned long)p.agitTime_ms,
                      (unsigned long)p.agitPause_ms,
                      (unsigned long)(p.rinseTotal_ms / 1000),
                      (unsigned long)p.rinseAgitTime_ms,
                      (unsigned long)(p.spinTime_ms / 1000),
                      (unsigned long)(p.drainTime_ms / 1000),
                      (unsigned long)(p.fillTimeout_ms / 1000));
    }
    Serial.println("------------------------");
}

void printStatus() {
    uint32_t remS = washer.remaining() / 1000;
    Serial.printf("[%s] %s | %s | %lu s restantes | agit transcurrido: %lu ms\n", washer.phaseLabel(), modeLabel(washer.mode()),
                  cycleLabel(washer.cycle()), remS, (unsigned long)washer.agitElapsedMs());
}

void handleSerial() {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;
    Serial.printf("> %c\n", c);

    switch (c) {
        case 'g':
        case 'G':
            washer.setWaterMode(selectedWater);
            if (washer.start(selectedCycle, selectedMode)) {
                beepStart();
                Serial.printf("INICIANDO - %s / %s / Agua: %s\n",
                              cycleLabel(selectedCycle), modeLabel(selectedMode), waterFillLabel(selectedWater));
                lastStatusPrint = 0;
            } else {
                Serial.println("Ya esta corriendo. Cancela primero (S).");
            }
            break;
        case 'p':
        case 'P':
            if (washer.phase() == CyclePhase::PAUSED) {
                washer.resume();
                beepStart();
                Serial.println("REANUDANDO");
            } else {
                washer.pause();
                beepPause();
                Serial.println("PAUSADO");
            }
            break;
        case 's':
        case 'S':
            washer.cancel();
            stopBeepRequested = true;
            Serial.println("CANCELADO");
            break;
        case '+':
            onCicloPress();
            break;
        case '*':
            onModoPress();
            break;
        case 'd':
        case 'D':
            printStatus();
            break;
        case 't':
        case 'T':
            printModeTimes();
            break;
        case '?':
            printMenu();
            break;
        default:
            Serial.println("Desconocido. '?' para ayuda.");
    }
}

// -----------------------------------------------------------------------------
void applyDefaultConfig() {
    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        washer.params[i] = DEFAULT_PARAMS[i];
    }
}

void loadPersistentConfig() {
    PersistedConfig cfg = {};
    prefs.begin(PREF_NS, true);
    size_t len = prefs.getBytesLength(PREF_KEY);
    if (len == sizeof(PersistedConfig)) {
        prefs.getBytes(PREF_KEY, &cfg, sizeof(PersistedConfig));
    }
    prefs.end();

    if (len != sizeof(PersistedConfig) || cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION) {
        Serial.println("Config NVS no valida, usando defaults.");
        savePersistentConfig();
        return;
    }

    if (cfg.mode < static_cast<uint8_t>(WashMode::MODE_COUNT)) {
        selectedMode = static_cast<WashMode>(cfg.mode);
    }
    if (cfg.cycle < static_cast<uint8_t>(CycleConfig::CYCLE_COUNT)) {
        selectedCycle = static_cast<CycleConfig>(cfg.cycle);
    }
    if (cfg.water < static_cast<uint8_t>(WaterFillMode::WATER_COUNT)) {
        selectedWater = static_cast<WaterFillMode>(cfg.water);
    }

    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        washer.params[i] = cfg.params[i];
    }

    Serial.println("Config cargada desde NVS.");
}

void savePersistentConfig() {
    PersistedConfig cfg = {};
    cfg.magic = CONFIG_MAGIC;
    cfg.version = CONFIG_VERSION;
    cfg.mode = static_cast<uint8_t>(selectedMode);
    cfg.cycle = static_cast<uint8_t>(selectedCycle);
    cfg.water = static_cast<uint8_t>(selectedWater);

    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        cfg.params[i] = washer.params[i];
    }

    prefs.begin(PREF_NS, false);
    prefs.putBytes(PREF_KEY, &cfg, sizeof(PersistedConfig));
    prefs.end();
}

// -----------------------------------------------------------------------------
void setupNetworking() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    //WiFi.softAP(AP_SSID);
    dnsServer.start(53, "*", AP_IP);
}

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", htmlPage());
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", jsonStatus());
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", jsonConfig());
    });

    server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (request->hasParam("cycle", true)) {
            CycleConfig c;
            if (parseCycle(request->getParam("cycle", true)->value(), c)) selectedCycle = c;
        }
        if (request->hasParam("mode", true)) {
            WashMode m;
            if (parseMode(request->getParam("mode", true)->value(), m)) selectedMode = m;
        }
        if (request->hasParam("water", true)) {
            WaterFillMode w;
            if (parseWater(request->getParam("water", true)->value(), w)) selectedWater = w;
        }
        washer.setWaterMode(selectedWater);

        bool ok = washer.start(selectedCycle, selectedMode);
        if (ok) beepStart();
        request->send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest* request) {
        washer.pause();
        beepPause();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/resume", HTTP_POST, [](AsyncWebServerRequest* request) {
        washer.resume();
        beepStart();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/cancel", HTTP_POST, [](AsyncWebServerRequest* request) {
        washer.cancel();
        stopBeepRequested = true;
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/select", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (washer.phase() != CyclePhase::IDLE) {
            request->send(409, "application/json", "{\"ok\":false,\"error\":\"locked_until_cancel\"}");
            return;
        }

        if (request->hasParam("cycle", true)) {
            CycleConfig c;
            if (parseCycle(request->getParam("cycle", true)->value(), c)) selectedCycle = c;
        }
        if (request->hasParam("mode", true)) {
            WashMode m;
            if (parseMode(request->getParam("mode", true)->value(), m)) selectedMode = m;
        }
        if (request->hasParam("water", true)) {
            WaterFillMode w;
            if (parseWater(request->getParam("water", true)->value(), w)) selectedWater = w;
        }

        washer.setWaterMode(selectedWater);

        savePersistentConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/save", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (washer.phase() != CyclePhase::IDLE) {
            request->send(409, "application/json", "{\"ok\":false,\"error\":\"locked_until_cancel\"}");
            return;
        }

        if (!request->hasParam("mode", true)) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"mode_required\"}");
            return;
        }

        WashMode targetMode;
        if (!parseMode(request->getParam("mode", true)->value(), targetMode)) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"mode_invalid\"}");
            return;
        }

        WashParams p = washer.params[static_cast<uint8_t>(targetMode)];

        if (request->hasParam("agit", true)) p.agitTime_ms = parseUInt(request->getParam("agit", true)->value(), p.agitTime_ms);
        if (request->hasParam("pause", true)) p.agitPause_ms = parseUInt(request->getParam("pause", true)->value(), p.agitPause_ms);
        if (request->hasParam("wtime", true)) p.washTotal_ms = parseUInt(request->getParam("wtime", true)->value(), p.washTotal_ms);
        if (request->hasParam("rtime", true)) p.rinseAgitTime_ms = parseUInt(request->getParam("rtime", true)->value(), p.rinseAgitTime_ms);
        if (request->hasParam("rtimeTotal", true)) p.rinseTotal_ms = parseUInt(request->getParam("rtimeTotal", true)->value(), p.rinseTotal_ms);
        if (request->hasParam("spin", true)) p.spinTime_ms = parseUInt(request->getParam("spin", true)->value(), p.spinTime_ms);
        if (request->hasParam("drain", true)) p.drainTime_ms = parseUInt(request->getParam("drain", true)->value(), p.drainTime_ms);
        if (request->hasParam("fill", true)) p.fillTimeout_ms = parseUInt(request->getParam("fill", true)->value(), p.fillTimeout_ms);

        washer.params[static_cast<uint8_t>(targetMode)] = p;
        savePersistentConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // Endpoints tipicos de deteccion de portal cautivo
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    server.begin();
}

String jsonStatus() {
    bool isIdle = washer.phase() == CyclePhase::IDLE;
    String s = "{";
    s += "\"phase\":\"" + String(washer.phaseLabel()) + "\",";
    s += "\"activePhase\":\"" + String(washer.activePhaseLabel()) + "\",";
    s += "\"running\":" + String(washer.isRunning() ? "true" : "false") + ",";
    s += "\"canEdit\":" + String(isIdle ? "true" : "false") + ",";
    s += "\"mode\":" + String(static_cast<uint8_t>(washer.mode())) + ",";
    s += "\"cycle\":" + String(static_cast<uint8_t>(washer.cycle())) + ",";
    s += "\"selectedMode\":" + String(static_cast<uint8_t>(selectedMode)) + ",";
    s += "\"selectedCycle\":" + String(static_cast<uint8_t>(selectedCycle)) + ",";
    s += "\"selectedWater\":" + String(static_cast<uint8_t>(selectedWater)) + ",";
    s += "\"remainingMs\":" + String(washer.remaining()) + ",";
    s += "\"remainingPhaseMs\":" + String(washer.remainingCurrentPhaseMs()) + ",";
    s += "\"remainingTotalMs\":" + String(washer.remainingTotalMs()) + ",";
    s += "\"agitElapsedMs\":" + String(washer.agitElapsedMs()) + ",";
    s += "\"error\":" + String(static_cast<uint8_t>(washer.error())) + ",";
    s += "\"errorLabel\":\"" + String(washer.errorLabel()) + "\",";
    s += "\"waterFull\":" + String(digitalRead(Pinout::SENSOR_NIVEL) == LOW ? "true" : "false");
    s += "}";
    return s;
}

String jsonConfig() {
    String s = "{";
    s += "\"selectedMode\":" + String(static_cast<uint8_t>(selectedMode)) + ",";
    s += "\"selectedCycle\":" + String(static_cast<uint8_t>(selectedCycle)) + ",";
    s += "\"selectedWater\":" + String(static_cast<uint8_t>(selectedWater)) + ",";
    s += "\"modes\":[";

    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        const WashParams& p = washer.params[i];
        if (i) s += ",";
        s += "{";
        s += "\"id\":" + String(i) + ",";
        s += "\"label\":\"" + String(modeLabel(static_cast<WashMode>(i))) + "\",";
        s += "\"agit\":" + String(p.agitTime_ms) + ",";
        s += "\"pause\":" + String(p.agitPause_ms) + ",";
        s += "\"wtime\":" + String(p.washTotal_ms) + ",";
        s += "\"rtime\":" + String(p.rinseAgitTime_ms) + ",";
        s += "\"rtimeTotal\":" + String(p.rinseTotal_ms) + ",";
        s += "\"spin\":" + String(p.spinTime_ms) + ",";
        s += "\"drain\":" + String(p.drainTime_ms) + ",";
        s += "\"fill\":" + String(p.fillTimeout_ms);
        s += "}";
    }

    s += "],";
    s += "\"waters\":[";
    for (uint8_t i = 0; i < static_cast<uint8_t>(WaterFillMode::WATER_COUNT); i++) {
        if (i) s += ",";
        s += "{";
        s += "\"id\":" + String(i) + ",";
        s += "\"label\":\"" + String(waterFillLabel(static_cast<WaterFillMode>(i))) + "\"";
        s += "}";
    }
    s += "],";
    s += "\"cycles\":[";
    for (uint8_t i = 0; i < static_cast<uint8_t>(CycleConfig::CYCLE_COUNT); i++) {
        if (i) s += ",";
        s += "{";
        s += "\"id\":" + String(i) + ",";
        s += "\"label\":\"" + String(cycleLabel(static_cast<CycleConfig>(i))) + "\"";
        s += "}";
    }
    s += "]";
    s += "}";

    return s;
}

String htmlPage() {
    return R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>Lavadora ESP32</title>
  <style>
    :root { --bg:#f6f8ee; --card:#ffffff; --ink:#10231a; --muted:#466354; --ok:#1f8f53; --warn:#b87d00; --danger:#b42828; --line:#d5e0d8; }
    * { box-sizing: border-box; }
    body { margin: 0; font-family: "Trebuchet MS", "Segoe UI", sans-serif; color: var(--ink); background:
      radial-gradient(circle at 10% -10%, #d8f1da 0, transparent 40%),
      radial-gradient(circle at 100% 0%, #f8e3b9 0, transparent 36%),
      var(--bg); }
    .wrap { max-width: 980px; margin: 0 auto; padding: 18px; }
    .hero { border: 1px solid var(--line); border-radius: 18px; padding: 16px; background: linear-gradient(145deg, #f4fff6, #fff8e9); }
    h1 { margin: 0 0 8px; letter-spacing: 0.4px; }
    .grid { display: grid; gap: 14px; grid-template-columns: repeat(auto-fit, minmax(290px, 1fr)); margin-top: 14px; }
    .card { background: var(--card); border: 1px solid var(--line); border-radius: 14px; padding: 14px; box-shadow: 0 8px 18px rgba(40,60,20,.07); }
    .row { display: grid; gap: 8px; grid-template-columns: 1fr 1fr; }
    .phaseBig { margin: 0 0 8px; font-size: clamp(1.55rem, 2.3vw, 2.3rem); line-height: 1.1; font-weight: 700; }
    .timers { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; margin: 6px 0 8px; }
    .heroMeta { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; }
    .kpi { border: 1px solid var(--line); border-radius: 12px; padding: 8px 10px; background: rgba(255,255,255,.55); }
    .kpi b { display: block; font-size: .83rem; color: var(--muted); font-weight: 600; }
    .kpi span { display: block; font-size: 1.15rem; font-weight: 700; margin-top: 2px; }
    .kpi.statusBox span { font-size: 1rem; font-weight: 600; }
    .metaLine { margin: 6px 0 0; font-size: .95rem; color: var(--muted); }
    .lockMsg { margin-top: 8px; color: #7a5615; font-weight: 600; min-height: 1.2em; }
    label { display: block; font-size: .9rem; color: var(--muted); margin-bottom: 4px; }
    select,input,button { width: 100%; border-radius: 10px; border: 1px solid #bfcfbe; padding: 9px 10px; font-size: 1rem; }
    button { cursor: pointer; background: #fff; }
    button.ok { background: var(--ok); border-color: var(--ok); color: #fff; }
    button.warn { background: var(--warn); border-color: var(--warn); color: #fff; }
    button.danger { background: var(--danger); border-color: var(--danger); color: #fff; }
    .badge { display: inline-block; border-radius: 999px; padding: 4px 10px; font-size: .88rem; border: 1px solid var(--line); background: #f8fbf8; }
    .muted { color: var(--muted); }
        .toast {
            position: fixed;
            right: 16px;
            bottom: 16px;
            z-index: 999;
            padding: 10px 12px;
            border-radius: 10px;
            color: #fff;
            font-size: .92rem;
            opacity: 0;
            transform: translateY(8px);
            transition: opacity .2s ease, transform .2s ease;
            pointer-events: none;
        }
        .toast.show { opacity: 1; transform: translateY(0); }
        .toast.ok { background: #1f8f53; }
        .toast.err { background: #b42828; }
                @media (max-width: 680px) { .row { grid-template-columns: 1fr; } .timers { grid-template-columns: 1fr; } .heroMeta { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
<div class="wrap">
  <div class="hero">
                <p id="currentPhase" class="phaseBig">Conectando...</p>
        <div class="timers">
            <div class="kpi"><b>Tiempo restante total</b><span id="remainingTotal">--</span></div>
            <div class="kpi"><b>Tiempo ciclo actual</b><span id="remainingPhase">--</span></div>
        </div>
                <div class="heroMeta">
                        <div class="kpi"><b>Nivel de agua</b><span id="waterLevelLine">--</span></div>
                        <div class="kpi statusBox"><b>Estado</b><span id="statusLine">Conectando...</span></div>
                        <div id="errorItem" class="kpi statusBox" style="display:none;"><b>Detalle</b><span id="errorLine"></span></div>
                </div>
  </div>

  <div class="grid">
    <section class="card">
      <h3>Control</h3>
      <div class="row">
        <div>
          <label for="cycle">Ciclo</label>
          <select id="cycle"></select>
        </div>
        <div>
          <label for="mode">Modo</label>
          <select id="mode"></select>
        </div>
      </div>
            <div class="row" style="margin-top:8px;">
                <div>
                    <label for="water">Agua</label>
                    <select id="water"></select>
                </div>
                <div></div>
            </div>
      <div class="row" style="margin-top:10px;">
                <button id="startBtn" class="ok" onclick="startCycle()">Iniciar</button>
                <button id="pauseBtn" class="warn" onclick="pauseResume()">Pausar/Reanudar</button>
      </div>
      <div class="row" style="margin-top:10px;">
                <button id="saveSelBtn" onclick="saveSelection()">Guardar seleccion</button>
        <button class="danger" onclick="cancelCycle()">Cancelar</button>
      </div>
    </section>

    <section class="card">
      <h3>Parametros por modo</h3>
      <div class="row">
        <div><label>Lavado: tiempo total (ms)</label><input id="wtime" type="number" min="1000" /></div>
        <div><label>Lavado: giro por sentido (ms)</label><input id="agit" type="number" min="200" /></div>
        <div><label>Enjuague: tiempo total (ms)</label><input id="rtimeTotal" type="number" min="1000" /></div>
        <div><label>Enjuague: giro por sentido (ms)</label><input id="rtime" type="number" min="200" /></div>
        <div><label>Centrifugado total (ms)</label><input id="spin" type="number" min="1000" /></div>
        <div><label>Desagüe (ms)</label><input id="drain" type="number" min="1000" /></div>
        <div><label>Pausa entre cambios de sentido (ms)</label><input id="pause" type="number" min="100" /></div>
        <div><label>Tiempo maximo de llenado (ms)</label><input id="fill" type="number" min="1000" /></div>
      </div>
            <p class="muted" style="margin-top:10px; font-size:.88rem; line-height:1.35;">
                Guía rápida: 1000 ms = 1 segundo. Ejemplo: 5000 ms = 5 s.<br>
                El tiempo total define cuánto dura completa la fase de lavado o enjuague.<br>
                La pausa entre inversiones controla cuánto espera antes de cambiar de sentido.<br>
                Si subes el tiempo total, la fase dura más.<br>
                Si subes el timeout de llenado, tarda más en declarar error por falta de agua.
            </p>
            <button id="saveParamsBtn" style="margin-top:10px;" onclick="saveParams()">Guardar parametros del modo</button>
            <p id="lockLine" class="lockMsg"></p>
    </section>
  </div>
</div>
<div id="toast" class="toast"></div>

<script>
let statusData = null;
let configData = null;
let hasLocalChanges = false;

const paramFieldIds = ['agit','pause','wtime','rtime','rtimeTotal','spin','drain','fill'];

function formatDuration(ms) {
    const sec = Math.max(0, Math.floor((Number(ms) || 0) / 1000));
    const s = sec % 60;
    const mTotal = Math.floor(sec / 60);
    const m = mTotal % 60;
    const h = Math.floor(mTotal / 60);

    const pad2 = (n) => String(n).padStart(2, '0');
    if (h > 0) return `${h}h ${pad2(m)}m ${pad2(s)}s`;
    if (mTotal > 0) return `${mTotal}m ${pad2(s)}s`;
    return `${s}s`;
}

function estimateSelectedTotalMs() {
    if (!configData) return 0;
    const modeId = parseInt(document.getElementById('mode').value, 10);
    const cycleId = parseInt(document.getElementById('cycle').value, 10);
    const p = configData.modes.find(x => x.id === modeId);
    if (!p) return 0;

    switch (cycleId) {
        case 0: return p.fill + p.wtime + p.drain + p.fill + p.rtimeTotal + p.drain + p.spin;
        case 1: return p.fill + p.rtimeTotal + p.drain + p.spin;
        case 2: return p.fill + p.wtime + p.drain;
        case 3: return p.fill + p.rtimeTotal + p.drain;
        case 4: return p.spin;
        default: return 0;
    }
}

function formatStatusText() {
    if (!statusData) return 'Sin datos';
    if (statusData.phase === 'Reposo') return 'Lista para iniciar';
    if (statusData.phase === 'Pausado') return `Proceso pausado en ${statusData.activePhase}`;
    if (statusData.phase === 'ERROR') return 'Proceso detenido por error';
    return `${statusData.activePhase}`;
}

function updateHero() {
    if (!statusData) return;

    const currentPhase = document.getElementById('currentPhase');
    const remainingTotal = document.getElementById('remainingTotal');
    const remainingPhase = document.getElementById('remainingPhase');
    const waterLevelLine = document.getElementById('waterLevelLine');
    const statusLine = document.getElementById('statusLine');
    const errorItem = document.getElementById('errorItem');
    const errorLine = document.getElementById('errorLine');

    const isError = statusData.phase === 'ERROR';
    const isPaused = statusData.phase === 'Pausado';

    let phaseText = statusData.activePhase || statusData.phase;
    if (isPaused) phaseText = `Pausado (${statusData.activePhase || 'fase'})`;
    if (statusData.phase === 'Reposo') phaseText = 'Reposo';
    if (isError) phaseText = 'Error';

    currentPhase.textContent = phaseText;
    const totalToShow = statusData.canEdit ? estimateSelectedTotalMs() : statusData.remainingTotalMs;
    remainingTotal.textContent = formatDuration(totalToShow);
    remainingPhase.textContent = formatDuration(statusData.remainingPhaseMs);
    waterLevelLine.textContent = statusData.waterFull ? 'Lleno' : 'Vacio';
    statusLine.textContent = formatStatusText();

    if (isError) {
        errorLine.textContent = statusData.errorLabel || 'Error desconocido';
        errorLine.style.color = '#b42828';
        errorItem.style.display = 'block';
    } else {
        errorLine.textContent = '';
        errorItem.style.display = 'none';
    }
}

function updateEditLock() {
    if (!statusData) return;
    const canEdit = !!statusData.canEdit;
    const lockMsg = canEdit ? '' : 'Proceso iniciado: para cambiar ciclo, modo, agua o tiempos debes cancelar.';

    const lockTargets = [
        'cycle','mode','water','saveSelBtn','saveParamsBtn',
        'agit','pause','wtime','rtime','rtimeTotal','spin','drain','fill'
    ];

    lockTargets.forEach(id => {
        const el = document.getElementById(id);
        if (!el) return;
        el.disabled = !canEdit;
    });

    const lockLine = document.getElementById('lockLine');
    if (lockLine) lockLine.textContent = lockMsg;
}

function mapApiError(err) {
    if (!err || !err.code) return err?.message || 'Error desconocido';
    if (err.code === 'locked_until_cancel') return 'Proceso iniciado: primero cancela para poder cambiar parametros.';
    if (err.code === 'mode_required') return 'Debes indicar el modo.';
    if (err.code === 'mode_invalid') return 'El modo enviado no es valido.';
    if (err.code === 'running') return 'La lavadora esta corriendo.';
    return err.message || err.code;
}

function getCurrentModeParamsFromForm() {
    return {
        agit: document.getElementById('agit').value,
        pause: document.getElementById('pause').value,
        wtime: document.getElementById('wtime').value,
        rtime: document.getElementById('rtime').value,
        rtimeTotal: document.getElementById('rtimeTotal').value,
        spin: document.getElementById('spin').value,
        drain: document.getElementById('drain').value,
        fill: document.getElementById('fill').value
    };
}

function areParamFieldsDirty() {
    if (!configData) return false;
    const modeId = parseInt(document.getElementById('mode').value, 10);
    const p = configData.modes.find(x => x.id === modeId);
    if (!p) return false;
    const current = getCurrentModeParamsFromForm();
    return paramFieldIds.some(k => String(current[k]) !== String(p[k]));
}

function areSelectionFieldsDirty() {
    if (!configData) return false;
    return String(document.getElementById('cycle').value) !== String(configData.selectedCycle) ||
                 String(document.getElementById('mode').value) !== String(configData.selectedMode) ||
                 String(document.getElementById('water').value) !== String(configData.selectedWater);
}

function formEncode(obj) {
  const p = new URLSearchParams();
  Object.keys(obj).forEach(k => p.append(k, obj[k]));
  return p;
}

let toastTimer = null;
function showToast(message, type = 'ok') {
    const toast = document.getElementById('toast');
    if (!toast) return;

    toast.textContent = message;
    toast.className = `toast ${type}`;
    requestAnimationFrame(() => toast.classList.add('show'));

    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
        toast.classList.remove('show');
    }, 1800);
}

async function apiGet(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

async function apiPost(url, data = {}) {
  const r = await fetch(url, {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: formEncode(data)
  });
    if (!r.ok) {
        let payload = null;
        try { payload = await r.json(); } catch (_) {}
        throw { status: r.status, code: payload?.error, message: payload?.error || ('HTTP ' + r.status) };
    }
  return r.json();
}

function loadSelectors() {
  const cycleSel = document.getElementById('cycle');
  const modeSel = document.getElementById('mode');
    const waterSel = document.getElementById('water');

  cycleSel.innerHTML = '';
  configData.cycles.forEach(c => {
    const o = document.createElement('option');
    o.value = c.id;
    o.textContent = c.label;
    cycleSel.appendChild(o);
  });

  modeSel.innerHTML = '';
  configData.modes.forEach(m => {
    const o = document.createElement('option');
    o.value = m.id;
    o.textContent = m.label;
    modeSel.appendChild(o);
  });

  cycleSel.value = configData.selectedCycle;
  modeSel.value = configData.selectedMode;

    waterSel.innerHTML = '';
    configData.waters.forEach(w => {
        const o = document.createElement('option');
        o.value = w.id;
        o.textContent = w.label;
        waterSel.appendChild(o);
    });
    waterSel.value = configData.selectedWater;

  fillModeParams();
    hasLocalChanges = false;
}

function fillModeParams() {
  const modeId = parseInt(document.getElementById('mode').value, 10);
  const p = configData.modes.find(x => x.id === modeId);
  if (!p) return;
    paramFieldIds.forEach(k => {
    document.getElementById(k).value = p[k];
  });
}

function configChanged(prev, next) {
    if (!prev) return true;
    if (prev.selectedMode !== next.selectedMode) return true;
    if (prev.selectedCycle !== next.selectedCycle) return true;
    if (prev.selectedWater !== next.selectedWater) return true;
    if (prev.modes.length !== next.modes.length) return true;

    for (let i = 0; i < prev.modes.length; i++) {
        const a = prev.modes[i];
        const b = next.modes[i];
        if (!b) return true;
        if (a.agit !== b.agit || a.pause !== b.pause || a.wtime !== b.wtime ||
            a.rtime !== b.rtime || a.rtimeTotal !== b.rtimeTotal ||
            a.spin !== b.spin || a.drain !== b.drain || a.fill !== b.fill) {
            return true;
        }
    }

    return false;
}

function applyConfigToUI(previousConfig, nextConfig) {
    const initialLoad = !previousConfig;
    const cycleSel = document.getElementById('cycle');
    const modeSel = document.getElementById('mode');
    const waterSel = document.getElementById('water');

    if (initialLoad) {
        configData = nextConfig;
        loadSelectors();
        return;
    }

    if (cycleSel && String(cycleSel.value) !== String(nextConfig.selectedCycle)) {
        cycleSel.value = String(nextConfig.selectedCycle);
    }
    if (modeSel && String(modeSel.value) !== String(nextConfig.selectedMode)) {
        modeSel.value = String(nextConfig.selectedMode);
        fillModeParams();
    }
    if (waterSel && String(waterSel.value) !== String(nextConfig.selectedWater)) {
        waterSel.value = String(nextConfig.selectedWater);
    }

    configData = nextConfig;
}

function updateStartButton() {
    const startBtn = document.getElementById('startBtn');
    if (!startBtn || !statusData) return;

    const inIdle = statusData.phase === 'Reposo';
    startBtn.disabled = !inIdle;
    startBtn.style.opacity = inIdle ? '1' : '0.55';
    startBtn.style.cursor = inIdle ? 'pointer' : 'not-allowed';
}

function syncLocalSelections() {
    if (!statusData) return;
    if (hasLocalChanges) return;

    const cycleSel = document.getElementById('cycle');
    const modeSel = document.getElementById('mode');
    const waterSel = document.getElementById('water');

    if (cycleSel && String(cycleSel.value) !== String(statusData.selectedCycle)) {
        cycleSel.value = String(statusData.selectedCycle);
    }

    let modeChanged = false;
    if (modeSel && String(modeSel.value) !== String(statusData.selectedMode)) {
        modeSel.value = String(statusData.selectedMode);
        modeChanged = true;
    }

    if (waterSel && String(waterSel.value) !== String(statusData.selectedWater)) {
        waterSel.value = String(statusData.selectedWater);
    }

    if (modeChanged) fillModeParams();
}

async function refresh() {
  statusData = await apiGet('/api/status');
    updateHero();
    updateStartButton();
    updateEditLock();
    syncLocalSelections();
}

async function refreshConfig() {
    const nextConfig = await apiGet('/api/config');
    if (!configData) {
        applyConfigToUI(null, nextConfig);
        return;
    }

    if (hasLocalChanges) {
        return;
    }

    if (!configChanged(configData, nextConfig)) return;
    applyConfigToUI(configData, nextConfig);
}

async function startCycle() {
    const startBtn = document.getElementById('startBtn');
    if (startBtn && startBtn.disabled) return;

  try {
    await apiPost('/api/start', {
      cycle: document.getElementById('cycle').value,
            mode: document.getElementById('mode').value,
            water: document.getElementById('water').value
    });
    await refresh();
    } catch (e) { alert('No se pudo iniciar: ' + mapApiError(e)); }
}

async function pauseResume() {
  try {
    if (statusData && statusData.phase === 'Pausado') await apiPost('/api/resume');
    else await apiPost('/api/pause');
    await refresh();
    } catch (e) { alert('Error pausa/reanudar: ' + mapApiError(e)); }
}

async function cancelCycle() {
  try {
    await apiPost('/api/cancel');
    await refresh();
    } catch (e) { alert('Error al cancelar: ' + mapApiError(e)); }
}

async function saveSelection() {
    if (statusData && !statusData.canEdit) {
        showToast('Primero cancela para editar', 'err');
        return;
    }
  try {
    await apiPost('/api/select', {
      cycle: document.getElementById('cycle').value,
            mode: document.getElementById('mode').value,
            water: document.getElementById('water').value
    });
        hasLocalChanges = false;
        await refreshConfig();
        showToast('Seleccion guardada');
    } catch (e) { alert('No se pudo guardar seleccion: ' + mapApiError(e)); }
}

async function saveParams() {
    if (statusData && !statusData.canEdit) {
        showToast('Primero cancela para editar', 'err');
        return;
    }
  try {
    const mode = document.getElementById('mode').value;
    await apiPost('/api/save', {
      mode,
      agit: document.getElementById('agit').value,
      pause: document.getElementById('pause').value,
            wtime: document.getElementById('wtime').value,
      rtime: document.getElementById('rtime').value,
            rtimeTotal: document.getElementById('rtimeTotal').value,
      spin: document.getElementById('spin').value,
      drain: document.getElementById('drain').value,
      fill: document.getElementById('fill').value
    });
        hasLocalChanges = false;
    await refreshConfig();
        showToast('Parametros guardados');
    } catch (e) { alert('No se pudieron guardar parametros: ' + mapApiError(e)); }
}

function onSelectionChanged() {
    hasLocalChanges = true;
}

document.getElementById('mode').addEventListener('change', () => {
    fillModeParams();
    onSelectionChanged();
});
document.getElementById('cycle').addEventListener('change', onSelectionChanged);
document.getElementById('water').addEventListener('change', onSelectionChanged);
paramFieldIds.forEach(id => {
    document.getElementById(id).addEventListener('input', onSelectionChanged);
});

(async () => {
  try {
    await refreshConfig();
    await refresh();
    setInterval(refresh, 1500);
        setInterval(refreshConfig, 5000);
  } catch (e) {
    document.getElementById('statusLine').textContent = 'Error de conexion: ' + e.message;
  }
})();
</script>
</body>
</html>
)HTML";
}

#ifdef USE_OLED
void setupOled() {
    Wire.begin(Pinout::OLED_SDA, Pinout::OLED_SCL);
    oledReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (!oledReady) {
        Serial.println("OLED no detectado en 0x3C");
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(DEVICE_DISPLAY_NAME);
    display.println("OLED OK");
    display.display();
}

void renderOled() {
    if (!oledReady) return;
    if (millis() - lastOledUpdate < 500) return;
    lastOledUpdate = millis();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    display.println(DEVICE_DISPLAY_NAME);
    display.println(String("Fase: ") + washer.phaseLabel());
    display.println(String("Modo: ") + modeLabel(selectedMode));
    display.println(String("Ciclo:") + cycleLabel(selectedCycle));
    display.println(String("Rem:") + String(washer.remaining() / 1000) + "s");
    display.println(String("Agua:") + (digitalRead(Pinout::SENSOR_NIVEL) == LOW ? "Llena" : "Vacia"));
    display.display();
}
#endif
