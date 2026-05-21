#pragma once
#include <stdint.h>

// ── Configuración visible al usuario (editable rápidamente) ───────────────
// Cambia estos #define para personalizar el nombre del equipo y el WiFi AP.
#define DEVICE_DISPLAY_NAME "Lavadora ESP32"
#define WIFI_AP_SSID        "Lavadora-ESP32"
#define WIFI_AP_PASSWORD    ""

// ── Modos de lavado (intensidad de agitación) ─────────────────────────────
enum class WashMode : uint8_t {
    SOFT   = 0,   // Suave
    NORMAL = 1,   // Normal
    STRONG = 2,   // Fuerte
    XSTRONG = 3,  // Muy fuerte
    MODE_COUNT
};

// ── Selección de ciclo ────────────────────────────────────────────────────
enum class CycleConfig : uint8_t {
    FULL       = 0,  // Lavado + Enjuague + Centrifugado
    RINSE_SPIN = 1,  // Enjuague + Centrifugado
    WASH_ONLY  = 2,  // Solo Lavado
    RINSE_ONLY = 3,  // Solo Enjuague
    SPIN_ONLY  = 4,  // Solo Centrifugado
    CYCLE_COUNT
};

// ── Selección de temperatura de agua para llenado ─────────────────────────
enum class WaterFillMode : uint8_t {
    COLD = 0,
    HOT  = 1,
    BOTH = 2,
    WATER_COUNT
};

// ── Parámetros de un ciclo (configurables en tiempo de ejecución) ─────────
struct WashParams {
    uint32_t agitTime_ms;       // Tiempo de motor encendido por pulso durante lavado (ms)
    uint32_t agitPause_ms;      // Pausa minima entre pulsos de motor (ms)
    uint32_t washTotal_ms;      // Tiempo total de la fase de lavado (ms)
    uint32_t rinseAgitTime_ms;  // Tiempo de motor encendido por pulso durante enjuague (ms)
    uint32_t rinseTotal_ms;     // Tiempo total de la fase de enjuague (ms)
    uint32_t spinTime_ms;       // Duracion total de centrifugado (ms)
    uint32_t drainTime_ms;      // Duracion de desagüe (ms)
    uint32_t fillEstimate_ms;   // Tiempo estimado de llenado usado para mostrar duraciones (ms)
    uint32_t fillTimeout_ms;    // Tiempo maximo para llenado antes de error (ms)
    bool enableSecondRinse;     // Habilita el segundo enjuague
};

// ── Valores por defecto ───────────────────────────────────────────────────
// Columnas:
// 1) agitTime_ms      -> tiempo de pulso de motor en lavado
// 2) agitPause_ms     -> pausa entre pulsos
// 3) washTotal_ms     -> tiempo total de lavado
// 4) rinseAgitTime_ms -> tiempo de pulso de motor en enjuague
// 5) rinseTotal_ms    -> tiempo total de enjuague
// 6) spinTime_ms      -> tiempo total de centrifugado
// 7) drainTime_ms     -> tiempo de desagüe
// 8) fillEstimate_ms  -> tiempo estimado de llenado para mostrar en la web
// 9) fillTimeout_ms   -> timeout de llenado
static const WashParams DEFAULT_PARAMS[4] = {
    /* SOFT    */ {   4000,  1000,  720000,   3000,  360000, 240000, 120000, 900000, 1200000, true },
    /* NORMAL  */ {   5000,  1000, 1080000,   4000,  480000, 360000, 120000, 900000, 1200000, true },
    /* STRONG  */ {   7000,  1000, 1440000,   5000,  600000, 480000, 120000, 900000, 1200000, true },
    /* XSTRONG */ {  10000,  1000, 1800000,   6000,  720000, 600000, 120000, 900000, 1200000, true },
};

// ── Etiquetas ─────────────────────────────────────────────────────────────
inline const char* modeLabel(WashMode m) {
    switch (m) {
        case WashMode::SOFT:    return "Suave";
        case WashMode::NORMAL:  return "Normal";
        case WashMode::STRONG:  return "Fuerte";
        case WashMode::XSTRONG: return "Muy Fuerte";
        default:                return "?";
    }
}

inline const char* cycleLabel(CycleConfig c) {
    switch (c) {
        case CycleConfig::FULL:       return "Lavado+Enjuague+Centrifugado";
        case CycleConfig::RINSE_SPIN: return "Enjuague+Centrifugado";
        case CycleConfig::WASH_ONLY:  return "Solo Lavado";
        case CycleConfig::RINSE_ONLY: return "Solo Enjuague";
        case CycleConfig::SPIN_ONLY:  return "Solo Centrifugado";
        default:                      return "?";
    }
}

inline const char* waterFillLabel(WaterFillMode w) {
    switch (w) {
        case WaterFillMode::COLD: return "Fria";
        case WaterFillMode::HOT:  return "Caliente";
        case WaterFillMode::BOTH: return "Ambas";
        default:                  return "?";
    }
}
