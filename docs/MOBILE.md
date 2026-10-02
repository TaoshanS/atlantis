# Móvil (iOS / Android)

Guía de compilación de iOS (preset `ios`, Xcode): [ios.md](ios.md). Estas son las notas de diseño.

Estado: el código del juego es el mismo que en escritorio (SDL3). Lo que ya está resuelto en el código:

- **Assets**: todo se lee a través de `src/core/vfs.*`; en móvil se usa un único `game.pak` que el propio usuario genera
  con `tools/pack/make_pack.py <extracted> <maps> game.pak` a partir de sus archivos originales. El pack no se distribuye.
- **Dónde busca el pack** (`src/app/main.cpp`): junto al ejecutable / bundle, en el directorio de preferencias
  (`SDL_GetPrefPath("sbso","atlantis")`), en Documentos del usuario (iOS: visible desde la app Archivos si el Info.plist
  tiene `UIFileSharingEnabled` y `LSSupportsOpeningDocumentsInPlace`) y en el almacenamiento externo de la app (Android,
  `Android/data/<paquete>/files/`, accesible por USB). Si no lo encuentra muestra un aviso con la ruta.
- **Guardados** en `SDL_GetPrefPath` (se puede cambiar con `--saves`).
- **Entrada**: SDL no sintetiza ratón desde el táctil (hints a 0); solo el primer dedo mueve el puntero, el hover se emula
  (mover antes de pulsar) y se limpia al soltar. `WILL_ENTER_BACKGROUND`/`TERMINATING` guardan la instantánea del nivel.
- **Pantallas anchas**: la batalla usa el ancho extra (`Layout`); el resto se centra en 4:3 y los márgenes se rellenan con
  un desenfoque oscurecido del fotograma. Insets de zona segura: `compute_layout(w, h, Insets)` los admite; falta leerlos de SDL
  (`SDL_GetWindowSafeArea`) y pasarlos desde `App::update_layout` en el dispositivo.

## Pendiente (necesita dispositivo/SDK, no se puede probar en la nube)
- Proyecto iOS: `cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS ...` con SDL3 compilado para iOS; Info.plist (orientación horizontal,
  claves de compartir archivos), icono y launch screen. Metal lo gestiona el renderer de SDL.
- Proyecto Android: usar `android-project` de SDL3 (Gradle + NDK), `SDL_main` ya está incluido; empaquetar `libmain.so` desde el
  objetivo `sbso`. El pack NO va dentro del APK (límite de tamaño); se copia al almacenamiento de la app.
- Pasar `SDL_GetWindowSafeArea` a `compute_layout` y reubicar HUD/menús con `anchor_x`.
- Medir rendimiento y memoria en dispositivo (el renderer cachea texturas Lanczos al cargar).
