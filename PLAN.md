# Plan de desarrollo — Lavadora Jesús (ESP32 + 8 relés)

## Hardware disponible

| Componente 			| Cantidad 	| Notas 												|
|-----------------------|-----------|-------------------------------------------------------|
| ESP32 DevKit 			| 	1 		| Framework Arduino / PlatformIO 						|
| Módulo 8 relés 		| 	1 		| Activo en LOW (típico) 								|
| Motor 5 cables 		| 	1 		| Naranja=neutro, Rojo, Blanco-violeta, Amarillo, Azul 	|
| Bomba centrifugado 	| 	1 		| 110 V CA 												|
| Válvula de agua 		| 	1 		| 110 V CA (llenado/desagüe) 							|
| Sensor de nivel 		| 	1 		| Normalmente Abierto (NA) 								|
| LEDs 					| 	4 		| Indicadores de estado 								|
| Pulsadores 			| 	3 		| Ciclo, Modo, Inicio/Cancelar 							|
| Pantalla OLED I2C 	| 	1 		| SSD1306 128×64, opcional 								|

---

## ¿TRIAC o relés para el motor?

**Relés son suficientes.** El motor es de inducción monofásico con devanado auxiliar.  
El control de giro se logra intercambiando los terminales del devanado auxiliar con 2 relés:

```
Sentido A (lavar horario):   	RELAY_1=ON, RELAY_2=OFF
Sentido B (lavar antihorario): 	RELAY_1=OFF, RELAY_2=ON
Motor apagado:               	RELAY_1=OFF, RELAY_2=OFF
```

> **CRÍTICO:** siempre apagar ambos relés y esperar ≥500 ms antes de invertir el sentido.  
> Nunca activar RELAY_1 y RELAY_2 simultáneamente → cortocircuito de devanados.

El control de **intensidad de lavado** (modos suave/normal/fuerte/muy fuerte) se logra variando los **tiempos de giro y pausa**, no la velocidad → no se necesita TRIAC.

---

## Asignación de pines

### Relés
| Relé 	  | GPIO | Función 							|
|---	  |---	 |---								|
| RELAY_1 | 32 	 | Motor — sentido A (horario) 		|
| RELAY_2 | 33 	 | Motor — sentido B (antihorario)  |
| RELAY_3 | 25 	 | Bomba de centrifugado (110 V) 	|
| RELAY_4 | 26 	 | Válvula de agua (110 V)			|
| RELAY_5 | 27 	 | Libre 							|
| RELAY_6 | 14 	 | Libre 							|
| RELAY_7 | 12 	 | Libre 							|
| RELAY_8 | 13 	 | Libre 							|

### Entradas / Salidas
| Señal 			| GPIO 	| Tipo 					| Notas 					|
|---				|---	|---					|---						|
| LED_LAVADO 		| 4 	| Salida 				| Fase lavado activa 		|
| LED_ENJUAGUE 		| 5 	| Salida 				| Fase enjuague activa 		|
| LED_CENTRIFUGADO 	| 18 	| Salida 				| Fase centrifugado activa 	|
| LED_ERROR 		| 19 	| Salida 				| Alarma / error 			|
| BTN_CICLO 		| 15 	| Entrada pull-up 		| Selecciona ciclo 			|
| BTN_MODO 			| 16 	| Entrada pull-up 		| Selecciona modo 			|
| BTN_INICIO 		| 17 	| Entrada pull-up 		| Inicio / Cancelar 		|
| SENSOR_NIVEL 		| 34 	| Entrada (solo input) 	| NA → HIGH=lleno 			|
| OLED_SDA 			| 21 	| I2C 					| Opcional 					|
| OLED_SCL 			| 22 	| I2C 					| Opcional 					|

---

## Modos de lavado

| Modo 			| T_giro (s) 	| T_pausa (ms) 	|
|---			|---			|---			|
| Muy fuerte 	| 12 			| 300 			|
| Fuerte 		| 8 			| 300			|
| Normal 		| 5 			| 400 			|
| Suave 		| 3 			| 600 			|

> Los tiempos son configurables desde la página cautiva y se guardan en NVS (flash).

---

## Ciclos disponibles

