# Bugs conocidos (prueba manual en macOS)

Primera pasada de prueba a mano en el Mac (build local, rama `main`). La lógica principal funciona; lo de abajo son
discrepancias con el original. Cada entrada lleva síntoma, causa probable (hipótesis, **sin verificar** salvo que se
diga) y por dónde empezar. Marcar `[x]` al corregir. Hay más bugs por descubrir: seguir probando y añadirlos aquí.

## Pendientes

- [ ] **1. Una medusa muestra el sprite de un cofre.**
  - Síntoma: una unidad medusa se dibuja con el sprite de un cofre.
  - Pistas: las medusas intervienen en el barril que libera una medusa (`barrel_jelly`, bit 16 de `Event::f`,
    `src/app/screens/battle_screen.cpp:252`, `src/app/ui/battle_animator.cpp:71-84`) y en el contraataque
    `ATTACK_PINK_JELLYFISH_1` (`src/game/sim.cpp:793`). Probable: símbolo/etiqueta equivocada al crear el clip, o
    unidad que usa el índice de sprite de otro tipo en `src/engine/level_view.cpp` (`build_units`).
  - Cómo reproducir: anotar nivel y momento (aún sin datos). Con `--battle S,N` se puede aislar.
  - **Investigado, sin reproducir**: todos los sprites de unidad (`units.jelly_*`, `chest`, `barrel`) se dibujan bien por
    separado; las unidades se asocian por id y el id de la medusa del barril es nuevo. Ojo: en 1-1 el cofre, Patrick y el
    amigo atlante son decorado (`effect="exit"`). Para localizarlo, ejecutar con `SBSO_DEBUG_UNITS=1` (imprime id/tipo/símbolo de
    cada unidad que se crea) y pasar nivel, turno y qué estaba pasando (¿en el mapa, en la animación de combate, en el HUD?).

- [x] **2. Cargar una partida guardada no la carga: vuelve a la selección de mapa.**
  - **Corregido**: `flow::saved_games` ahora reanuda la batalla de la instantánea (fija `session` al nivel de la
    instantánea y llama a `flow::battle(app, true)`); sin instantánea va al mapa. Verificado con el menú de pausa
    (guardar en la ranura 1 y cargarla: "battle: resumed from a saved turn").
  - Causa (leída en el código): `SavesScreen` (`src/app/screens/saves_screen.cpp:66`) llama a `App::load_from_slot`
    y luego a `on_loaded_`, que es `level_select(app)`. La instantánea de nivel solo se ofrece al pulsar "Go" en el
    mapa (`level_select_screen.cpp:~140`, diálogo "Continue your saved battle?"). Cargar una ranura con
    instantánea debería ir directamente a `flow::battle(app, true)` (o al menos abrir ese diálogo).
  - Revisar también: cargar desde el menú de pausa de una batalla en curso (debe sustituir la batalla actual) y que
    `session_.stage/level` apunten al nivel de la instantánea, no a `latest_level(progress_)`
    (`src/app/app.cpp:221`).

- [x] **3. El cartel de VICTORY no sale de la pantalla.**
  - **Corregido**: el cartel termina su deslizamiento en x=786 (fuera de los 640 px del original); en escena ancha
    se le suma el ancho extra a los últimos fotogramas (`battle_screen.cpp`, dibujo de `end_anim_`). Verificado a 1311x603.
  - Síntoma: al final de la animación el cartel se oculta solo parcialmente y sigue viéndose un trozo.
  - Pista: `end_anim_` se dibuja con matriz fija `(0, 70)` en coordenadas de diseño 640x480
    (`battle_screen.cpp:675`), sin tener en cuenta la escena extendida (`viewport_w_`) ni la altura/escala de la
    ventana. El original lo sacaba del área visible; con la ventana más ancha/alta queda dentro. Comprobar también
    DEFEAT (offset 0) y el banner de turno (`battle_screen.cpp:674`).

