# 🍄 SM64 Living ROM (BETA)

**Un hack de Super Mario 64 con una IA dentro del propio cartucho**: observa cómo juegas, desarrolla una personalidad, cambia el mundo (escaleras, puentes, torres, habitaciones nuevas con las texturas de cada nivel, pasillos que llevan a otro sitio, música y escala sutilmente distintas…) y **lo recuerda entre sesiones**. Cada copia acaba siendo distinta.

> ⚠️ **Es una beta**: puede tener fallos.
>
> ⚠️ **Solo funciona en [Vortex64](https://sugarsv11.github.io/vortex64-site/)** (Windows 1.1 o posterior). En cualquier otro emulador o en consola la ROM muestra *"ESTA COPIA SOLO FUNCIONA EN VORTEX64"* y no arranca.

## 🚀 Cómo jugar

1. Descarga **Vortex64** desde su web: **https://sugarsv11.github.io/vortex64-site/**
2. Descarga **`SM64-Living-ROM-v1.3.bps`** desde [Releases](../../releases).
3. Aplica el parche a **tu propia copia** de *Super Mario 64 (USA)* en `.z64`
   (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`) con [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/), Floating IPS o `python tools/bps_tool.py apply`.
4. Abre la ROM parcheada en Vortex64. **Ya está.**

Vortex64 está hecho para esta ROM: la reconoce y le da la memoria de 16 Kbit que necesita la IA para recordar. **No hay que configurar nada.**

Este repositorio **no contiene la ROM ni datos de Nintendo**: solo el parche (en Releases) y el código fuente del hack.

## 🧠 Qué hace la IA

- **Observa**: dónde vas, dónde mueres, qué evitas, cuánto exploras.
- **Personalidad**: curiosa, protectora, juguetona, inquietante… cambia según cómo juegas.
- **Cambia el mundo** (sin exagerar): estructuras de bloques con las texturas del nivel, habitaciones que nunca existieron, regalos, secretos, enemigos que cambian su ruta, pasillos que a veces llevan a otro destino (rara vez), música un poco más lenta, árboles de otro tamaño.
- **Recuerda**: guarda su memoria en la EEPROM (copias A/B con CRC, versiones y recuperación), junto a tus partidas normales, que no se tocan.

## 📂 Código

| Carpeta | Qué hay |
|---|---|
| `src/` | La IA en C89 sin `malloc`: `ai/` (observación, memoria, personalidad, decisiones), `world/` (cambios del mundo), `persistence/` (guardado), `integration/` (unión con el motor; `living_sm64.c` tiene la comprobación de Vortex64) |
| `decomp_patch/` | `sm64_living.patch`: cambios al motor sobre [n64decomp/sm64](https://github.com/n64decomp/sm64) (commit `9921382`) |
| `tools/` | `bps_tool.py` (crear/aplicar BPS), scripts de compilación (WSL), lector de guardados, autoprueba |
| `tests/`, `benchmarks/` | 94 pruebas en PC con un mundo simulado (`build_host.bat`, Visual Studio) |
| `docs/` | Informe técnico por fases (`FASES.md`) y guía de desarrollo (`DESARROLLO.md`) |

Para compilar: clona n64decomp/sm64 en `sm64/`, pon tu ROM en `rom/baserom.us.z64` y sigue `docs/DESARROLLO.md`.

## 🔗 Enlaces

- Vortex64 (web): https://sugarsv11.github.io/vortex64-site/
- Vortex64 para Windows: https://github.com/SugarSV11/Vortex64-Windows

*Super Mario 64 es una marca de Nintendo. Proyecto de fans sin ánimo de lucro, sin relación con Nintendo.*