| Selección 						| Fases ejecutadas 															|
|---								|---																		|
| Lavado + Enjuague + Centrifugado 	| Llenar → Lavar → Desaguar → Llenar → Enjuagar → Desaguar → Centrifugar 	|
| Enjuague + Centrifugado 			| Llenar → Enjuagar → Desaguar → Centrifugar 								|
| Solo centrifugado 				| Centrifugar 																|
| Solo Lavado 						| Llenar → Lavar → Desaguar 												|
| Solo Enjuague 					| Llenar → Enjuagar → Desaguar 												|

---

## Lista de pruebas (Fase 1 — comisionamiento)

### Bloque A — Salidas digitales simples
- [ ] A1: Cada LED enciende y apaga individualmente
- [ ] A2: Cada relé activa/desactiva (verificar con multímetro o LED de módulo)

### Bloque B — Entradas
- [ ] B1: Cada pulsador reporta correctamente por Serial (con debounce)
- [ ] B2: Sensor de nivel reporta NA (LOW) y cerrado (HIGH) correctamente

### Bloque C — Motor
- [ ] C1: Sentido A activa solo RELAY_1, motor gira en un sentido
- [ ] C2: Sentido B activa solo RELAY_2, motor gira en sentido contrario
- [ ] C3: Cambio de sentido con pausa de 500 ms — sin chispazo ni traba
- [ ] C4: Secuencia de agitación automática (N ciclos A/B)

### Bloque D — Cargas 110 V
- [ ] D1: Válvula de agua abre/cierra con RELAY_4
- [ ] D2: Bomba centrifugado arranca/para con RELAY_3

### Bloque E — Control de nivel
- [ ] E1: Llenado automático (abrir válvula → esperar sensor → cerrar válvula)
- [ ] E2: Timeout de llenado (alarma si el sensor no activa en X segundos)

### Bloque F — WiFi / Página cautiva
- [ ] F1: ESP32 levanta AP "Lavadora_Jesús"
- [ ] F2: Captura cualquier petición DNS → redirige a 192.168.4.1
- [ ] F3: Página muestra ciclo, modo y tiempo restante en tiempo real
- [ ] F4: Botones Iniciar/Detener funcionan desde la página
- [ ] F5: Editor de tiempos guarda en NVS y persiste al reiniciar

### Bloque G — OLED (opcional)
- [ ] G1: Pantalla muestra estado actual (ciclo, modo, tiempo)
- [ ] G2: Animación de fase en curso

---

## Fases de desarrollo

### Fase 1 — Pruebas unitarias ✅ (empezando ahora)
- `pinout.h` completo con todos los pines
- `main.cpp` con menú serial interactivo para probar cada componente
- Verificar bloques A, B, C, D, E

### Fase 2 — Máquina de estados del ciclo
- `enum CyclePhase` (IDLE, FILL, WASH, DRAIN, RINSE, SPIN, ERROR)
- `enum WashMode` (SOFT, NORMAL, STRONG, XSTRONG)
- `enum CycleConfig` (FULL, RINSE_SPIN, SPIN_ONLY, ...)
- Lógica de transiciones con tiempos configurables
- Seguridad: mutex motor, timeout de llenado, apagado de emergencia

### Fase 3 — Persistencia (NVS)
- Guardar/cargar tiempos de cada modo
- Guardar última configuración de ciclo y modo
- Librería: `Preferences.h` (incluida en ESP32 Arduino)

### Fase 4 — WiFi AP + Página cautiva
- AP mode: SSID "Lavadora_Jesús", sin contraseña (o con clave configurable)
- DNS cautivo redirige todo a 192.168.4.1
- Servidor web con `ESPAsyncWebServer`
- Página HTML/CSS/JS en SPIFFS/LittleFS
- API REST JSON para estado y control
- WebSocket para actualizaciones en tiempo real

### Fase 5 — OLED SSD1306 (opcional, `#define USE_OLED`)
- Librería: `Adafruit SSD1306` + `Adafruit GFX`
- Pantalla principal: fase, modo, tiempo restante
- Pantalla de error con código

---

## Librerías requeridas

```ini
lib_deps =
    me-no-dev/ESPAsyncWebServer @ ^1.2.3
    me-no-dev/AsyncTCP @ ^1.1.1
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.9
```

---

## Estado del proyecto

| Fase | Estado |
|---|---|
| Fase 1 — Pruebas unitarias | 🔄 En progreso |
| Fase 2 — Máquina de estados | ⏳ Pendiente |
| Fase 3 — NVS | ⏳ Pendiente |
| Fase 4 — Página cautiva | ⏳ Pendiente |
| Fase 5 — OLED | ⏳ Pendiente |
