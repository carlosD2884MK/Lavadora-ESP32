#include "machine.h"

static const uint32_t LEVEL_STABLE_MS = 5000;
static const uint32_t LEVEL_LOST_DEBOUNCE_MS = 250;

// ─────────────────────────────────────────────────────────────────────────
WashingMachine::WashingMachine()
    : _phase(CyclePhase::IDLE)
    , _pausedPhase(CyclePhase::IDLE)
    , _mode(WashMode::NORMAL)
    , _waterMode(WaterFillMode::BOTH)
    , _cycle(CycleConfig::FULL)
    , _error(ErrorCode::NONE)
    , _motorPhase(MotorPhase::STOPPED)
    , _pausedMotorPhase(MotorPhase::STOPPED)
    , _phaseStart(0)
    , _motorStart(0)
    , _pausedAt(0)
    , _refillStart(0)
    , _refillPauseStart(0)
    , _levelReachedAt(0)
    , _levelLostAt(0)
    , _rinseCount(0)
    , _spinningDrainStage(false)
    , _agitCount(0)
    , _refillingLevel(false)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(WashMode::MODE_COUNT); i++) {
        params[i] = DEFAULT_PARAMS[i];
    }
}

void WashingMachine::begin() {
    motorStop();
    setFill(false);
    setSpin(false);
    setDrain(false);
}

// ── Bucle principal (no bloqueante) ──────────────────────────────────────
void WashingMachine::update() {
    switch (_phase) {
        case CyclePhase::FILLING_WASH:
        case CyclePhase::FILLING_RINSE_1:
        case CyclePhase::FILLING_RINSE_2:
            updateFilling();   break;
        case CyclePhase::WASHING:
        case CyclePhase::RINSING_1:
        case CyclePhase::RINSING_2:
            updateAgitating(); break;
        case CyclePhase::DRAINING_WASH:
        case CyclePhase::DRAINING_RINSE_1:
        case CyclePhase::DRAINING_RINSE_2:
            updateDraining();  break;
        case CyclePhase::SPINNING:       updateSpinning();  break;
        default: break;  // IDLE, PAUSED, ERROR: sin acción
    }
}

// ── Control público ───────────────────────────────────────────────────────
bool WashingMachine::start(CycleConfig cycle, WashMode mode) {
    if (_phase != CyclePhase::IDLE) return false;
    _cycle = cycle;
    _mode  = mode;
    _error = ErrorCode::NONE;
    _rinseCount = 0;
    switch (cycle) {
        case CycleConfig::FULL:
            enterPhase(CyclePhase::FILLING_WASH);  break;
        case CycleConfig::WASH_ONLY:
            enterPhase(CyclePhase::FILLING_WASH);  break;
        case CycleConfig::RINSE_SPIN:
            enterPhase(CyclePhase::FILLING_RINSE_1); break;
        case CycleConfig::RINSE_ONLY:
            enterPhase(CyclePhase::FILLING_RINSE_1); break;
        case CycleConfig::SPIN_ONLY:
            enterPhase(CyclePhase::SPINNING);      break;
        default: break;
    }
    return true;
}

void WashingMachine::pause() {
    if (_phase == CyclePhase::PAUSED ||
        _phase == CyclePhase::IDLE   ||
        _phase == CyclePhase::ERROR) return;

    _pausedPhase      = _phase;
    _pausedMotorPhase = _motorPhase;
    _pausedAt         = millis();

    motorStop();
    _refillingLevel = false;
    _refillPauseStart = 0;
    _levelReachedAt = 0;
    _levelLostAt = 0;
    setFill(false);
    setSpin(false);
    setDrain(false);
    _phase = CyclePhase::PAUSED;
}

