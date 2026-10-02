# Plan del port nativo

Ver fases y validación en `AGENTS.md`. Aquí van los detalles de diseño a medida que se concreten.

## Arquitectura (borrador)
- `tools/extract/`: extractor SWF -> paquete de datos (atlas + manifest + WAV + XML). Salida fuera de git.
- `engine/`: SDL3 + SDL_GPU, escenas, entrada unificada ratón/táctil, audio 48 kHz, texto.
- `game/`: lógica pura serializable (sin dependencias de render), PRNG con semilla.
- `ui/`: pantallas; layout reubicable para pantallas anchas.
- `save/`: perfiles, ranuras, instantáneas versionadas; interfaz SaveBackend para la nube.
- `i18n/`: tablas de cadenas por idioma (en, es-ES).
- `tests/`: modo headless determinista contra trazas de Ruffle.

## Puntos abiertos
- Tratamiento de animaciones de MovieClip: hojas de sprites offline frente a reimplementar display list.
- Esquema táctil sin hover.
- Nº de ranuras y límite de perfiles; auth de la nube.

## Estado fase 1 (extractor)
- `tools/extract/extract.py` (Python 3 + Pillow + numpy) lee los SWF sin ffdec: bitmaps (lossless, lossless2, JPEG2/3), sonidos MP3 y SymbolClass.
- Sobre el juego original extrae 6.814 imágenes y 182 sonidos (coincide con el estudio previo). Alfa guardado sin premultiplicar.
- `characters.py` + `extract.py` exportan además, por SWF, `characters/<swf>.json` con formas (7.084, todas parseadas; casi todas relleno de bitmap, 0 degradados), sprites con línea de tiempo (1.132), botones (87), textos editables (11), fuentes (8) y la línea de tiempo raíz. Coordenadas en píxeles; la matriz de relleno de bitmap lleva escala x20.
- `render_frame.py` es un renderizador de depuración. Validado con `titlescreen` sprite 67: frame 230 = menú, frame 175 = splash de Big Fish (el rango a retirar está confirmado).
- Pendiente: clipDepth/máscaras (15) en el renderizador y en el exportador, scripts de fotograma (DoABC; están en `recovered/source/scripts`), DefineMorphShape (4), DefineText estático (69), glifos de fuentes, conversión de audio a WAV 48 kHz, empaquetado en atlas.


## Estado (2026-10-01, fin de sesión larga)
Hecho: extractor completo para bitmaps/sprites/scripts/fuentes/audio; lógica de combate y guardado en C++ con pruebas;
runtime de MovieClip con renderizador de software y SDL3; visor de niveles. Ver `AGENTS.md` (sección "Estado actual").
Decisión técnica nueva: el filtro Lanczos se aplica al cargar cada bitmap (CPU, `core/resample`) y se dibuja con coordenadas
nativas, sin shaders; el modo "Original" usa los bitmaps sin tocar con escalado entero por vecino más cercano.
