# SM64 Living ROM — Informe de fases 2 a 10

Fechas: 2026-11-15 (fases 2–9), 2026-11-16 (fase 10). Base: `n64decomp/sm64` @ `9921382` (US).

Leyenda: ✅ hecho y probado en N64 emulada · 🟡 probado solo en PC.

| Fase | Estado | Evidencia |
|---|---|---|
| 1 Auditoría | ✅ | `docs/FASE1_AUDITORIA.md` |
| 2 Arquitectura del motor | ✅ | §2 |
| 3 Behavioral Tracker | ✅ | autoprueba en Vortex64 (§10): 4 niveles observados, celdas, monedas, acciones |
| 4 Memoria persistente | ✅ | `.eep` real: copias A/B válidas, CRC correcto, `OK` al reiniciar |
| 5 Micro AI | ✅ | rasgos/ánimo cambian y persisten; afinidad aprendida en la sesión 2 |
| 6 World Mutation Engine | ✅ | 5 mutaciones aplicadas y 1 rechazada por la geometría real |
| 7 Generación procedural | 🟡 | determinismo bit a bit probado en PC; en N64 solo se ha visto que los planes se reconstruyen |
| 8 Persistencia de mutaciones | ✅ | los planes de la sesión 1 se aplican en la sesión 2 |
| 9 Estabilidad y rendimiento | ✅ | medido con `osGetCount()` dentro de la ROM (§10) |
| 10 Compilación N64 + emulador | ✅ | IDO 5.3 sin errores; ROM original reproducida bit a bit con los hooks presentes |

Batería host: **62 pruebas, 62 correctas** (MSVC 14.44, `/W4 /WX`).

---

## 2. Arquitectura real del motor (investigada en el código)

Puntos de enganche elegidos (una llamada cada uno, todas bajo `#ifdef LIVING_ROM`):

| Hook | Fichero / función | Por qué ahí |
|---|---|---|
| `living_hook_boot` | `game_init.c` tras `save_file_load_all()` | EEPROM ya sondeada (`gEepromProbe`), partida original cargada |
| `living_hook_area_loaded` | `area.c` fin de `load_area()` | nivel/área conocidos; los objetos aún no: la IA aplica en el primer frame |
| `living_hook_area_unloading` | `area.c` inicio de `unload_area()` | los objetos **todavía existen**: se evalúa qué hizo el jugador con ellos |
| `living_hook_update` | `level_update.c` `play_mode_normal()` tras `area_update_objects()` | solo juego normal (no pausa, no menús); ignora la demo de título |
| `living_hook_death` | `level_update.c` `level_trigger_warp()` con `WARP_OP_DEATH`/`WARP_OP_WARP_FLOOR` | captura también caídas al vacío |
| `living_hook_game_saved` | `save_file.c` fin de `save_file_do_save()` | persiste la IA cuando el juego persiste |
| `living_hook_render` | `area.c` `render_game()` tras la escena 3D, antes del HUD | tinte ambiental sin tapar el HUD |

Hallazgos clave que condicionaron el diseño:

- **EEPROM llena**: 4 ficheros × 2 copias + menú = 0x200 bytes exactos (`STATIC_ASSERT` en `save_file.c`). La IA usa **EEPROM 16 Kbit**, bloques 64–255, sin tocar jamás los bloques 0–63.
- **libultra US no admite 16K**: `osEepromLongWrite/Read` rechazan `address > 0x40`; `osEepromRead/Write` exigen `type == CONT_EEPROM` (un chip 16K responde `0xC000`), y `osEepromProbe` nunca devuelve `EEPROM_TYPE_16K` fuera de iQue. Parcheados bajo `LIVING_ROM`.
- **`osEepromWrite` espera sola a que el chip termine** (bucle sobre `CONT_EEPROM_BUSY`): escribir 1 bloque por frame (33 ms) evita los bloqueos de `osEepromLongWrite` (12 ms de espera por bloque).
- **El linker lista objetos explícitamente** (`sm64.ld`), pero en US enlaza `src/game*.o` por comodín en `.text/.data/.rodata/.bss`: los envoltorios van en `src/game/` y no hace falta tocar `sm64.ld`.
- **Modelos cargados por nivel**: `gLoadedGraphNodes[model] != NULL` es la puerta de seguridad antes de generar cualquier objeto.
- **Slots de objetos reciclados**: un puntero a `Object` puede reutilizarse; los handles guardan objeto + comportamiento y se invalidan si cambia.
- **Git en Windows convierte a CRLF** y rompe `make`/scripts en WSL: el clon local se fijó a `core.autocrlf=false`, `core.eol=lf`.

