<div align="center">

# 🍄 Every Copy Is Personalized

**Un hack de Super Mario 64 con una IA dentro del propio cartucho: observa cómo juegas, cambia el mundo y lo recuerda.**

![Estado](https://img.shields.io/badge/estado-beta-orange)
![Versión](https://img.shields.io/badge/versi%C3%B3n-1.4--beta-blue)
![Plataforma](https://img.shields.io/badge/N64-ROM%20hack-red)
![Tests](https://img.shields.io/badge/tests-101%20OK-brightgreen)
![Lenguaje](https://img.shields.io/badge/IA-C89%20sin%20malloc-lightgrey)

🇪🇸 Español · [🇬🇧 English](README.en.md)

</div>

> ⚠️ **Es una beta**: puede tener fallos.
> Recomendado: [Vortex64](https://sugarsv11.github.io/vortex64-site/), que ya trae la entrada de base de datos con la EEPROM de 16 Kbit. **Funciona en cualquier emulador o consola**: si no hay EEPROM de 16K, el juego arranca igual y la IA trabaja solo en RAM (olvida al apagar).

## 📸 Capturas

> Pendiente: los GIFs se añadirán en [`docs/img/`](docs/img/).

| Escaleras y puentes | Habitaciones nuevas | Segunda sesión |
|:--:|:--:|:--:|
| _Pendiente_ (`docs/img/01-escaleras.gif`) | _Pendiente_ (`docs/img/02-habitacion.gif`) | _Pendiente_ (`docs/img/03-segunda-sesion.gif`) |

## 🚀 Cómo jugar

1. Descarga **`Every-Copy-Is-Personalized-v1.4-beta.bps`** desde [Releases](../../releases).
2. Aplícalo a **tu propia copia** de *Super Mario 64 (USA)* en `.z64` (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`) con [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/), Floating IPS o `python tools/bps_tool.py apply`.
3. Abre la ROM parcheada en tu emulador o flashcart.

Este repositorio **no contiene la ROM ni datos de Nintendo**: solo el parche y el código fuente del hack.

## 🕹️ Emuladores y consola

La ROM declara una EEPROM de 16 Kbit en su cabecera (id `ED`, byte `0x3F = 0x20`), así que los emuladores modernos la eligen solos.

| Plataforma | Estado | Notas |
|---|:--:|---|
| [Vortex64](https://sugarsv11.github.io/vortex64-site/) | ✅ Probado en v1.3 · v1.4 _Pendiente_ | La v1.3 (con candado) se probó con guardado; la v1.4 sin candado aún no |
| ares | ⏳ Pendiente | |
| RMG / mupen64plus | ⏳ Pendiente | |
| Consola con EverDrive | ⏳ Pendiente | |

Si encuentras un fallo en alguno, abre un [issue](../../issues) indicando emulador y versión.

## 🧠 Cómo funciona la IA

> **Importante:** la "IA" **no es un modelo de lenguaje ni una red neuronal**. Es un sistema de decisiones escrito en C: contadores, reglas, utilidades con pesos y un generador pseudoaleatorio determinista. Cabe en la ROM, no usa red y cuesta ~8 µs por frame (medido en el emulador).

```text
 ┌──────────┐   ┌───────────┐   ┌──────────────┐   ┌───────────┐   ┌─────────────┐
 │ OBSERVA  │ → │  RECUERDA │ → │ PERSONALIDAD │ → │  DECIDE   │ → │ CAMBIA EL   │
 │ posición │   │ memoria   │   │ curiosa,     │   │ utilidad  │   │ MUNDO       │
 │ muertes  │   │ por nivel │   │ protectora,  │   │ + semilla │   │ bloques,    │
 │ monedas  │   │ y rasgos  │   │ juguetona…   │   │ aprendida │   │ salas, ruta │
 └──────────┘   └───────────┘   └──────────────┘   └───────────┘   └──────┬──────┘
       ▲                                                                  │
       └──────────── EEPROM 16K (copias A/B + CRC) ◄──── guarda ◄─────────┘
```

| Etapa | Qué hace | Código |
|---|---|---|
| Observar | Dónde vas, dónde mueres, qué evitas, cuánto exploras | `src/ai/behavior_tracker.c` |
| Recordar | Rasgos y memoria por zona entre sesiones | `src/ai/player_memory.c` |
| Personalidad | Estado de ánimo que cambia según tu estilo | `src/ai/personality_engine.c` |
| Decidir | Elige operaciones por afinidad y aprende de lo que funciona | `src/ai/decision_engine.c` |
| Cambiar el mundo | Estructuras, habitaciones, regalos, pasillos que a veces llevan a otro sitio, música y escala | `src/world/` |
| Guardar | Dos copias con CRC, versión y recuperación; no toca tus partidas | `src/persistence/` |

Los primeros minutos solo observa; después de un tiempo despierta y empieza a cambiar cosas, de forma sutil.

## 🔨 Compilar

| Entorno | Comando |
|---|---|
| Linux nativo | `bash tools/build_linux.sh` (añade `--bps` para crear el parche) |
| Windows + WSL | `wsl -d Ubuntu-24.04 -u root -- bash tools/wsl_mirror_build.sh` |
| Tests en PC (MSVC) | `build_host.bat` y luego `builds\host\test_living.exe` |

Necesitas tu propia ROM en `rom/baserom.us.z64`. Detalles en [`docs/DESARROLLO.md`](docs/DESARROLLO.md).

## 📂 Código

| Carpeta | Qué hay |
|---|---|
| `src/` | La IA en C89 sin `malloc`: `ai/`, `world/`, `persistence/`, `integration/` |
| `decomp_patch/` | Cambios al motor sobre [n64decomp/sm64](https://github.com/n64decomp/sm64) (commit `9921382`) |
| `tools/` | `bps_tool.py`, scripts de compilación (Linux/WSL), lector de guardados |
| `tests/`, `benchmarks/` | 101 pruebas en PC con un mundo simulado |
| `docs/` | Informe por fases (`FASES.md`) y guía de desarrollo |

## 🔗 Enlaces

- 🌐 Web de Vortex64: https://sugarsv11.github.io/vortex64-site/
- 🪟 Vortex64 para Windows: https://github.com/SugarSV11/Vortex64-Windows

---

*Super Mario 64 es una marca de Nintendo. Proyecto de fans sin ánimo de lucro, sin relación con Nintendo.*