void WashingMachine::resume() {
    if (_phase != CyclePhase::PAUSED) return;

    // Compensar el tiempo que estuvo pausado para no perder la cuenta
    uint32_t paused = millis() - _pausedAt;
    _phaseStart += paused;
    _motorStart += paused;

    _phase      = _pausedPhase;
    _motorPhase = _pausedMotorPhase;

    // Reiniciar salidas de hardware según el estado restaurado
    switch (_phase) {
        case CyclePhase::FILLING_WASH:
        case CyclePhase::FILLING_RINSE_1:
        case CyclePhase::FILLING_RINSE_2:
            setFill(true);
            setDrain(false);
            break;
        case CyclePhase::WASHING:
        case CyclePhase::RINSING_1:
        case CyclePhase::RINSING_2:
            if (_motorPhase == MotorPhase::GOING_A || _motorPhase == MotorPhase::GOING_B) {
                applyMotorDirection(false, true);
            }
            setDrain(false);
            break;
        case CyclePhase::DRAINING_WASH:
        case CyclePhase::DRAINING_RINSE_1:
        case CyclePhase::DRAINING_RINSE_2:
            setDrain(true);
            setSpin(false);
            break;
        case CyclePhase::SPINNING:
            setDrain(true);
            setSpin(true);
            break;
        default: break;
    }
}

void WashingMachine::cancel() {
    motorStop();
    _refillingLevel = false;
    _refillPauseStart = 0;
    _levelReachedAt = 0;
    _levelLostAt = 0;
    setFill(false);
    setSpin(false);
    setDrain(false);
    _phase      = CyclePhase::IDLE;
    _motorPhase = MotorPhase::STOPPED;
    _rinseCount = 0;
    _spinningDrainStage = false;
    _agitCount  = 0;
    _error      = ErrorCode::NONE;
}

// ── Getters ───────────────────────────────────────────────────────────────
bool WashingMachine::isRunning() const {
    return _phase != CyclePhase::IDLE   &&
           _phase != CyclePhase::PAUSED &&
           _phase != CyclePhase::ERROR;
}

uint32_t WashingMachine::remaining() const {
    return remainingCurrentPhaseMs();
}

const char* WashingMachine::phaseLabel() const {
    if (_refillingLevel && _phase == CyclePhase::WASHING) {
        return "Lavado (Llenando)";
    }
    if (_refillingLevel && (_phase == CyclePhase::RINSING_1 || _phase == CyclePhase::RINSING_2)) {
        return "Enjuagando (Llenando)";
    }
    if (_phase == CyclePhase::SPINNING && _spinningDrainStage) {
        return "Centrifugado (Desaguando)";
    }
    return phaseLabelOf(_phase);
}

const char* WashingMachine::activePhaseLabel() const {
    if (_refillingLevel && _phase == CyclePhase::WASHING) {
        return "Lavado (Llenando)";
    }
    if (_refillingLevel && _phase == CyclePhase::RINSING) {
        return "Enjuagando (Llenando)";
    }
    if (_phase == CyclePhase::SPINNING && _spinningDrainStage) {
        return "Centrifugado (Desaguando)";
    }
    return phaseLabelOf(activePhase());
}

const char* WashingMachine::errorLabel() const {
    switch (_error) {
        case ErrorCode::NONE:
            return "Sin error";
        case ErrorCode::FILL_TIMEOUT:
            return "Error de llenado: no se alcanzo el nivel de agua a tiempo";
        default:
            return "Error desconocido";
    }
}

uint32_t WashingMachine::phaseDurationMs(CyclePhase phase, const WashParams& p) const {
    switch (phase) {
        case CyclePhase::FILLING_WASH:
        case CyclePhase::FILLING_RINSE:
            return p.fillEstimate_ms;
        case CyclePhase::WASHING:
            return p.washTotal_ms;
        case CyclePhase::RINSING:
            return p.rinseTotal_ms;
        case CyclePhase::DRAINING_WASH:
        case CyclePhase::DRAINING_RINSE:
            return p.drainTime_ms;
        case CyclePhase::SPINNING:
            return p.spinTime_ms;
        default:
            return 0;
    }
}

uint32_t WashingMachine::remainingCurrentPhaseMs() const {
    const WashParams& p = params[static_cast<uint8_t>(_mode)];
    const CyclePhase currentPhase = activePhase();
    if (currentPhase == CyclePhase::SPINNING) {
        uint32_t nowRefSpin = (_phase == CyclePhase::PAUSED) ? _pausedAt : millis();
        uint32_t elapSpin = nowRefSpin - _phaseStart;
        if (_spinningDrainStage) {
            uint32_t drainRem = (elapSpin < p.drainTime_ms) ? (p.drainTime_ms - elapSpin) : 0;
            return drainRem + p.spinTime_ms;
        }
        return (elapSpin < p.spinTime_ms) ? (p.spinTime_ms - elapSpin) : 0;
    }

    const uint32_t duration = phaseDurationMs(currentPhase, p);
    if (duration == 0) return 0;

    uint32_t nowRef = (_phase == CyclePhase::PAUSED) ? _pausedAt : millis();
    if (_refillingLevel && (currentPhase == CyclePhase::WASHING || currentPhase == CyclePhase::RINSING) && _refillPauseStart != 0) {
        nowRef = _refillPauseStart;
    }
    const uint32_t elap = nowRef - _phaseStart;
    return (elap < duration) ? (duration - elap) : 0;
}