- [x] **4. Los textos de los diálogos se cortan o no se muestran bien.**
  - **Corregido (dos causas)**: (a) los hablantes que no están en el mapa de colores de `DialogueBox` (`gaurd_scared`,
    `sandy_smile`, `king_angry`, `king_hypnotized`, `patrick_angry`, `gary_dazed`, `robo_krab`, `sandy_look_up`) usaban el
    color 10 (blanco): texto blanco sobre cuadro blanco. En el original `undefined` pasa como `uint` = 0. (b) `count_lines`
    era una estimación: ahora se mide el ajuste de línea real (`TextRaster::measure`) y el paso de línea es el de Flash
    (25 px, `TextRun::line_height`). Verificado con `--battle 4,1`: 6 líneas, 3 por página.
  - Pistas: `src/app/ui/dialogue_screen.cpp`: `count_lines` es una estimación ("22 px en caja de ~390 px", línea 34),
    y el desplazamiento por pulsación es de 3 líneas * 25 px fijos (línea ~158); si el ajuste de línea real del
    motor de texto no coincide, se cortan líneas o sobra/falta scroll. Revisar el máscara del cuadro, la fuente
    usada (TikiMagic) y el ajuste de línea en `set_text`. Probar con `--battle 5,1` (sin `--skip-dialogues`).
  - Ojo: cuando llegue es-ES los textos serán más largos; resolverlo con medición real de texto, no con estimación.

## Segunda tanda (prueba manual)

- [?] **5. El menú de "New card earned" muestra los bordes.**
  - Probado con `--gain-card ATTACK_BUBBLE_PUNCH_2` (y `--gain-belt N`). Cambio hecho: en escena ancha el contenido del diálogo se recorta al
    escenario 640x480 y las barras de letterbox se repiten a los lados (`dialogue_screen.cpp`). **Pendiente de confirmar** qué bordes
    se veían exactamente (¿las barras negras que no llegaban al borde de la ventana?, ¿una línea clara en el borde del panel amarillo?).
- [x] **6. Al ganar esperaba un clic para volver al mapa.** El original sale solo al llegar al último fotograma de `VICTORY_ANI` /
  `DEFEAT_ANI` (`endBattleAni_CB`); ahora `BattleScreen::tick` sale en ese momento.
- [x] **7. El HUD se salía del mapa en pantallas anchas.** El mapa se amplía (`zoom_`) cuando el nivel es más estrecho que el viewport
  (`update_zoom`, `to_map` y la cámara dividen/multiplican por el zoom; `Screen::content_zoom` hace que los assets se remuestreen a esa escala).
- [x] **8. El sonido del autobús se quedaba pillado al cambiar rápido de mapa.** El bucle `mapscreen_ship_movement_loop` solo se paraba al
  terminar el trayecto: ahora `~TourBus` y `StageMap::silence_bus()` (al cambiar de mapa) lo paran, y se para el anterior antes de arrancar otro.
- [x] **9. El borde blanco que bota al pasar el cursor por un botón solo salía en una esquina.** Causa: el extractor
  (`tools/extract/characters.py`) sobrescribía la tabla de rellenos cuando una forma definía estilos nuevos a mitad (`StateNewStyles`),
  así que solo sobrevivía el relleno de una esquina. Ahora se acumulan las tablas y se reindexan. **Hay que reextraer los assets**
  (`python3 tools/extract/build_assets.py`); puede mejorar otras formas con varios estilos.
  - Gancho de prueba nuevo: `--hover x,y@tick` (mueve el puntero sin pulsar).
  - Efecto colateral (verificado con capturas antes/después): también recuperan su arte el fondo verde del cuadro de la intro, el papel
    amarillo de los créditos y las estrellas/moneda del HUD de batalla. Los scripts reextraen solos (`build_assets.py --if-stale`).

## Tercera tanda

