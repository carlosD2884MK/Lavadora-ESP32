#pragma once

namespace Pinout {

// ── Relés (módulo de 8 relés, activo en HIGH) ─────────────────────────────
// Lavado con relés dedicados para motor, centrifugado, llenado y desagüe.
constexpr int RELAY_MOTOR_ON         = 32;  // Encendido general de motor
constexpr int RELAY_DIR_A            = 33;  // Relé auxiliar de inversión A
constexpr int RELAY_DIR_B            = 25;  // Relé auxiliar de inversión B
constexpr int RELAY_VALVULA_FRIA     = 26;  // Entrada agua fría
constexpr int RELAY_VALVULA_CALIENTE = 27;  // Entrada agua caliente
constexpr int RELAY_DRAIN            = 14;  // Valvula de desagüe
constexpr int RELAY_7         = 12;  // Libre
constexpr int RELAY_8         = 13;  // Libre

// ── LEDs indicadores ───────────────────────────────────────────────────────
constexpr int LED_LAVADO       =  4;  // Fase lavado activa
constexpr int LED_ENJUAGUE     =  5;  // Fase enjuague activa
constexpr int LED_CENTRIFUGADO = 18;  // Fase centrifugado activa
constexpr int LED_ERROR        = 19;  // Alarma / error
constexpr int LED_AGUA_FRIA    =  0;  // LED azul seleccion agua fria
constexpr int LED_AGUA_CALIENTE = 2;  // LED rojo seleccion agua caliente

// ── Pulsadores (INPUT_PULLUP, activo en LOW) ───────────────────────────────
constexpr int BTN_CICLO  = 15;  // Selecciona ciclo
constexpr int BTN_MODO   = 16;  // Selecciona modo de lavado
constexpr int BTN_INICIO = 17;  // Inicio / Cancelar

// ── Sensor de nivel (NA con INPUT_PULLUP) ────────────────────────────────
// Sensor conectado entre GPIO y GND.
// Abierto (vacío) = HIGH (pull-up activo) | Cerrado (lleno) = LOW.
constexpr int SENSOR_NIVEL = 23;

// ── OLED I2C (opcional, compilar con -D USE_OLED) ─────────────────────────
constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

// ── Buzzer ────────────────────────────────────────────────────────────────
constexpr int BUZZER_PIN = 13;

// ── UART0 (USB-TTL) ────────────────────────────────────────────────────────
constexpr int SERIAL_TX = 1;
constexpr int SERIAL_RX = 3;

}  // namespace Pinout

// ── Lógica del módulo de relés (activo en HIGH) ───────────────────────────
// Definidos fuera del namespace para uso directo en digitalWrite().
static constexpr int RELAY_ON  = HIGH;
static constexpr int RELAY_OFF = LOW;