## 3–8. Implementación

Código C89 portable, sin `malloc`, sin libc, sin coma flotante en el núcleo. El núcleo no conoce SM64: habla con el juego solo mediante `src/living_world.h` (operaciones en lista blanca), lo que permite probar exactamente el mismo C en PC.

```
src/
├── living_types.h            tipos, presupuestos y límites (todas las constantes en un sitio)
├── living_world.h            interfaz mundo: probe, spawn, despawn, relocate, rehome, fx
├── living.c/.h               orquestador: boot, entrar/salir de área, update, commit
├── ai/
│   ├── behavior_tracker.c    flancos por frame + muestra cada 15 frames (0,5 s)
│   ├── player_memory.c       memoria persistente, LRU de 12 áreas, rejilla 8×8, frontera
│   ├── personality_engine.c  10 rasgos observados → 6 rasgos de personalidad + FSM de 7 estados de ánimo
│   └── decision_engine.c     utilidad: rasgos×W + observaciones×C + ánimo + afinidad aprendida − novedad
├── world/
│   ├── procedural_generator.c hash/PRNG enteros deterministas, plantillas de estructuras
│   ├── mutation_engine.c     diario (journal) + rollback, presupuestos, validación de suelo
│   ├── geometry_mutator.c    escaleras / muros / pilares de cajas rompibles (200³)
│   ├── object_mutator.c      rastro de monedas, recolocar monedas, emboscada, rutina NPC, regalo, secreto
│   └── event_system.c        10 eventos contextuales con compuertas deterministas
├── persistence/
│   ├── save_manager.c        A/B + CRC16 + versión + saneado + escritura goteada por bloques
│   └── memory_storage.c      cortafuegos: bloques < 64 nunca se escriben
└── integration/
    ├── living_sm64.c         implementación sobre el motor real (find_floor, spawn_object…, osEeprom*)
    └── living_hooks.h
```

**Fase 3 – Behavioral Tracker.** Por frame solo hay comparaciones: cambio de acción (clasificada en 9 tipos), botones de cámara (C/R), monedas, estrellas, muerte. Cada 15 frames: celda (8×8 de 2048 u), calor por celda, celdas nuevas, última posición en suelo por celda, ruta (anillo de 16 transiciones), detección de bucles (secuencias repetidas de 3–5 celdas), inactividad. Habitaciones evitadas = **frontera**: celdas no visitadas adyacentes a celdas visitadas. Regresos = visitas en una sesión posterior.

**Fase 4 – Memoria persistente.** 640 bytes de carga útil + 10 de cabecera por copia (de 768 disponibles). Serialización explícita big-endian (independiente del compilador). Al cargar: copia más reciente válida; si falla, la otra (`RECOVERED`); si ambas fallan, reinicio limpio (`CORRUPT_RESET`); si la versión es más nueva, **modo solo-RAM sin escribir nada**; sin EEPROM 16K, solo-RAM. Saneado de campo a campo (rasgos, ánimo, ids de nivel/área, ids de op, coordenadas, claves duplicadas).

**Fase 5 – Micro AI.** Rasgos: curiosidad, juego, benevolencia, amenaza, paciencia, apego. Se mueven hacia el objetivo que implica la conducta observada (fracción de la distancia por reflexión). Estados: DORMANT (primeros 120 s de vida: solo observa) → OBSERVING → CURIOUS / PLAYFUL / PROTECTIVE / TESTING / UNSETTLING, con histéresis y edad mínima. La decisión no es aleatoria: es una función de utilidad con precondiciones duras; el azar determinista solo desempata (0–3 puntos) y fija detalles (posiciones). Aprendizaje: cada op aplicada se evalúa al salir del área (¿recogió las monedas?, ¿rompió las cajas?, ¿murió con la emboscada?, ¿se quedó con el ambiente?) y ajusta su afinidad (bandido simple con suelo/techo 32–224 para no dejar de explorar).

