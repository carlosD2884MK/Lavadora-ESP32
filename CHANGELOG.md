# CHANGELOG

## 2026-05-21

### Integración de control de Segundo Enjuague (enableSecondRinse)
- Checkbox "Segundo enjuague" agregado en la web embebida, editable por modo, con ayuda contextual.
- El valor se expone y persiste por modo en la configuración (NVS), editable desde la web y guardado vía API.
- Valor por defecto de fábrica: activo (true).
- Migración automática de configuración previa, manteniendo compatibilidad.
- Lógica de máquina de estados y cálculo de tiempo total robustos para uno o dos enjuagues.
- Eliminación de referencias a afterDrain y limpieza de lógica de fases.
- Aviso auditivo implementado al iniciar el segundo enjuague.
- Web embebida: ayuda textual y control visual integrados, sincronización frontend-backend completa.
- Validación de persistencia y reflejo inmediato en la UI y lógica interna.

---

### Otros cambios recientes
- Separación de tiempos de llenado y timeout.
- Refactorización de máquina de estados y migración de configuración versionada.
- Mejoras de robustez y experiencia de usuario.