uint32_t WashingMachine::remainingTotalMs() const {
    if (_phase == CyclePhase::IDLE || _phase == CyclePhase::ERROR) return 0;

    const WashParams& p = params[static_cast<uint8_t>(_mode)];
    const CyclePhase currentPhase = activePhase();
    uint32_t total = remainingCurrentPhaseMs();

    // Cálculo para dos enjuagues condicionales
    bool hasSecondRinse = p.enableSecondRinse;
    if (_cycle == CycleConfig::FULL) {
        // Lavado + (1 o 2 enjuagues) + centrifugado
        switch (currentPhase) {
            case CyclePhase::FILLING_WASH:
                total += p.washTotal_ms + p.drainTime_ms;
                total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms; // Enjuague 1
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms; // Enjuague 2
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::WASHING:
                total += p.drainTime_ms;
                total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::DRAINING_WASH:
                total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::FILLING_RINSE_1:
                total += p.rinseTotal_ms + p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::RINSING_1:
                total += p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::DRAINING_RINSE_1:
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::FILLING_RINSE_2:
                total += p.rinseTotal_ms + p.drainTime_ms + p.spinTime_ms;
                break;
            case CyclePhase::RINSING_2:
                total += p.drainTime_ms + p.spinTime_ms;
                break;
            case CyclePhase::DRAINING_RINSE_2:
                total += p.spinTime_ms;
                break;
            case CyclePhase::SPINNING:
            default:
                break;
        }
    } else if (_cycle == CycleConfig::RINSE_SPIN) {
        // Solo enjuague(s) + centrifugado
        switch (currentPhase) {
            case CyclePhase::DRAINING_WASH:
            case CyclePhase::FILLING_RINSE_1:
                total += p.rinseTotal_ms + p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::RINSING_1:
                total += p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::DRAINING_RINSE_1:
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                total += p.spinTime_ms;
                break;
            case CyclePhase::FILLING_RINSE_2:
                total += p.rinseTotal_ms + p.drainTime_ms + p.spinTime_ms;
                break;
            case CyclePhase::RINSING_2:
                total += p.drainTime_ms + p.spinTime_ms;
                break;
            case CyclePhase::DRAINING_RINSE_2:
                total += p.spinTime_ms;
                break;
            case CyclePhase::SPINNING:
            default:
                break;
        }
    } else if (_cycle == CycleConfig::WASH_ONLY) {
        // Solo lavado
        switch (currentPhase) {
            case CyclePhase::FILLING_WASH:
                total += p.washTotal_ms + p.drainTime_ms;
                break;
            case CyclePhase::WASHING:
                total += p.drainTime_ms;
                break;
            case CyclePhase::DRAINING_WASH:
            default:
                break;
        }
    } else if (_cycle == CycleConfig::RINSE_ONLY) {
        // Solo enjuague(s)
        switch (currentPhase) {
            case CyclePhase::DRAINING_WASH:
            case CyclePhase::FILLING_RINSE_1:
                total += p.rinseTotal_ms + p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                break;
            case CyclePhase::RINSING_1:
                total += p.drainTime_ms;
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                break;
            case CyclePhase::DRAINING_RINSE_1:
                if (hasSecondRinse) {
                    total += p.fillEstimate_ms + p.rinseTotal_ms + p.drainTime_ms;
                }
                break;
            case CyclePhase::FILLING_RINSE_2:
                total += p.rinseTotal_ms + p.drainTime_ms;
                break;
            case CyclePhase::RINSING_2:
                total += p.drainTime_ms;
                break;
            case CyclePhase::DRAINING_RINSE_2:
            case CyclePhase::SPINNING:
            default:
                break;
        }
    } else if (_cycle == CycleConfig::SPIN_ONLY) {
        // Solo centrifugado
        if (currentPhase == CyclePhase::SPINNING) {
            // nada extra
        }
    }
    return total;
}