**Fase 6 – World Mutation Engine.** Cada op se registra en un diario; si cualquier primitiva falla, se deshace todo lo de esa op (despawn, posiciones y "home" originales). Fallos geométricos o de modelo no disponible marcan la op como rechazada (persistido, no se reintenta); fallos circunstanciales (Mario encima, pool lleno) se reintentan cada 5 s. Validación: suelo existente, seguro (no lava, plano de muerte, arenas movedizas, viento mortal, bajo el agua), pendiente (normal Y ≥ 0,7), escalón máximo, techo libre, columna vacía (detecta mesetas sólidas), caja de coordenadas ±8000, distancia a Mario ≥ 400, ≤ 16 objetos por área, pool de objetos < 190 de 240. Las estructuras son de **cajas rompibles**: nunca pueden encerrar al jugador.

**Fase 7 – Generación procedural.** Hash entero (seed, nivel, área, generación, slot) → PRNG xorshift. Posiciones = anclas (lugares donde el jugador estuvo de pie) + desplazamientos deterministas, revalidados contra la colisión real cada vez. Plantillas: escalera (6 cajas, crea una ruta vertical nueva), muro (4), pilar (3).

**Fase 8 – Persistencia de mutaciones.** Se guarda el *plan* (op + parámetro de 1 byte) y las anclas por área, no los objetos: 47 bytes por área. Al volver a entrar, el plan reconstruye exactamente el mismo mundo (probado bit a bit tras un reinicio simulado). El área se "repiensa" cada 1–3 visitas según curiosidad/paciencia; los cambios aparecen en la **siguiente** visita (la IA "piensa en ti cuando te vas").

## 9. Pruebas y medidas

`build_host.bat` compila el núcleo exacto de la ROM + un mundo simulado (`tests/mock_world.c`: suelo, franja de lava, meseta de 600 u, foso, habitación de techo bajo, EEPROM 2 KiB con inyección de fallos) y tres jugadores sintéticos deterministas.

Resultados (`tests/test_results.txt`):

| Jugador sintético | Ánimo final | Rasgo dominante | Lo que construye la IA |
|---|---|---|---|
| Explorador (recorre todo el mapa) | CURIOUS | curiosidad +16 (los otros ≤ +2) | rastros de monedas, secretos, monedas recolocadas hacia zonas sin explorar |
| Torpe (se queda quieto, muere cada visita) | PROTECTIVE | benevolencia +91, paciencia +48 | regalos (corazón/1-UP), ambiente tranquilo, secretos |
| Farmeador (bucle de 3 celdas, acrobacias, estrellas) | PLAYFUL | juego +48, amenaza +31 | escaleras de cajas, muros sobre su ruta repetida |

Persistencia: corte de corriente simulado tras cada uno de los posibles bloques de un commit → siempre se carga la última copia buena. Bloques 0–63 (partida original) verificados intactos tras toda la simulación. Misma entrada → misma memoria y misma imagen EEPROM byte a byte.

Medidas (`benchmarks/bench_results.txt`). **Son tiempos de PC x64, no de N64**; lo que sí se traslada son los recuentos:

| Medida | Valor host | Qué significa para N64 |
|---|---|---|
| `living_update` | 0,033 µs/frame de media | trabajo por frame = comparaciones; la muestra pesada ocurre 1 de cada 15 frames |
| Sondeos de suelo en régimen estable | 0,05 por frame | solo eventos; `find_floor` es la operación cara del motor |
| Entrar en área (aplicar plan) | 13,4 sondeos de suelo de media | una vez por carga de área |
| Salir de área (reflexión) | 7,3 µs | una vez por descarga |
| Commit EEPROM | 15,6 bloques de media (de 82) gracias al diff | ≈ 16 frames de goteo, 1 bloque (8 B) por frame |
| RAM total | 3 888 B (layout x64) | memoria 656 + tracker 530 + mutación 278 + eventos 46 + guardado 2 336 |

El pico de 138 µs en un frame aislado es planificación del sistema operativo Windows (no se repite entre ejecuciones; otras dieron 1,7 µs).

## 10. Compilación N64 y pruebas en emulador — ✅

**Entorno (2026-11-16):** WSL2 + Ubuntu 24.04, `binutils-mips-linux-gnu` 2.42, IDO 5.3 (recompilación estática incluida en el decomp). Se compila en un espejo dentro de Linux (`/root/slr`), porque compilar sobre OneDrive desde WSL es muy lento: `tools/wsl_mirror_build.sh`.

### Compilación