- [x] **10. Los carteles "SpongeBob's turn" / "Enemy turn" no ocupaban toda la pantalla (escena ancha).** La banda (profundidad 1 del clip) se
  estira ahora a todo el viewport (`MovieClip::stretch_depth_x`, usado en `BattleScreen::draw`).
- [?] **11. "New card earned": quedan líneas negras translúcidas.** Las líneas discontinuas que se ven junto al borde interior del poste izquierdo
  del bambú son píxeles semitransparentes oscuros del propio bitmap del marco (`GUI/80.png`, alfa 65-129). No he conseguido aislar si el
  original las ocultaba; **pendiente de que confirmes dónde las ves (captura con el sitio marcado)**. Prueba: `--gain-card ATTACK_BUBBLE_PUNCH_2`.
- [x] **12. El cierre de "New card earned" era brusco.** Causa: la máscara de la animación de salida es una forma *morph* (DefineMorphShape) que el
  extractor ignoraba, y además el original re-coloca el panel como instancia nueva (su contenido arranca fuera de pantalla). Ahora el extractor
  convierte cada (morph, ratio) en una forma normal (`tools/extract/morph.py`) y el panel conserva su instancia (`MovieClip::keep_instance`),
  de modo que la carta y el texto bajan junto al bambú. **Hay que reextraer** (los scripts lo hacen solos).
- [~] **13. El botón Play, las banderas y el autobús del mapa "saltan" demasiado deprisa.** Las animaciones del mapa se crearon a 12 fps y el
  original fija `stage.frameRate = 38`; no he podido comprobar a qué ritmo corría de verdad. Ahora el mapa va a 24 fps por defecto
  (`Settings::map_fps`, editable en `settings.json`, o `--map-fps N` por línea de comandos). **Dime qué valor te parece bien** y lo fijo.

## Cuarta tanda

- [x] **14. El autobús iba a un nivel bloqueado y se desbloqueaban niveles de otros mundos.** Causa raíz: la animación de desbloqueo de la
  bandera blanca (minijuego) arranca parada por un `stop()` y el original la pone en marcha con `play()` (`FlagMarker_unlock.playUnlock`);
  el port no lo hacía, así que el contador de desbloqueos no llegaba a 0, el nivel no se marcaba superado y `level_just_completed` quedaba
  activo: al cambiar de mundo se aplicaba la secuencia de desbloqueo al mapa equivocado. Corregido (`UnlockFlag::tick`) y además la secuencia
  solo se ejecuta si el mapa mostrado es el del mundo jugado (`LevelSelectScreen::scroll_done`). Verificado encadenando victorias por los
  mundos 1 y 2 (`--win` repetible).
- [x] **15. El botón Play seguía muy acelerado.** Las animaciones de los estados de un botón (la flecha que salta) se repetían en bucle y
  corrían aunque el estado no estuviera visible; en el original se reproducen una vez al entrar en el estado (`addFrameScript(stop)`).
  Ahora solo avanzan en su estado, arrancan desde el primer fotograma y paran en el último (`stop_button_over_clips_at_end`).
- [x] **16. La música del título no sonaba al abrir el juego.** `sbso2.finishLoad` hace `switchMusic("menu")` al inicio; el port solo lo hacía al
  volver al título (`main.cpp`).
- [?] **17. Bocetos de los créditos con "overlines" negros.** No lo he conseguido reproducir (ni a 1x ni a 4x de escala); las imágenes
  de los bocetos son opacas y traen el sombreado de las columnas incluido. **Necesito una captura** con el sitio marcado.