const char* WashingMachine::phaseLabelOf(CyclePhase phase) const {
    switch (phase) {
        case CyclePhase::IDLE:           return "Reposo";
        case CyclePhase::FILLING_WASH:   return "Lavado (Llenando)";
        case CyclePhase::WASHING:        return "Lavando";
        case CyclePhase::DRAINING_WASH:  return "Lavado (Desaguando)";
        case CyclePhase::FILLING_RINSE_1:  return "Enjuague 1 (Llenando)";
        case CyclePhase::RINSING_1:        return "Enjuagando 1";
        case CyclePhase::DRAINING_RINSE_1: return "Enjuague 1 (Desaguando)";
        case CyclePhase::FILLING_RINSE_2:  return "Enjuague 2 (Llenando)";
        case CyclePhase::RINSING_2:        return "Enjuagando 2";
        case CyclePhase::DRAINING_RINSE_2: return "Enjuague 2 (Desaguando)";
        case CyclePhase::SPINNING:       return "Centrifugando";
        case CyclePhase::PAUSED:         return "Pausado";
        case CyclePhase::ERROR:          return "ERROR";
        default:                         return "?";
    }
}

// ── Transiciones internas ─────────────────────────────────────────────────
void WashingMachine::enterPhase(CyclePhase p) {
    _phase      = p;
    _phaseStart = millis();
    _refillingLevel = false;
    _refillPauseStart = 0;
    _levelReachedAt = 0;
    _levelLostAt = 0;
    _spinningDrainStage = false;

    switch (p) {
        case CyclePhase::FILLING_WASH:
            motorStop();
            setSpin(false);
            setDrain(false);
            setFill(true);
            break;
        case CyclePhase::WASHING:
            setFill(false);
            setSpin(false);
            setDrain(false);
            _agitCount = 0;
            enterMotorPhase(MotorPhase::GOING_A);
            break;
        case CyclePhase::DRAINING_WASH:
            motorStop();
            setFill(false);
            setSpin(false);
            setDrain(true);
            break;
        case CyclePhase::FILLING_RINSE_1:
            motorStop();
            setSpin(false);
            setDrain(false);
            setFill(true);
            break;
        case CyclePhase::RINSING_1:
            setFill(false);
            setSpin(false);
            setDrain(false);
            _agitCount = 0;
            enterMotorPhase(MotorPhase::GOING_A);
            break;
        case CyclePhase::DRAINING_RINSE_1:
            motorStop();
            setFill(false);
            setSpin(false);
            setDrain(true);
            break;
        case CyclePhase::FILLING_RINSE_2:
            motorStop();
            setSpin(false);
            setDrain(false);
            setFill(true);
            // Aviso auditivo para suavizante
            extern void beepSuavisante();
            beepSuavisante();
            break;
        case CyclePhase::RINSING_2:
            setFill(false);
            setSpin(false);
            setDrain(false);
            _agitCount = 0;
            enterMotorPhase(MotorPhase::GOING_A);
            break;
        case CyclePhase::DRAINING_RINSE_2:
            motorStop();
            setFill(false);
            setSpin(false);
            setDrain(true);
            break;
        case CyclePhase::SPINNING:
            motorStop();
            setFill(false);
            setSpin(false);
            setDrain(true);
            _spinningDrainStage = true;
            break;
        case CyclePhase::IDLE:
        case CyclePhase::ERROR:
            motorStop();
            setFill(false);
            setSpin(false);
            setDrain(false);
            break;
        default: break;
    }
}

void WashingMachine::enterMotorPhase(MotorPhase mp) {
    _motorPhase = mp;
    _motorStart = millis();

    switch (mp) {
        case MotorPhase::GOING_A:
            applyMotorDirection(false, true);
            break;
        case MotorPhase::GOING_B:
            applyMotorDirection(false, true);
            break;
        case MotorPhase::PAUSING_A:
        case MotorPhase::PAUSING_B:
        case MotorPhase::STOPPED:
            applyMotorDirection(false, false);
            break;
    }
}