| Comprobación | Resultado |
|---|---|
| Juego original desde el decomp (`LIVING_ROM=0 COMPARE=1`) | SHA-1 `9bef1128…`: **idéntico** a la ROM del usuario → toolchain exacto |
| Original **con los 9 ficheros del motor parcheados** y `LIVING_ROM=0` | SHA-1 `9bef1128…`: **idéntico** → los hooks desaparecen por completo |
| ROM Living (IDO 5.3, `NON_MATCHING=1`) | 0 errores, 0 avisos en el código de la IA |
| Tamaño en la ROM | código 41,1 KB (35,0 núcleo + 6,1 integración), datos 240 B |
| RAM (BSS) | 4 128 B (`gLiving` 3 888 B + handles 240 B) |
| Margen antes del segmento `engine` | 24,4 KB (el linker lo comprueba con `ASSERT`) |

ROM final: `builds/sm64_living_20261116_113547.us.z64` (SHA-1 `79402812…`), con su entrada RDB en el `.rdb.txt` de al lado.

### Autoprueba en Vortex64

`make LIVING_SELFTEST=1` produce una variante que: aprende de las demos del título (que juegan solas), no tiene periodo inactivo, y escribe métricas en los bloques EEPROM 252–255 (fuera de los 82 bloques que usa cada copia). `tools/run_selftest.ps1` la ejecuta en una **copia aislada** de Vortex64 (`builds/emu`, con APPDATA/LOCALAPPDATA propios y la entrada RDB de 16K), y `tools/read_living_eep.py` decodifica el `.eep` resultante.

Resultados de dos sesiones de 5 minutos (apagar y encender entre ambas):

| | Sesión 1 | Sesión 2 |
|---|---|---|
| Carga de la memoria | `FRESH` | `OK` (sesión 2 cargada de la EEPROM) |
| Niveles observados | Bowser 1, WF, CCM, BBH | los mismos, con `returns=1` |
| Personalidad | CURIOUS, curiosidad +55 | CURIOUS, curiosidad +53, paciencia +24 |
| Mutaciones aplicadas / rechazadas | 0 / 0 (los cambios llegan en la siguiente visita) | **5 / 1** (rechazada por la geometría real) |
| Eventos | FIRST_CONTACT | + WELCOME_BACK |
| Aprendizaje | — | afinidad COIN_TRAIL 128→140 (las demos recogieron las monedas), SECRET 126 |
| Copias EEPROM | A seq 3, B seq 4, CRC válido | A seq 7, B seq 8, CRC válido |

Rendimiento medido **dentro de la ROM** con `osGetCount()` (46,875 MHz). La precisión depende de cómo emula Vortex64 el registro COUNT; hay que confirmarlo en hardware real:

| Medida | Valor | % de un frame de 30 fps |
|---|---|---|
| `living_update`, media | 7,9 µs | 0,024 % |
| `living_update`, pico | 48 µs | 0,14 % |
| Entrada en área, pico por frame (plan aplicado de golpe, primera versión) | 5,3 ms | 15,9 % |
| Entrada en área, pico por frame (**una op por frame**, versión actual) | 3,3 ms | 9,9 % (una vez por entrada, durante el fundido) |

### Bugs que solo aparecieron en la prueba real (corregidos)

1. **Las salidas de nivel no pasan por `unload_area()`**: el script del nivel ejecuta `CLEAR_LEVEL`, que destruye objetos y áreas directamente. La IA nunca evaluaba ni guardaba. Arreglo: hook también al inicio de `level_cmd_clear_level()` (`src/engine/level_script.c`).
2. **El guardado goteado solo avanzaba durante el juego**: en menús/transiciones se paraba. Arreglo: `living_hook_frame()` en el bucle principal (`game_init.c`) escribe un bloque por frame en cualquier pantalla.
3. **Pico de 5,3 ms al aplicar el plan**. Arreglo: la entrada se reparte en etapas (adquirir ranura → una op por frame → eventos).
4. **Los menús ocupaban ranuras de memoria de área**. Arreglo: la ranura se asigna en el primer frame de juego real (nueva prueba host).
5. **Finales de línea CRLF** introducidos por parches desde Windows rompían el preprocesador de IDO. Todo el proyecto está ahora en LF.