- [x] **18. Los menús nuevos no respetan la interfaz original** (opciones de idioma/gráficos, "partidas guardadas" en pausa y en perfiles,
  Corregido: `ui/skin.*` dibuja el marco de bambú (9 trozos) y los botones verdes originales; lo usan SavesScreen, OptionsScreen, pausa y perfiles.

## Ideas / por comprobar
- Buscar más discrepancias recorriendo todo el juego (mapa, cofre, minijuego, créditos, opciones) y compararlas con
  Ruffle ejecutando el SWF original (ver `AGENTS.md`, sección Validación).

- [x] **19. Bocetos de los créditos con bandas oscuras / bordes negros translúcidos**: los DefineBitsJPEG3 de Flash traen el color
  ya premultiplicado por el alfa; el extractor ahora lo detecta y lo deshace (`tools/extract/extract.py`, hay que re-extraer).
  Posible causa también de las líneas del cuadro "New card earned" (por confirmar con captura).
- [x] **20. Números de coste/pasos de las cartas**: salían como manchas blancas sobre el arte; el campo de texto original
  (Card.as) pone la línea base a y+2+ascent, no a y+tamaño. Corregido en cofre, barra de acción y minijuego.
- [x] **21. Franjas verticales en el mapa de fondo del diálogo de carta** (costuras de la escena extendida, x≈260/1740 a 2000 px de ancho). El
  relleno de los márgenes de las pantallas 4:3 estiraba todo el fotograma desenfocado sobre la ventana, así que en la unión el color no casaba con
  el borde del centro (que además trae una viñeta oscura pintada). Ahora cada margen prolonga la columna del borde, muy desenfocada, y se
  oscurece en degradado (`App::render_frame`).

## Quinta tanda
- [x] **22. Línea oscura en "New card earned"**: el bitmap del patrón amarillo (GUI 65) tiene un borde transparente de ~3 px que deja ver el fondo junto al bambú; el extractor lo rellena (`bleed_edges`).
- [x] **23. Límite de ventana**: `SDL_SetWindowAspectRatio(4:3 .. 2.2:1)`; más ancho que eso la escena extendida se queda sin arte.
- [x] **24. Cuadro de ataque/defensa con pantalla ancha**: el fondo oscuro cubre todo el ancho y las animaciones quedan centradas en el escenario de 640 (`BattleAnimator::draw`).
- [x] **25. Partida cargada: el enemigo muerto reaparecía**: los visuales de unidades muertas se crean ocultos al reanudar (`BattleScreen`).
- [x] **26. Menú de guardado**: texto con contorno verde oscuro como el original, centrado vertical, sombra del botón más marcada (`Skin::text/button`). Sin verificar el ataque en pantalla (no hay gancho que provoque un combate en ancho).
- [x] **27. Números de las cartas como manchas blancas**: causa real, `FontBank::add_directory` se quedaba con la primera "Unibody 8 Black" (la de 7 glifos de `instructions.swf`); ahora gana la de más glifos. Además las fuentes de píxel Unibody ≤10 pt se rasterizan a 1x y el renderer las escala (`TextRaster::Result::scale`). Los números del punto 20 eran ya correctos de posición.

## Sexta tanda
- [x] **28. Hover en "saved games" (perfiles)**: los botones de `Skin` dibujan ahora los corchetes blancos que rebotan y suenan `button_rollover` al entrar.
- [x] **29. Barra de vida del enemigo llena en su turno**: al empezar el intercambio de golpes los paneles se abrían con barras al 100 %; ahora usan el % real (`BattleScreen`) y `Hud::bar_to` ya no pierde el valor si el panel está en el fotograma 1.
- [x] **30. Panel del enemigo muerto**: nunca se llamaba a `hide_enemy`; ahora se oculta al acabar el combate del jugador y al empezar su turno (como BattleManager/TurnManager).
- [~] **31. Crashes al redimensionar**: no reproducido (con el renderer software y 4 pantallas, 60 redimensionados seguidos van bien). Mitigado: las texturas Lanczos solo se rehacen cuando el tamaño se estabiliza (12 fotogramas), escala de assets limitada a 4x y texturas ≤ 8192 px. Si vuelve a pasar, hace falta el informe de fallo de macOS (Console → Informes de fallos).
- [x] **32. Cargar partida desde Perfiles (menú principal)** no llevaba al nivel: ahora usa `flow::after_slot_loaded` (retoma la instantánea de batalla o va al mapa).

## Séptima tanda
- [x] **33. Corchetes de hover** de los botones nuevos: ahora usan los bitmaps originales (titlescreen 3/4, deslizan ~2 px) en vez de líneas dibujadas. Los botones del menú principal seguían "hover" mientras había un overlay encima: `App` mueve el puntero fuera de la pantalla de debajo al abrir un overlay.
- [x] **34. Ver la salud del enemigo antes de atacar**: el original solo muestra el panel del enemigo en su turno y en los combates; añadido (mejora del port) mostrarlo mientras el puntero está sobre un enemigo en el turno del jugador (`BattleScreen::update_hover_enemy`).
- [~] **35. Crash al redimensionar**: sigue sin reproducirse en Linux (renderer software, tamaños de 1x1 a 4000x300 en 6 pantallas). Añadido informe de fallo: un crash nativo escribe `crash.txt` con la pila en el directorio de guardados (macOS: `~/Library/Application Support/sbso/atlantis/saves/crash.txt`). Mandarlo para localizar la causa.

## Octava tanda
- [x] **36. Vida distinta al atacar**: el panel del enemigo arrancaba el combate con `salud actual + daño` (mal con exceso de daño) y energía ya gastada. `Event::Battle` lleva ahora `h0`/`e0` (salud del defensor y energía del atacante antes del golpe) y las barras animan desde ahí.
- [x] **37. Corchetes que no botan**: el sprite original alterna 5 fotogramas en reposo y 5 a 2,2 px (32 fps); ahora se reproduce así, en bucle, mientras dura el hover.
- [x] **38. Corchetes en los botones originales a la vez que en "saved games"**: además de apagar el hover de la pantalla de debajo, el ProfileScreen deja de pasar el puntero a sus botones originales cuando está sobre el botón añadido. No se reproduce en las capturas; los corchetes blancos fijos alrededor del perfil seleccionado son originales y siguen ahí.
- [~] **35. Crash al redimensionar**: sin novedades; falta el `crash.txt`.

## Novena tanda
- [x] **35/31. Crash al redimensionar (macOS 26)**: los 7 informes de `~/Library/Logs/DiagnosticReports/sbso-*.ips` son el mismo `SIGTRAP`
  dentro de AppKit (`-[NSWindow _adjustNeedsDisplayRegionForNewFrame:]`) al hacer zoom, salir de pantalla completa o arrastrar el borde, y
  empezaron 3 minutos después de añadir `SDL_SetWindowAspectRatio` (punto 23). Con mínimo != máximo, SDL 3.2 pone en Cocoa
  `setContentAspectRatio:{0,0}`. Quitado: la escena ancha se limita a 2.2:1 en `compute_layout` (`kMaxLogicalW`) y por encima salen barras
  negras. Sin informes nuevos desde entonces. Además el manejador de fallos ahora captura `SIGTRAP` y solo crea
  `crash.txt` al fallar (antes lo vaciaba en cada arranque).

## Décima tanda
- [x] **39. Franjas verticales en "New card earned" sobre la batalla** (x≈230/1820 a 2000 px): la capa oscura del letterbox (`Dialogue_LetterBox`,
  forma 51) mide 645 px y las copias laterales se ponen cada 640, así que se solapaban ~5 px con doble oscurecimiento en los bordes del escenario.
  Recortar cada copia no bastaba: las barras son un bitmap con bordes semitransparentes y seguía viéndose una línea en la unión. Ahora el
  letterbox no se copia, se estira en horizontal sobre todo el ancho (`DialogueScreen::draw`). Verificado sin líneas a 1600x900, 2000x1178 y
  2400x1100. Además los recortes rectangulares redondean en vez de floor/ceil (`rect_of`, `sdl_renderer.cpp`). Gancho nuevo: `--battle S,N --gain-card X`.
- [x] **40. Corchetes de los botones nuevos** pegados al borde y encogidos: ahora se colocan como en el original (sprite 138x64 en (-5.85, -8.1)
  sobre un botón de 126x51: ~6 px a los lados, 8 arriba, 5 abajo) y a tamaño real salvo en botones muy bajos (`Skin::button`).
- [x] **38. Corchetes duplicados en Perfiles**: los corchetes de selección de perfil se ocultan mientras el puntero está sobre "saved games".

## Undécima tanda
- [x] **41. Corchetes de QUIT detrás de "saved games"** (pausa): el botón añadido se dibuja antes que los originales y sus propios corchetes al final
  (`Skin::button(..., brackets_out)`, `InGameMenuScreen::draw`).
- [x] **42. Bordes negros al redimensionar**: el límite nativo de aspecto provocaba el crash de macOS 26 (punto 35), así que la ventana se ajusta sola
  a la proporción válida más cercana (4:3 a 2.2:1) cuando termina el redimensionado y se suelta el ratón (`App::snap_window_aspect`). Probado en el Mac:
  1470x500 -> 1100x500, 700x809 -> 700x525, sin cierres. Durante el arrastre aún se ven barras un momento.
- [x] **43. Botones nuevos con estilo propio**: ahora se construyen con el bitmap del botón original (OPTIONS / QUIT del menú de pausa, extremos sin
  estirar y centro desde una franja sin letras) y el texto lleva el halo verde del original. En Perfiles, "saved games" va en la fila del OK con su altura.
- [x] **35. Crash al redimensionar**: no ha vuelto a pasar (sin informes nuevos en DiagnosticReports desde que se quitó el límite de aspecto).

## Duodécima tanda (iPhone)
- [x] **44. Táctil sin respuesta en iOS**: las coordenadas del dedo se multiplicaban dos veces por el tamaño de pantalla (SDL ya las convierte en
  `SDL_ConvertEventToRenderCoordinates`). Además se tratan los toques cancelados por el sistema.
- [x] **45. Velo oscuro del tutorial solo en 640 px**: la capa 1 de `cContent` (help_2..help_5) se estira a todo el ancho (`HelpScreen::draw`).
- [x] **46. Partidas guardadas diminutas en el móvil**: en móvil el panel ocupa todo el escenario, con filas de 35 px, botones de 78x30 y texto mayor.
- [x] **47. Botones de menú/ayuda/ocultar bajo la esquina del iPhone**: el HUD y esos botones se colocan dentro de la zona segura (`safe_left/right`).
- [x] **48. Tocar en cualquier parte**: avanza diálogos, "New card earned" y los pasos del tutorial (salvo los finales, que se cierran con su botón).
- [x] **49. Botones pequeños para el dedo**: en móvil cada zona de toque tiene 10 px de margen (`ButtonHit::slop`; los aciertos exactos tienen prioridad).
- [x] **1. Medusa/anguila con sprite de cofre (1-1)** (ver punto 50): corregido un caso relacionado: al pasar por el decorado (cofre, Patrick) salía el panel de enemigo
  con el retrato de otra unidad; ahora solo hay panel para unidades con retrato propio. **Pendiente de confirmar** si era esto.
- Vista previa del móvil en escritorio: compilar con `-DCMAKE_CXX_FLAGS=-DSBSO_FORCE_MOBILE` (p. ej. `build/mobile-preview`).

## Decimotercera tanda (iPhone)
- [x] **50. El cofre del 1-1 salía como enemigo en el tutorial**: faltaba `Map.bubbleOffList` tras el diálogo de introducción (`GameLevel.DIALOGUE_OVER
  "stage_intro"`): las unidades `effect="exit"` (atlante, Patrick, cofre; Burbuja Sucia en 5-1...) se van entre burbujas. `Sim::bubble_off_list`.
  El cofre sobre el señor Cangrejo en la escena del Krusty Krab es original (`CutSceneScreen.DIALOGUE_NEXT` lo añade como hijo de Krabs).
  El test `app_battle` atacaba a Burbuja Sucia (decorado): ahora ataca un barril en 2-2.
- [x] **51. QUIT asomaba un fotograma al volver al menú (móvil)**: se oculta también justo antes de dibujar.
- [x] **52. Sin icono en iOS**: catálogo `Assets.xcassets` con el icono de 1024 generado al configurar (`ASSETCATALOG_COMPILER_APPICON_NAME`).
- [x] **53. Menú/ayuda/ocultar diminutos en móvil**: se dibujan a 1,8x. **54. Barra de acciones a 1,3x en móvil** (dibujo y toques, anclada abajo al centro).
- [x] **55. Instrucciones cortadas a 4:3 y cuadro verde abajo a la derecha**: el fondo (capa 1) se estira a todo el ancho; CONTINUE y TOOL TIPS, aparcados
  fuera del escenario original, se ocultan mientras estén fuera.
- [x] **56. Deslizar para cambiar de tipo de carta**: un deslizamiento horizontal rápido en cualquier parte gira el carrusel (gancho de prueba `--drag`).

## Decimocuarta tanda
- [x] **57. Autoguardado más frecuente**: la instantánea de la batalla se escribe al empezar cada turno del jugador (antes solo al salir o al pasar a segundo plano).
- [x] **58. LOAD en lugar de QUIT (móvil)**: botón con el estilo del menú en el sitio de QUIT (que sigue oculto debajo); carga la ranura más reciente del perfil
  y retoma la batalla si la había (`flow::after_slot_loaded`). Desactivado si no hay partidas.
- [x] **59. CLOSE más grandes en móvil**: `MovieClip::enlarge_named` (dibujo y toque, alrededor del centro) a 1,6x en instrucciones, opciones, perfiles,
  tutorial y el CLOSE del cuadro de diálogo.

## Decimoquinta tanda
- [x] **60. App/.ipa de 170 MB (el juego original son 27 MB)**: el audio se guardaba en WAV de 48 kHz (114 MB). Ahora el extractor escribe FLAC sin pérdida
  con el mismo remuestreo (35 MB; comprobado bit a bit contra los WAV) y el paquete ya no lleva los MP3. `game.pak` 173 -> 85 MB, app 87 MB, `.ipa` 80 MB.
  Las imágenes (55 MB en PNG) podrían bajar más guardando como JPEG las que lo eran en el original.
- Arreglado de paso: las rutas relativas de `game_data.py` acababan en `tools/extract/`; la app de macOS y el `.ipa` no recogían un `game.pak` nuevo.
- [x] **61. Tamaño mínimo**: `game.pak` 85 -> 25 MB (el juego original son 27 MB); app de macOS 30 MB, `.ipa` 30 MB.
  - Audio: los MP3 originales (5 MB) en vez de PCM a 48 kHz. El juego los descodifica (minimp3, salida float) y remuestrea con una réplica de
    libswresample 8.1 (64 coeficientes, Kaiser 9, fases exactas, suma float par/impar, reflejo final). Prueba de cancelación contra los WAV/FLAC de
    ffmpeg en los 182 sonidos: pico del residuo -90,3 dBFS (1 LSB de 16 bits), media -101,6 dB, mismas longitudes. Carga diferida: 85 ms una pista de 1 min.
  - Imágenes: WebP sin pérdida exacto (píxeles idénticos), libwebp como dependencia de CMake. Paquete v2 (`SBSOPAK2`): duplicados una sola vez
    (1.513 bitmaps repetidos entre SWF) y JSON/XML con zlib. ffmpeg ya no hace falta para extraer.
