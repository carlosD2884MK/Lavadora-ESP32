# Documentación de Cambios y Detalles Técnicos

## Segundo Enjuague (enableSecondRinse)

### Resumen
- Se agregó un control visual (checkbox) llamado "Segundo enjuague" en la web embebida, editable por modo.
- El valor se guarda y persiste por modo en la configuración (NVS), editable desde la web y guardado vía API.
- El valor por defecto de fábrica es activo (true).
- El usuario puede activar/desactivar el segundo enjuague por modo desde la web.
- La ayuda contextual explica que los tiempos de enjuague no cambian, solo se repite el ciclo si está activo.

### Backend
- `WashParams` incluye el campo `enableSecondRinse`.
- `DEFAULT_PARAMS` lo define como true.
- Migración automática de configuraciones previas.
- Endpoints `/api/config` y `/api/save` exponen y guardan el valor por modo.
- Persistencia en NVS garantizada por `savePersistentConfig` y `loadPersistentConfig`.

### Frontend
- Checkbox agregado tras el campo de tiempo de enjuague total.
- El JS sincroniza el valor con el backend: lo carga, lo muestra y lo guarda por modo.
- El valor se envía y persiste correctamente al guardar parámetros.
- Ayuda textual visible junto al checkbox.

### Máquina de Estados
- Enum `CyclePhase` extendido para soportar dos enjuagues.
- Lógica de transición robusta, sin referencias a fases antiguas.
- Cálculo de tiempo total y labels adaptados a uno o dos enjuagues.
- Aviso auditivo implementado al iniciar el segundo enjuague.

### Compatibilidad y Migración
- Migración automática de configuraciones previas.
- Compatibilidad total con versiones anteriores.

### Validación
- Validado por compilación, pruebas de web y persistencia.
- El valor de enableSecondRinse se refleja en la UI, la lógica y la configuración interna.

---

## Otros detalles
- Separación de tiempos de llenado y timeout.
- Refactorización de máquina de estados.
- Mejoras de robustez y experiencia de usuario.