Además, `apply_living_hooks.py` tenía el hook de muerte con el ancla desajustada (duplicaba `s32 val04`); corregido. El clon contiene un parche ajeno en `tools/ido-static-recomp/libc_impl.c` (arreglo para MSYS2/Cygwin, 15-11 19:52) que no afecta a Linux; se ha dejado tal cual. Vortex64 (build de depuración) a veces se queda colgado al inicializar los plugins, antes de ejecutar la ROM; `run_selftest.ps1` lo detecta y relanza.

### Primera partida real (usuario, 2026-11-16, Vortex64 con interfaz)

Bob-omb Battlefield, Rey Bob-omb derrotado y estrella conseguida, 262 s de juego. Memoria leída (`builds/runs/user_play_20261116_bob.eep`): ánimo OBSERVING; curiosidad +23, paciencia +25, juego −40; 29 celdas exploradas en Bob-omb; 4 bucles de recorrido detectados → plan **BOX_WALL** para la próxima visita. Copias A/B válidas, partida original intacta.

Hallazgos:
- **La versión con interfaz de Vortex64 escribe la EEPROM al disco solo al cerrar la partida** (la versión de línea de comandos escribe al momento). Si se mata el proceso, se pierde lo jugado.
- **Semilla idéntica en todos los arranques** (`847CC4F8`, la misma que en la autoprueba): el contador de CPU al arrancar es constante en emulador. Corregido: una memoria nueva toma la semilla del primer frame de juego real (depende del tiempo que el jugador pasa en menús) y queda congelada en cuanto la IA ve un área. 2 pruebas host nuevas.
- El lector `.eep` daba "4K / copia B ausente" porque el emulador no rellena el fichero hasta 2 KiB; corregido.
- La visita en curso no se guarda si se apaga dentro del nivel (la IA guarda al salir). Pendiente: guardar también al pausar.

ROM corregida: `builds/sm64_living_20261116_120052.us.z64`. La memoria del usuario se ha copiado a la carpeta de guardado de esa ROM (Vortex64 la nombra con el MD5 de la imagen en orden de 32 bits invertido).

### Segunda partida real (sesión 2, ROM `120052`)

Memoria traspasada desde la sesión 1 y cargada correctamente (`sessions=2`, seq 7/8). En Bob-omb **se aplicó el muro de cajas** planeado (1 mutación, 0 rechazos) y la IA registró que el jugador interactuó con él: afinidad BOX_WALL 128 → 134. Ánimo OBSERVING → **CURIOUS** (curiosidad +46). Al salir repensó Bob-omb (generación 2): **SECRET + COIN_TRAIL** (un 1-UP escondido y un rastro de monedas hacia zonas sin pisar); también planeó el interior del castillo (COIN_TRAIL + SECRET). Guardado: `builds/runs/user_play_20261116_session2.eep`.

### Ampliación 2026-11-16 (tarde): pasillos, personalización en vivo, límite de 1 MiB

- **Cambios de pasillo** (`src/world/passage_shifter.c`): muy de vez en cuando (nunca antes de la sesión 3, ≥3 sesiones entre cambios, 12–24 % por sesión), durante una sesión entera una puerta entre zonas o un cuadro lleva a otro sitio. Destinos: visitados **o nuevos pero ya alcanzables** según los flags de puertas abiertas del guardado (`lw_destination_reachable`), para no saltar llaves/puertas de estrellas ni encerrar a Mario. Solo RAM, se restaura al descargar el área. Sin aviso.
- **Personalización en vivo y en sitios nunca visitados**: a los ~10 s de entrar en un área nueva y luego ≤1 por minuto (máx. 2 por visita), sin salir del nivel; usa como anclas las monedas que el propio nivel tiene en celdas no exploradas; aparece a ≥1500 u de Mario; se guarda en el plan y se hace commit al momento.
- **Guardado v2** (+4 bytes al final); los guardados v1 se migran (probado con la memoria real del usuario).
- **Bug grave encontrado en el emulador y corregido:** el IPL3 de la consola solo copia **1 MiB** del segmento principal; el código de la IA lo superó (la ROM normal por 784 B). Lo que quedaba fuera era basura → cuelgues que dependían del tamaño. Arreglo: `main_func` carga el resto por DMA PI antes de nada (`apply_living_hooks.py`). Se verifica con `tools/wsl_segments.sh`.
- **Bug corregido:** `lw_find_existing` pasaba la dirección segmentada del comportamiento a `get_object_list_from_behavior` → lista de objetos basura → bucle infinito al buscar anclas en vivo. Localizado con trazas en EEPROM (`LIVING_TRACE`, solo autoprueba).
- Autoprueba final: 191 s sin fallos, 2 mutaciones en vivo en primeras visitas, ánimo CURIOUS. Host: 79/79. ROM: `builds/sm64_living_20261116_135236.us.z64`.