void WashingMachine::applyMotorDirection(bool antiClockwise, bool enableMotor) {
    // Conmutación segura: primero sin alimentación, luego cambia sentido y por último energiza.
    const bool dirRelaysOn = enableMotor && antiClockwise;
    digitalWrite(Pinout::RELAY_MOTOR_ON, RELAY_OFF);

    if (!enableMotor) {
        digitalWrite(Pinout::RELAY_DIR_A, RELAY_OFF);
        digitalWrite(Pinout::RELAY_DIR_B, RELAY_OFF);
        return;
    }

    if (antiClockwise) {
        digitalWrite(Pinout::RELAY_DIR_A, RELAY_ON);
        digitalWrite(Pinout::RELAY_DIR_B, RELAY_ON);
    } else {
        digitalWrite(Pinout::RELAY_DIR_A, RELAY_OFF);
        digitalWrite(Pinout::RELAY_DIR_B, RELAY_OFF);
    }

    digitalWrite(Pinout::RELAY_MOTOR_ON, RELAY_ON);
}

void WashingMachine::motorStop() {
    applyMotorDirection(false, false);
    _motorPhase = MotorPhase::STOPPED;
}

void WashingMachine::setFill(bool on) {
    if (!on) {
        digitalWrite(Pinout::RELAY_VALVULA_FRIA, RELAY_OFF);
        digitalWrite(Pinout::RELAY_VALVULA_CALIENTE, RELAY_OFF);
        return;
    }

    switch (_waterMode) {
        case WaterFillMode::COLD:
            digitalWrite(Pinout::RELAY_VALVULA_FRIA, RELAY_ON);
            digitalWrite(Pinout::RELAY_VALVULA_CALIENTE, RELAY_OFF);
            break;
        case WaterFillMode::HOT:
            digitalWrite(Pinout::RELAY_VALVULA_FRIA, RELAY_OFF);
            digitalWrite(Pinout::RELAY_VALVULA_CALIENTE, RELAY_ON);
            break;
        case WaterFillMode::BOTH:
        default:
            digitalWrite(Pinout::RELAY_VALVULA_FRIA, RELAY_ON);
            digitalWrite(Pinout::RELAY_VALVULA_CALIENTE, RELAY_ON);
            break;
    }
}

void WashingMachine::setSpin(bool on) {
    // Centrifugado usa el mismo motor en configuración antihoraria.
    applyMotorDirection(true, on);
}

void WashingMachine::setDrain(bool on) {
    digitalWrite(Pinout::RELAY_DRAIN, on ? RELAY_ON : RELAY_OFF);
}

// Determina qué fase sigue después de un desagüe según el ciclo seleccionado
CyclePhase WashingMachine::afterDrain() const {
    if (_phase == CyclePhase::DRAINING_WASH) {
        return (_cycle == CycleConfig::FULL) ? CyclePhase::FILLING_RINSE : CyclePhase::IDLE;
    }
    // DRAINING_RINSE
    if (_cycle == CycleConfig::SPIN_ONLY) return CyclePhase::IDLE;
    if (_rinseCount < 2) return CyclePhase::FILLING_RINSE;
    return CyclePhase::IDLE;
}

// ── Actualizaciones de fase ───────────────────────────────────────────────
void WashingMachine::updateFilling() {
    const uint32_t now = millis();

    // LOW = lleno (sensor NA con INPUT_PULLUP, cortocircuito a GND)
    if (digitalRead(Pinout::SENSOR_NIVEL) == LOW) {
        if (_levelReachedAt == 0) {
            _levelReachedAt = now;
        }

        if (now - _levelReachedAt >= LEVEL_STABLE_MS) {
            setFill(false);
            _levelReachedAt = 0;
            enterPhase((_phase == CyclePhase::FILLING_WASH) ? CyclePhase::WASHING
                                                            : CyclePhase::RINSING);
        }
        return;
    }

    _levelReachedAt = 0;

    const WashParams& p = params[static_cast<uint8_t>(_mode)];
    if (now - _phaseStart >= p.fillTimeout_ms) {
        setFill(false);
        _error = ErrorCode::FILL_TIMEOUT;
        enterPhase(CyclePhase::ERROR);
    }
}

