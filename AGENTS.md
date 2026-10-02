# AGENTS.md — SpongeBob Atlantis SquareOff (preservación, port nativo, localización)

Guía para cualquier agente (o persona) que retome este proyecto. Léela entera antes de tocar nada.
El detalle de diseño está en `docs/PORT_PLAN.md`; las decisiones cerradas, en `docs/DECISIONS.md`.

## Objetivo
Preservar y revivir **SpongeBob Atlantis SquareOff** (juego Flash AS3 de 2008, WildGames/Armadillo, 640x480):
1. Recuperación 1:1 del original (HECHO).
2. **Reescritura completa en C++17 + SDL3** para escritorio y móvil, mayor resolución.
3. Localización: inglés (original) + castellano de España, con selector de idioma.
4. Mejoras que no cambian el juego: audio HQ, ranuras de guardado, instantáneas a mitad de nivel, nube (al final).

## Reglas duras
- **Los originales no se modifican**: `SpongeBob Atlantis SquareOff - WildGames/` y `recovered/sbso_main_candidate.swf` (copias locales del usuario,
  ya **no versionadas**) son de solo lectura. Los parches se hacen sobre copias.
- **El repositorio contiene solo código y herramientas.** Los assets (arte, audio, XML) son de Nickelodeon: se extraen con el extractor a partir de los archivos originales del usuario y **no se commitean ni redistribuyen** (el directorio de salida va en `.gitignore`).
- No borres la VM de Windows XP del usuario ni nada de su entorno local sin preguntar.
- No crees PR salvo petición explícita. Desarrolla en la rama indicada por la sesión.
- Las decisiones cerradas no se reabren; si crees que una está mal, propón y espera respuesta.
- Responde al usuario en español.

## Estado del repositorio
- Estructura final (como dusklight): `cmake/` (SDL3, GameData, Packaging), `platforms/<so>/`, `game/` (archivos del usuario, ignorado salvo
  su README), `docs/` (building/dumping/ios en inglés + notas en castellano), `.github/workflows/build.yml`, `CMakePresets.json`.
  Los datos se generan al compilar: target `game_data` -> `tools/game_data.py` (extrae si hace falta, `game.pak`, iconos).