### Tercera partida real (sesión 3, ROM `135236`)

Memoria v1 migrada a v2 sin pérdidas (seq 21/22, CRC válidos). 744 s de juego acumulado, 18 reflexiones, ánimo CURIOUS (curiosidad +54, amenaza −55). **15 mutaciones aplicadas** (4 rechazadas por el terreno real) en 5 áreas, incluidas áreas visitadas por primera vez en esta sesión (WF, patio del castillo). Eventos WELCOME_BACK y **SECRET_FOUND** (el jugador encontró el 1-UP escondido). Aprendizaje: COIN_TRAIL 152, SECRET 132, BOX_WALL 134, RELOCATE 124. Tras 2 muertes (BoB, WF) el plan de BoB incluye GIFT. Sin cambio de pasillo: la sesión 3 es la primera elegible y la probabilidad era ~12 %. Guardado: `builds/runs/user_play_20261116_session3.eep`.

### v1.1 (2026-11-16): de "parece corrupta" a estructuras de nivel

Feedback del usuario tras jugar: "parecía una ROM corrompida, ponía monedas donde sea, no modificaba bloques ni escaleras". Cambios:
- Eliminados el tinte de pantalla completa (`LFX_TINT` ya no dibuja) y la recolocación de monedas del nivel. Rastro de monedas: 4 monedas, en el suelo, solo como recurso secundario.
- Estructuras guiadas por el terreno (`geometry_mutator.c`): `lgeo_stairs_to_ledge` (escalera hasta un saliente real 250–800 u más alto), `lgeo_bridge` (puente a ras de suelo sobre hueco/foso/lava; las cajas no tienen gravedad), `lgeo_tower` (plataforma 2×2 elevada con premio, busca en 8 direcciones × 3 distancias). Cadena puente → escalera → torre. Los ids de op se reutilizan (BRIDGE = antiguo RELOCATE, TOWER = antiguo PILLAR) para no cambiar el formato de guardado.
- Decisión: +30 a estructuras si el nivel tiene cargados los modelos de caja (`script_func_global_1`: casi todos los cursos y exteriores del castillo, no el interior).
- **Verificación visual** por primera vez: capturas automáticas de la ventana del emulador (`run_selftest.ps1 -ShotEvery`). Se ven las torres de corcho en WF y CCM integradas en el escenario. Diagnóstico que lo hizo posible: la torre solo probaba un punto y casi siempre caía en pared/valla.
- Host: 87/87. ROM `builds/sm64_living_20261116_153952.us.z64` (SHA-1 86f6d320…). Parche `release_bps/SM64-Living-ROM-v1.1.bps` verificado.

### v1.2 (2026-11-16): texturas del nivel, habitaciones nuevas, música y escala

- **Modelos propios** generados por `tools/gen_living_models.py` → `src/integration/living_models.inc.c`: bloque sólido 200³ y habitación 1040×1040×640 (suelo, 4 paredes con puerta, techo y tejado transitable), con colisión cerrada por losas (misma plantilla/winding que la caja de SM64). Ids de modelo 0xF0/0xF1; comportamientos propios escritos como comandos crudos y pasados como segmento 0.
- **Texturas del nivel**: los `gsDPSetTextureImage` apuntan al segmento 0x09 (paquete del nivel cargado); al cargar un área se parchean las tres texturas (pared, suelo, tejado) según una tabla de 12 paquetes elegida mirando `builds/texture_banks.png`. Funciona también dentro del castillo (paquete `inside`).
- **Op ROOM** (reutiliza el id del antiguo rastro de monedas): busca 8 direcciones × 3 distancias un terreno llano, vacío y con suelo delante de la puerta; puerta orientada hacia el ancla; premio dentro.
- **Ambiente sutil**: música al 90 % de volumen y, a veces, tempo 90 % (82 % si el ánimo es inquietante; se reaplica si la secuencia lo cambia), y hasta 4 árboles reescalados (70–140 %, con rollback).
- **Visto en el emulador**: escalera de bloques con textura de troncos en CCM, edificio de ladrillo marrón de WF junto al inicio de WF.
- Host 94/94. ROM `sm64_living_20261116_160310` (SHA-1 2cf5b402…), parche `release_bps/SM64-Living-ROM-v1.2.bps`.
- **Límite de memoria cercano**: al segmento principal le quedan ~3,8 KB antes del segmento `engine` (además supera el MiB del IPL3 en 14 KB, cubierto por el cargador de cola). Más crecimiento exige mover la IA a un segmento propio.

