# Notas de lógica del juego original (extraídas de recovered/source/scripts)

Objetivo: separar la simulación (datos puros, determinista) de la presentación (animaciones).
En el original están entrelazadas: el resultado de una batalla se calcula al empezar (`BattleManager.battle`)
pero se "revela" con animaciones guiadas por frames (`addFrameScript`), y la IA es una máquina de estados
con retardos medidos en frames (`_mAIDelay`). El port debe producir la misma secuencia de decisiones/daños,
no los mismos tiempos.

## Unit (Unit.as)
- Salud/energía con clamp 0..max. Movimiento por A* (4 direcciones) sobre la rejilla; `moveTo` descuenta el
  coste de la carta MOVE actual de la energía.
- Estados: IDLE, MOVE (+ THINK, ROUTE, ATTACK, BATTLE en la IA). Moverse a 5 px/frame sobre casillas de 30 px.
- `mAttackable=false` para amigos (`GameInfo.isFriend`); `lamp` y `barrel` no muestran salud.

## TurnManager
- Dos turnos: PLAYER=0, COMPUTER=1 (arranca en COMPUTER). Al cambiar de turno TODAS las unidades recuperan la
  energía máxima. Turno del jugador: se quitan escudos del mapa, se renuevan cartas (ActionGUI.renewCards),
  se reinician las cartas de defensa. Si no quedan AIUnits en el turno del jugador, se rellena la energía de SB.

## Resolución de batalla (BattleManager.battle)
- Carta de ataque actual: `damage`, `cost`. Si el atacante es SpongeBob y la carta tiene `counterGaugeSec>0`
  hay minijuego de medidor (daño = potencia elegida por el jugador, redondeada).
- Defensor: `getCurrentDefenseCard()` (la IA consume su lista de escudos con shift; SB usa la lista de GameInfo).
  Si la carta de defensa tiene `counterDamage>0` el defensor contraataca después con esa carta.
- Sin medidor: energía atacante -= coste; salud defensor -= max(0, daño - antidaño); el escudo se consume.
- Fin de batalla: contraataque si sigue vivo (o es "barrel"); si no, el turno continúa (`battleEnds` de la
  unidad cuyo turno es). Si SB llega a 0 de salud -> derrota. Monedas se suman al perfil al derrotar.
- Botín al derrotar a un enemigo por SB (setupAnimations): reglas por fase (stage): fase 2 kelp (cinturón si es
  el kelp nº1), fase 8 doodlebob (cinturón si es el último), fase 9 bot_plankton_red (cinturón), fases 3 y 6
  (cinturón si `mBelt`), si no, carta de `mDropCards` (`getDroppedCard`) o monedas (`mCoins`).
- `getDroppedCard`: índice = Math.random()*(drops + nº de huecos vacíos); fuera de rango -> nada; si la carta
  ya está en el perfil (y no es NICK) -> nada.
- Barril: al ser destruido, probabilidad `getBarrelInfo()`% (Math.random()*100) de soltar medusa rosa
  (contraataque ATTACK_PINK_JELLYFISH_1).

## IA (AIUnit)
- THINK_ENTER (una vez por turno de unidad): distancia a SB; elige carta de ataque: jefe = la de mayor daño
  alcanzable con la energía (ordenadas por daño desc.); resto = azar entre una ATTACK y una NICK y luego azar
  entre ambas. Si distancia > alcance máximo de la carta y tiene cartas MOVE asequibles -> elige MOVE al azar
  y encola ROUTE; si puede pagar el ataque encola ATTACK. Defensas ordenadas por daño desc.
- ROUTE: A* hasta SB (o hasta la casilla más cercana al huir); recorre el camino hasta la última casilla
  resaltada (alcanzable) y pulsa esa casilla; si el camino es vacío, `handleEmptyPath` busca la casilla más
  cercana a SB que mejore la distancia.
- ATTACK: espera `_mAIDelay`=10 frames, pulsa la casilla de SB si está resaltada; si no, vuelve a THINK.
- Fin de turno de unidad (`tryToEquipShieldOrRun`): apila escudos mientras alcance la energía (en orden de
  daño desc.). Si atacó y no hay escudos y Math.random()>=0.5 -> intenta huir con la primera carta MOVE
  asequible; si no, termina y pasa a la siguiente (`ai.makeDecision`).