- Locales, fuera de git desde esta sesión (material de Nickelodeon; el historial antiguo aún los contiene):
- `SpongeBob Atlantis SquareOff - WildGames/`: juego original (data/*.swf, maps/, etc.). CMake lo usa si no hay `game/`.
- `recovered/sbso_main_candidate.swf`: SWF principal recuperado de memoria (FWS v9, sha256 0185333a…). Para otros usuarios: `tools/dump/sbso_dump.c`
  (volcador Win32; compilado por la CI, **sin probar en Windows**) y `docs/dumping.md`.
- `recovered/source/scripts/`: 124 .as descompilados (JPEXS ffdec, ~23k líneas, 1 glitch de decompilación). Es la referencia de la lógica.
- `recovered/localization/strings_en.{csv,json}`, `recovered/comparison/` (pruebas de escalado), `recovered/audio_ab/` (A/B de audio).
- Zinc (Multidmedia) y Armadillo: solo 5 puntos de llamada a Zinc (sbso2.as ~55, 246-247, 313; GameCursor.as ~26). En el port no existen.

## Hechos técnicos del juego
- AS3, SWF v9, 640x480. Niveles de 30x20 tiles (900x600) con scroll; tiles de 32x32 sobre rejilla de 30 px (1 px solapado, bordes translúcidos).
- ~6.8k bitmaps (64 Mpx), casi sin vector real; sin filtros ni blend modes; solo 15 máscaras.
- Hover (ROLL_OVER) usado en 19 archivos: en táctil hay que sustituirlo.
- Audio: 182 sonidos MP3 mono (sound_library.swf 154 SFX, music_library.swf 28 pistas); 151 a 11,025 kHz/16 kbps. Sin clipping.
- Texto: `maps/SBSO2_CONFIG.xml` (241 líneas de diálogo, cartas, cinturones, fases), ~55 cadenas en AS, `instructions.swf` (69 textos), texto incrustado en bitmaps (botones, logo, carteles de mapa/tiles).
- Fuentes: Unibody 8 Black tiene glifos españoles; TikiMagic y TikiIsland NO (acentos/ñ): hace falta fuente de respaldo.
- Perfiles originales: SharedObject "SBSO2Profiles", 3 perfiles, nombre ≤15 chars, autoguardado por nivel; ajustes mezclados en el perfil.
- Estado de partida disperso en GameLevel, BattleManager, TurnManager, Unit, AIUnit, Map, HUD, CardCarousel (~115 variables); Math.random en ~15 sitios.
- Ruffle nightly (macOS) ejecuta el SWF con data/ y maps/ al lado; sirve de referencia.

## Decisiones cerradas (resumen; ver docs/DECISIONS.md)
- Port: reescritura completa C++17 + SDL3 (SDL_GPU: Metal/Vulkan/D3D12). NO envolver Ruffle.
- Gráficos: Lanczos por defecto + selector Original<->Lanczos. Mundo renderizado a resolución lógica original y remuestreado; UI y texto a resolución nativa. Alfa premultiplicado, sin unsharp, sin grano. UI y carteles: reconstrucción procedural/vectorial con la fuente real.
- Pantallas anchas (iPhone ~2622x1206): **extender la escena y reubicar la UI** (no barras).
- Audio: remuestreo sinc HQ a 48 kHz, sin más procesado (sin denoise/exciter/IA/selector).
- Localización: en + es-ES, selector en Opciones y en primera ejecución (autodetección del sistema). Ajuste global, no por perfil. Tabla de cadenas por clave; XML duplicado por idioma con IDs idénticos. Glosario (doblaje de España, tuteo): Señor Cangrejo, Tritón Man, Crustáceo Crujiente, Cubo de Cebo, Amigo Burbuja, Burbuja Sucia, la Atlántida; logo "SquareOff" se queda en inglés. Términos dudosos: confirmar con el usuario.
- Big Fish Games: quitar SOLO su splash (titlescreen.swf, sprite 67, frames 140-200). Mantener Nickelodeon, This Is Pop y créditos de desarrolladores (Pop & Company, Tiny Mantis, iBeta). Revisar textos "Big Fish" en credits.swf/credit.swf antes de decidir. Uninstall.exe y .bfg no entran en el port.
- Guardado: perfiles con ranuras manuales (límite de perfiles mayor que 3); **una instantánea de nivel por ranura**, tomada en límites de turno (inicio de turno del jugador + al pasar a segundo plano/salir, tras terminar la acción en curso), nunca a mitad de animación; PRNG con semilla cuyo estado se guarda; estado de juego como datos serializables separados del render; instantánea versionada con id de nivel y hash del XML; se borra al ganar/perder. Ajustes (volumen, pantalla completa, idioma) separados del perfil. Importación de .sol originales deseable. Instantáneas: solo en el port.
- Nube: **al final del proyecto**. Backend neutral autoalojado, componente reutilizable C99 con ABI C estable (usable desde C++/Swift/Kotlin/Rust); modelo game_id/perfil/ranura/blob + revisión; protocolo HTTP clave-valor con ETag/If-Match (compatible WebDAV); cifrado en cliente; política de conflictos aportada por el juego. Abierto: nº de ranuras, auth, librería HTTP por plataforma.

## Plan por fases
1. Extractor de assets (offline): bitmaps->atlas, MovieClips->hojas de sprites + listas de frames, máscaras, sonidos->WAV 48 kHz, XML/strings; produce manifest + paquete de datos. El juego nunca lee SWF.
2. Motor SDL3: mundo lógico 640x480, remuestreo, escena extendida, UI nativa, audio, entrada ratón/táctil, escenas.
3. Lógica de juego como datos puros + PRNG con semilla (Map, Unit, AIUnit, TurnManager, BattleManager, cartas, cinturones).
4. Pantallas y minijuegos.
5. i18n, guardado/ranuras/instantáneas, selector de gráficos.
6. Plataformas: macOS, iOS, Windows/Linux, Android.
7. Nube.

## Validación
Modo headless determinista: misma semilla y entradas -> comparar estado de turnos/daños con trazas de la versión Flash en Ruffle. Probar contra el original, no a ojo.

## Entorno
- Sesiones en la nube: no tienen ffdec ni el entorno Mac del usuario (GitHub puede dar 403 desde el proxy). El usuario trabaja en macOS (QEMU con XP SP3 para el dump original).
- Antes de instalar paquetes (pip, brew, etc.) pregunta si hace falta; el usuario ha rechazado instalaciones sin avisar (en la nube dio libertad total; en local, pregunta).
- Disco del usuario casi lleno (~7 GB libres): evita artefactos grandes.

## Convenciones
C++17, CMake + Ninja, código que imita el estilo del entorno; sin identificadores de modelo en commits, código ni docs. Commits claros; push a la rama de la sesión.

## Estado actual (actualizar al terminar cada sesión)
Fase 1 (extractor) y gran parte de las fases 2-3 están hechas y probadas:
- `tools/extract/`: SWF -> PNG, MP3->WAV 48 kHz (`audio.py`), fuentes TTF (`fonts.py`), formas/sprites/botones/textos,
  scripts de fotograma (`framescripts.py` + `avm2.py`, los 612 recuperados) y cabecera (tamaño de escena, fps).
  `python3 tools/extract/extract.py "<juego>" extracted` (necesita `pip install pillow numpy fonttools`).
- `src/core`: RNG PCG32, remuestreo Lanczos, layout de pantalla extendida (tests: `test_core`).
- `src/game`: base de datos de cartas/enemigos, niveles, tablero (rangos, A*), `Sim` completa (turnos, IA, batalla,
  misiones, botín) y snapshots versionados (tests: `test_game`, `test_sim`). Notas en `docs/LOGIC_NOTES.md`.
- `src/save`: perfiles, ranuras manuales + autoguardado, snapshot por ranura, escritura atómica con `.bak` (`test_save`).
- `src/engine`: `Library` (personajes del SWF), `MovieClip` (líneas de tiempo, scripts de fotograma, máscaras),
  `SoftRenderer` (CPU), `SdlRenderer` (SDL3, texturas premultiplicadas, Lanczos al cargar), `LevelView` (nivel + unidades).
  Verificado visualmente: portada (menú y splash de Big Fish en frames 140-200 del sprite 67) y niveles completos.
- `src/save/progression.*`: tabla de desbloqueos (`GameInfo.buildUnlockSequence`) y lógica de mapa (`LevelSelectScreenMap`) sobre `Progress`.
- `src/app`: `App` (ventana SDL3, bucle a tasa fija, ratón/táctil, texto, audio, perfiles activos, autoguardado, instantánea de nivel),
  `flow.*` (qué pantalla sigue a cuál, música, autoguardado; port de `sbso2.as`), `AssetStore`. Pantallas portadas con el arte original:
  `TitleScreen`, `ProfileScreen` (+ `SavesScreen`, ranuras manuales: añadido nuevo), `OptionsScreen`, `LevelSelectScreen` (mapa, banderas,
  autobús, pergamino, desbloqueos animados), `ChestScreen` (cofre: cartas/cinturones/monedas/banderas, mazo, minimapa de rango),
  `BattleScreen` (mapa, cámara, intro con scroll, HUD, mano, banners, diálogos, medidor de contraataque, menú/instrucciones/ayuda,
  escenas de victoria/derrota, instantáneas de turno), `SceneScreen` (final del juego y paseo al minijuego, guion `game_complete.xml`/`minigameN.xml`),
  `KrustyCutScene`, `MinigameScreen` (juego de cartas de Bubble Buddy), `IntroScreen`, `CreditsScreen`, `HelpScreen`, `InstructionsScreen`, `InGameMenuScreen`.
  Utilidades: `ClipPanel` (clip + botones DefineButton con hover/clic), `CardCarousel`, `MapPaths/TourBus/MapFlag` (`ui/world_map.*`).
  Ejecutable: `build/sbso`; opciones de prueba: `--battle S,N --map --chest --intro --credits --scene N --minigame N --cleared S,N [--just-won]
  --coins N --profile NOMBRE --suspend TICK --type TEXTO@tick --key CODIGO@tick --skip-dialogues --headless N --shot f.png --click x,y@tick
  --act CARTA,tx,ty@tick --end-turn@tick`.
- Herramientas de captura: `sbso_shot`, `sbso_shot_sdl`, `sbso_shot_level` (ver `tools/shot/`).
- El extractor también procesa el SWF principal recuperado (clave `main`: autobús, widgets de perfil, cursor, escudo).

### Retomar en local (léelo primero)
- La rama de trabajo es `main` (el trabajo de las sesiones en la nube ya está fusionado). No hay PR abierto.
- Puesta en marcha: `cmake --preset macos && cmake --build --preset macos && ctest --preset macos` (o `tools/setup_local.sh`, sin preset).
  El build extrae los assets solo. Requisitos: cmake, compilador C++17, python3 + `requirements.txt`, ffmpeg. En el Mac del usuario
  **no hay Ninja ni Xcode completo** (presets con Makefiles; iOS sin compilar) y el disco va muy justo (~2 GB libres).
- Los originales (`SpongeBob Atlantis SquareOff - WildGames/`, `recovered/`) siguen siendo de solo lectura.
- Las capturas de verificación se hacen sin pantalla: `./build/sbso --extracted extracted --maps "<juego>/maps" --saves /tmp/s --headless TICKS --shot f.png`
  con los ganchos de `main.cpp` (`--battle S,N --map --chest --intro --credits --scene N --minigame N --cleared S,N --just-won --coins N
  --profile NOMBRE --click x,y@tick --key CODIGO@tick --type TEXTO@tick --suspend TICK --skip-dialogues --flow-battle S,N --win TICK
  --lose TICK --window WxH --import-sol f.sol`). `--battle 5,1` (sin `--skip-dialogues`) enseña los diálogos; `--profile` crea un perfil.
- Commits: mensajes claros en castellano, sin firmas de herramientas ni identificadores de modelo.
- Lo que NO se pudo hacer en la nube: proyectos iOS/Android (SDK/dispositivo), validar con trazas de Ruffle, probar un `.sol` real.
  Siguiente trabajo sugerido: (1) probar el juego a mano de principio a fin y anotar discrepancias con el original; (2) traducción es-ES
  cuando el usuario lo pida; (3) móvil (`docs/MOBILE.md`); (4) nube (al final).

- **App de macOS**: target `sbso_app` (preset `macos`) -> `build/macos/Atlantis SquareOff.app` con `game.pak` e icono (generado de los assets por
  `tools/icon/make_icon.py`); `tools/make_mac_app.sh` lo copia a `dist/`. Verificada en el Mac del usuario (macOS 26.4).
- Crash de redimensionado en macOS 26: era `SDL_SetWindowAspectRatio`; no volver a usarlo. La ventana se ajusta a 4:3..2.2:1 al terminar de
  redimensionar (`App::snap_window_aspect`).
- iOS: preset `ios`/`ios-simulator` (Xcode), `platforms/ios/`, `kMobile` en `app.h` (sin QUIT, pantalla completa, horizontal). Guía: `docs/ios.md`.
  **Compila y arranca en el iPhone 17 del usuario** (iOS 26.1) con `TEAM=UHVJ67NX4J INSTALL=1 tools/make_ios_ipa.sh` -> `dist/AtlantisSquareOff.ipa`.
  Xcode 26.6 está en /Applications pero `xcode-select` apunta a las CLT: usar `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.
  En iOS `game.pak` e iconos van como recursos del bundle (un POST_BUILD rompe la firma). Falta la prueba táctil a mano (ver checklist de docs/ios.md).
- Bugs de la prueba manual: `docs/BUGS.md` (marcar `[x]` al corregir; el usuario sigue añadiendo entradas).

### Compilar y probar
```
cmake --preset macos            # o linux / windows / code-only (sin datos del juego, lo que usa la CI); ver docs/building.md
cmake --build --preset macos && ctest --preset macos
```
Los tests que necesitan `maps/` o `extracted/` se saltan si no existen. SDL3 se compila desde el tarball de GitHub (release 3.2.x).
Para pruebas sin pantalla: `SDL_VIDEODRIVER=dummy` y el renderer "software".

### Pendiente (por orden sugerido)
1. Localización es-ES (lo pedido: **aún no traducir**): infraestructura lista (`src/i18n`, selector en Opciones, `App::tr`); faltan las
   traducciones (`recovered/localization/strings_en.csv`, columna `spanish` vacía; ya no está en git: las cadenas en inglés son del juego, así que la tabla debe generarse desde los assets del usuario y en el repo solo pueden ir las traducciones propias por clave), la herramienta que genera `extracted/strings/es-ES.json`
   y usar `tr()` en el resto de pantallas (ya se usa en las pantallas nuevas con fallback en inglés). Glosario confirmado más arriba.
   Ojo: el nombre de perfil solo admite ASCII en mayúsculas (como el original); decidir si se amplía a Latin-1 con la traducción.
2. Guardado: importación de .sol hecha (`src/save/sol_import.*`, `--import-sol f.sol` o `SBSO2Profiles.sol` junto al pack/en el directorio de preferencias; probada con SOL sintéticos AMF0/AMF3, **falta validarla con un .sol real**; el Point de killedKelps puede venir vacío en el original); nube al final (componente C99 neutral). El menú de pausa ya tiene el botón "partidas guardadas".
3. Fidelidad que queda: (diálogos: DialogueBubbleMover y ColorChanger ya portados), cursor GameCursor,
   `ProfileScreen` solo muestra 3 perfiles como el original, interpolación del autobús validada solo a ojo, flags `playUnlock` ya fieles.
4. Plataformas móviles (iOS/Android): los assets ya se leen por `src/core/vfs.*` y pueden ir en un único `.pak` (`tools/pack/make_pack.py <extracted> <maps> game.pak`;
   `sbso --extracted game.pak`). El usuario empaqueta sus propios assets (no se distribuyen). Falta: proyectos iOS/Android, `vfs::set_file_reader` para APK/bundle, entrada táctil real; escena extendida real en el resto de pantallas (solo la batalla usa el ancho extra; las demás se centran en 4:3 y `App::render_frame` rellena los márgenes con un desenfoque oscurecido del propio fotograma, `Screen::uses_wide_layout`) y revisión táctil.
5. Validar fidelidad con trazas de Ruffle (orden de iteración de Object en A*/rangos, desempates) y tiempos de fotograma.

### Decisiones de fidelidad tomadas en esta fase (ver también docs/LOGIC_NOTES.md)
- `gotoAndPlay(n)` sobre el fotograma en el que ya está el clip no vuelve a ejecutar el script de ese fotograma (Flash tampoco); sin esto
  las animaciones `flip` de las cartas y las miras se quedaban paradas.
- Final del juego: el guion `game_complete.xml` tiene dos `fadeout`; el original lanza los créditos tras el *primero* y termina ahí
  (los pasos posteriores, la escena del Krusty Krab, son código muerto). Se reproduce igual.
- El cofre limita el desplazamiento de la rejilla a filas enteras (el original usaba una división real y podía quedar a media fila).
- El destino de la autobús tras desbloquear banderas no queda "seleccionado" visualmente hasta que se hace clic (comportamiento original).