## v1.3 — solo en Vortex64 Living

- **Vortex64 Living** (`vortex64_living_src/`, clon de Vortex64-Windows b5d1c0f): `RomMemoryHandler::Read32` responde una firma en PI `0x1FB00000–0x1FB000FF` (`x = a·0x9E3779B1; x ^= x>>15; x *= 0x85EBCA77; x ^= x>>13; x ^ 'VX64'`), también en la ruta de dirección constante del recompilador x86. En cualquier otro sitio esa dirección da el patrón de bus abierto.
- **ROM**: `living_hook_boot` lee 4 palabras con `osPiRawReadIo` (dirección `osRomBase | dev`, que el recompilador no puede plegar; 3 intentos). Si no coinciden, dibuja con la CPU en `gFramebuffer0` (fuente 5×7 propia, ×2) "ESTA COPIA SOLO FUNCIONA EN VORTEX64 LIVING" y bloquea el hilo del juego. Las builds de autoprueba (`LIVING_SELFTEST`) no comprueban.
- **Arreglos del emulador**: `vortex64.exe` como `WIN32_EXECUTABLE` (sin CMD); GLideN64 se copia a `bin/` en la compilación (antes solo iba en el ZIP); teclas: Qt en Windows da el scancode set-1 (= código evdev en el bloque principal, tabla para las extendidas) y se le restaba 8 como si fuera un keycode de X (`NativeKeyEvdev.h`); carpetas propias `vortex64-living`; entrada RDB `[D7176DB3-AF4D38B6-C:45]` con EEPROM 16K.
- `tools/vx_migrate_keys.py` convierte teclas guardadas con el fallo (deja las que conservan su valor por defecto).
- **Visto**: GLideN64 en la lista de plugins y desplegable con los dos; W/A/S/D bien nombradas y una pulsación real de T capturada como "T"; ROM parcheada arrancando en Vortex64 Living (GLideN64) y pantalla azul en Vortex64 1.0.0. Host 94/94. ROM `sm64_living_20261116_162947` (SHA-1 8dee7196…), parche `CARPETA_FINAL/Parche_BPS/SM64-Living-ROM-v1.3.bps`.

## Problemas abiertos y riesgos conocidos

- **Sin verificación visual.** Las pruebas leen la memoria de la IA y las métricas, pero nadie ha *mirado* la pantalla: tinte ambiental (`gDPFillRectangle` 1-ciclo + `G_RM_XLU_SURF`), altura de las monedas generadas (+20 u), escaleras de cajas y el 1-UP secreto. Siguiente paso: jugar la ROM a mano.
- **Las demos solo ejercitan parte del sistema**: no mueren, no usan la cámara y visitan 4 niveles. Las estructuras de cajas, emboscadas, regalos y estados PROTECTIVE/TESTING/UNSETTLING solo están probados en PC.
- **Tiempos del emulador, no del hardware.** COUNT en Vortex64 es una aproximación; confirmar en consola real o con un emulador de ciclo exacto (ares).
- **Pico de 3,3 ms al entrar en un área.** Aceptable (ocurre durante la transición), pero se puede bajar a "un objeto por frame" si en hardware real se nota.
- **EEPROM 16K exige configuración en el emulador/flashcart** (entrada RDB). Sin ella, la IA funciona solo en RAM y el juego no se ve afectado. Falta la cabecera "advanced homebrew" para la detección automática (pendiente de verificar la especificación).
- **Una IA por cartucho**, no por fichero A–D: no caben cuatro memorias en 16 Kbit junto a la original.
- **"Conexiones entre espacios / disposición de habitaciones"** se implementan con módulos de colisión (cajas) y rutas (monedas/enemigos). Cambiar warps o la geometría estática exige variantes de nivel en tiempo de compilación; queda como siguiente paso deliberado.
