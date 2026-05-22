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
    uint32_t washTotal_ms;      // Tiempo total de la fase de lavado (ms)
    uint32_t rinseTotal_ms;     // Tiempo total de la fase de enjuague (ms)
    uint32_t spinTime_ms;       // Duracion total de centrifugado (ms)
    uint32_t drainTime_ms;      // Duracion de desagüe (ms)
    uint32_t fillTimeout_ms;    // Tiempo maximo para llenado antes de error (ms)
    uint32_t fillEstimate_ms;   // Tiempo estimado de llenado (ms)
};

// ── Valores por defecto ───────────────────────────────────────────────────

// Columnas:
// 1) washTotal_ms     -> tiempo total de lavado
// 2) rinseTotal_ms    -> tiempo total de enjuague
// 3) spinTime_ms      -> tiempo total de centrifugado
// 4) drainTime_ms     -> tiempo de desagüe
// 5) fillTimeout_ms   -> timeout de llenado
// 6) fillEstimate_ms  -> tiempo estimado de llenado

static const WashParams DEFAULT_PARAMS[4] = {
    /* SOFT    */   { 300000, 180000, 180000, 30000, 1200000, 900000 },
    /* NORMAL  */   { 480000, 180000, 300000, 30000, 1200000, 900000 },
    /* STRONG  */   { 600000, 180000, 300000, 30000, 1500000, 1200000 },
    /* XSTRONG */   { 720000, 180000, 300000, 30000, 1500000, 1200000 },
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