- Puntos de azar de la IA: 6 llamadas a Math.random (elección de cartas, huida) + botín. Todas deben pasar por
  `sbso::Rng` en el port.

## A* (tinymantis/AStar.as)
- Vecinos en orden (+x, -x, +y, -y). Coste = coste de casilla (`map.getTile`) + 10*distancia Manhattan al destino;
  casillas >=1000 son intransitables. Elige el nodo abierto de menor coste iterando un Object (orden de
  iteración de AVM2: posible fuente de diferencias en empates; el port debe fijar un desempate estable y
  validarlo contra trazas de Ruffle). Devuelve vacío si el destino tiene valor 0 o está bloqueado.

## Riesgos de fidelidad a validar con trazas de Ruffle
1. Desempates del A* y orden de iteración de Object.
2. Orden relativo de eventos que en el original depende de frames (retardos de IA, fin de animaciones).
3. Redondeos (`int()`, `Math.round`, `parseInt` de strings del XML).

## Estado del port de la lógica (src/game)
- `database`, `level`, `board` (rangos de movimiento/ataque, A* voraz) y `sim` (unidades, IA, turnos, batalla,
  misiones, botín) están portados y probados (`tests/test_game.cpp`, `tests/test_sim.cpp`).
- El simulador es determinista: misma semilla + mismas entradas => mismo flujo de eventos (comprobado en las 54 fases).
- `StateMachine.setState` es diferido (se aplica en el siguiente tick); `Sim::tick_unit` lo replica.
- Los errores en manejadores de frame del original (p. ej. jefe sin energía: `mCardInfo[null]`) se emulan con `AsError`:
  el manejador aborta y el juego sigue.
- Rarezas conservadas: `numEmpties` se filtra entre enemigos al construir el mapa; `isBoss` usa `indexOf > 1`;
  `getClosestTileToUnit` puede devolver null aunque la primera casilla sea la más lejana; el bucle `for each(item in c)`
  de `buildMap` pisa `item` si un enemigo trae `<cards>` propias (ningún nivel lo usa, no portado).
- Desviaciones deliberadas: A* sin camino deja la unidad quieta (el original la bloquea); los retardos de la IA no existen.
- Instantáneas: `Sim::snapshot()/restore()` (formato binario versionado, hash de la fase, rechazo de datos corruptos); solo en
  límite de turno (`can_snapshot`). Probado: restaurar y continuar produce el mismo flujo de eventos y el mismo snapshot.
- Pendiente: perfil (cartas/cinturones/monedas persistentes), minijuegos, diálogos,
  validar el orden de iteración de Object (AVM2/Ruffle) con trazas reales.

## Presentation notes (screens)
- **World map progression** (`save/progression.*`): `GameInfo.buildUnlockSequence` is a table per stage; clearing a flag unlocks the listed flags, the
  bonus (white) flag only on odd stages (`Profile.setBonusState` ignores even stages), and the bus drives to `dest`, or to the next UNLOCKED flag along the
  stage's main path when `dest` is already cleared. Clearing flag 6 sends the bus to the terminal and unlocks flag 1 of the next stage.
- **Level just completed** (`GameLevel.endBattleAni_CB`): only a first-time clear (state UNLOCKED) or the last level of the game sets
  `mLevelJustCompletedFlag`; replaying a cleared level returns to the map without the unlock animation.
- **Battle intro** (`Map.introAnimation`): level 1-1 skips it (it starts with the Krusty Krab cut scene). Otherwise the map shows the flag first when
  it is off screen, waits 31 frames, then scrolls to SpongeBob with a cosine interpolation at ~10 px/frame; the stage intro dialogue comes after.
- **Tutorial** (`fForceShowHelp`): shown once, when the first player turn banner ends, while level 1-1 has not been cleared; closing it clears the flag.
- **Treasure chest**: belted cards are stored in collection order (not click order); the grid keeps a slot reserved for every owned card, so cards
  that are on the belt leave a hole. A card counts as "new" (sheen) if it was in the profile's new list when the screen opened; clicking it clears it.
- **Mid-level saves**: `Sim::snapshot` at the start of each player turn (and the first one at level start); stored with the autosave when the player
  quits or the app goes to the background; the map asks "Continue your saved battle?" when Play is pressed on the same level.