void WashingMachine::updateAgitating() {
    const WashParams& p  = params[static_cast<uint8_t>(_mode)];
    bool     isWash      = (_phase == CyclePhase::WASHING);
    uint32_t agitTime    = isWash ? p.agitTime_ms     : p.rinseAgitTime_ms;
    uint32_t phaseTotal  = isWash ? p.washTotal_ms    : ((p.rinseTotal_ms > 1) ? (p.rinseTotal_ms / 2) : p.rinseTotal_ms);
    uint32_t now         = millis();

    // En lavado y enjuague, el nivel se valida continuamente.
    // Si cae, se detiene la agitación de inmediato, se rellena y el tiempo de la fase queda congelado.
    {
        bool isFull = (digitalRead(Pinout::SENSOR_NIVEL) == LOW);
        if (!isFull) {
            _levelReachedAt = 0;
            if (_levelLostAt == 0) {
                _levelLostAt = now;
                return;
            }
            if (now - _levelLostAt < LEVEL_LOST_DEBOUNCE_MS) {
                return;
            }
            if (!_refillingLevel) {
                _refillingLevel = true;
                _refillStart = now;
                _refillPauseStart = now;
                _levelReachedAt = 0;
                motorStop();
                setFill(true);
            }
            if (now - _refillStart >= p.fillTimeout_ms) {
                _refillingLevel = false;
                _refillPauseStart = 0;
                _levelReachedAt = 0;
                setFill(false);
                _error = ErrorCode::FILL_TIMEOUT;
                enterPhase(CyclePhase::ERROR);
            }
            return;
        }

        _levelLostAt = 0;

        if (_refillingLevel) {
            if (_levelReachedAt == 0) {
                _levelReachedAt = now;
            }
            if (now - _levelReachedAt < LEVEL_STABLE_MS) {
                return;
            }

            _refillingLevel = false;
            setFill(false);
            if (_refillPauseStart != 0) {
                _phaseStart += (now - _refillPauseStart);
            }
            _refillPauseStart = 0;
            _levelReachedAt = 0;
            // Reanuda agitacion inmediatamente tras el nivel estable.
            enterMotorPhase(MotorPhase::GOING_A);
            return;
        }

        _levelReachedAt = 0;
    }

    if (now - _phaseStart >= phaseTotal) {
        enterMotorPhase(MotorPhase::STOPPED);
        if (isWash) {
            enterPhase(CyclePhase::DRAINING_WASH);
        } else {
            if (_phase == CyclePhase::RINSING_1) {
                enterPhase(CyclePhase::DRAINING_RINSE_1);
            } else if (_phase == CyclePhase::RINSING_2) {
                enterPhase(CyclePhase::DRAINING_RINSE_2);
            } else {
                enterPhase(CyclePhase::IDLE);
            }
        }
        return;
    }

    switch (_motorPhase) {
        case MotorPhase::GOING_A:
        case MotorPhase::GOING_B:
            if (now - _motorStart >= agitTime)
                enterMotorPhase(MotorPhase::PAUSING_A);
            break;

        case MotorPhase::PAUSING_A:
        case MotorPhase::PAUSING_B:
            if (now - _motorStart >= p.agitPause_ms)
                enterMotorPhase(MotorPhase::GOING_A);
            break;

        default: break;
    }
}

void WashingMachine::updateDraining() {
    const WashParams& p = params[static_cast<uint8_t>(_mode)];
    if (millis() - _phaseStart >= p.drainTime_ms) {
        setDrain(false);
        // Transición según la fase de drenaje
        if (_phase == CyclePhase::DRAINING_WASH) {
            enterPhase(CyclePhase::FILLING_RINSE_1);
        } else if (_phase == CyclePhase::DRAINING_RINSE_1) {
            if (p.enableSecondRinse) {
                enterPhase(CyclePhase::FILLING_RINSE_2);
            } else {
                enterPhase(CyclePhase::SPINNING);
            }
        } else if (_phase == CyclePhase::DRAINING_RINSE_2) {
            enterPhase(CyclePhase::SPINNING);
        } else {
            enterPhase(CyclePhase::IDLE);
        }
    }
}

void WashingMachine::updateSpinning() {
    const WashParams& p = params[static_cast<uint8_t>(_mode)];
    if (_spinningDrainStage) {
        if (millis() - _phaseStart >= p.drainTime_ms) {
            _spinningDrainStage = false;
            _phaseStart = millis();
            setSpin(true);
            setDrain(true);
        }
        return;
    }

    if (millis() - _phaseStart >= p.spinTime_ms) {
        setSpin(false);
        setDrain(false);
        enterPhase(CyclePhase::IDLE);
    }
}
