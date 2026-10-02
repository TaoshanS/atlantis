# Decisiones cerradas (registro)

Fecha de referencia: 2026-10-01. Detalle completo en `AGENTS.md`.

| Tema | Decisión |
|---|---|
| Enfoque del port | Reescritura completa C++17 + SDL3 (no Ruffle) |
| Plataformas | Escritorio + móvil |
| Gráficos | Lanczos por defecto; selector Original<->Lanczos; UI/carteles procedurales |
| Pantallas anchas | Extender escena y reubicar UI |
| Audio | Sinc HQ a 48 kHz, sin procesado extra |
| Idiomas | Inglés + castellano (es-ES), selector, autodetección en primera ejecución |
| Big Fish Games | Quitar solo su splash (frames 140-200 de titlescreen sprite 67) |
| Guardado | Ranuras por perfil + 1 instantánea de nivel por ranura (límite de turno) |
| Nube | Al final; componente C99 reutilizable, backend autoalojado, cifrado en cliente |
| Assets | El repo solo tiene código; los assets se extraen de los originales del usuario |
