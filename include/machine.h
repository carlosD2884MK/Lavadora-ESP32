#pragma once
#include <Arduino.h>
#include "config.h"
#include "pinout.h"

// ── Fases principales del ciclo ───────────────────────────────────────────
enum class CyclePhase : uint8_t {
    IDLE          = 0,
    FILLING_WASH,    // llenando para lavado
    WASHING,         // agitando (lavado)
    DRAINING_WASH,   // desaguando tras lavado
    FILLING_RINSE,   // llenando para enjuague
    RINSING,         // agitando (enjuague)
    DRAINING_RINSE,  // desaguando tras enjuague
    SPINNING,        // centrifugando
    PAUSED,
    ERROR
};

// ── Sub-estados del motor durante agitación ───────────────────────────────
enum class MotorPhase : uint8_t {
    STOPPED  = 0,
    GOING_A,    // girando sentido horario
    PAUSING_A,  // pausa tras A, antes de B
    GOING_B,    // girando sentido antihorario
    PAUSING_B   // pausa tras B, antes del siguiente A
};

// ── Códigos de error ──────────────────────────────────────────────────────
enum class ErrorCode : uint8_t {
    NONE         = 0,
    FILL_TIMEOUT     // nivel no alcanzado en fillTimeout_ms
};

// ─────────────────────────────────────────────────────────────────────────
class WashingMachine {
public:
    WashingMachine();

    void begin();
    void update();   // llamar en cada iteración de loop() — no bloqueante

    // Control
    bool start(CycleConfig cycle, WashMode mode);  // false si ya está corriendo
    void setWaterMode(WaterFillMode mode) { _waterMode = mode; }
    void pause();
    void resume();
    void cancel();

    // Getters
    CyclePhase  phase()          const { return _phase; }
    WashMode    mode()           const { return _mode; }
    WaterFillMode waterMode()    const { return _waterMode; }
    CycleConfig cycle()          const { return _cycle; }
    ErrorCode   error()          const { return _error; }
    const char* errorLabel()     const;
    MotorPhase  motorPhase()     const { return _motorPhase; }
    CyclePhase  activePhase()    const { return (_phase == CyclePhase::PAUSED) ? _pausedPhase : _phase; }
    const char* activePhaseLabel() const;
    uint32_t    agitElapsedMs()  const {
        if (_phase != CyclePhase::WASHING && _phase != CyclePhase::RINSING) return 0;
        if (_refillingLevel && (_phase == CyclePhase::WASHING || _phase == CyclePhase::RINSING) && _refillPauseStart != 0) {
            return _refillPauseStart - _phaseStart;
        }
        return millis() - _phaseStart;
    }
    bool        isRunning()      const;
    uint32_t    remaining()      const;  // ms restantes en la fase actual
    uint32_t    remainingCurrentPhaseMs() const;
    uint32_t    remainingTotalMs() const;
    const char* phaseLabel()     const;

    // Parámetros ajustables (inicializados con DEFAULT_PARAMS; editables en Fase 4)
    WashParams params[static_cast<uint8_t>(WashMode::MODE_COUNT)];

private:
    CyclePhase  _phase;
    CyclePhase  _pausedPhase;
    WashMode    _mode;
    WaterFillMode _waterMode;
    CycleConfig _cycle;
    ErrorCode   _error;
    MotorPhase  _motorPhase;
    MotorPhase  _pausedMotorPhase;

    uint32_t _phaseStart;   // millis() al entrar en la fase actual
    uint32_t _motorStart;   // millis() al cambiar sub-estado del motor
    uint32_t _pausedAt;     // millis() al pausar
    uint32_t _refillStart;  // inicio de rellenado entre agitaciones
    uint32_t _refillPauseStart; // inicio de congelamiento del tiempo por relleno
    uint32_t _levelReachedAt;   // inicio de nivel detectado como lleno para estabilizacion
    uint8_t  _rinseCount;       // tandas de enjuague completadas (0..2)
    bool     _spinningDrainStage; // true mientras centrifugado esta en pre-desagüe
    int      _agitCount;    // ciclos A/B completados en la fase de agitación
    bool     _refillingLevel;

    // Internos
    void       enterPhase(CyclePhase p);
    void       enterMotorPhase(MotorPhase mp);
    void       applyMotorDirection(bool antiClockwise, bool enableMotor);
    void       motorStop();
    void       setFill(bool on);
    void       setSpin(bool on);
    void       setDrain(bool on);
    CyclePhase afterDrain() const;

    void updateFilling();
    void updateAgitating();
    void updateDraining();
    void updateSpinning();

    uint32_t phaseDurationMs(CyclePhase phase, const WashParams& p) const;
    const char* phaseLabelOf(CyclePhase phase) const;
};
