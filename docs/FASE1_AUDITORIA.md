# FASE 1 — Auditoría de ROM, herramientas y entorno

Fecha: 2026-11-15. Nada original fue modificado. Toda la salida está en `SM64-Living-ROM/`.

## 1. ROM encontrada

| Campo | Valor |
|---|---|
| Archivo | `Desktop\Super Mario 64 (USA).z64` (sin tocar) |
| Copia de trabajo | `SM64-Living-ROM\rom\baserom.us.z64` |
| Tamaño | 8 388 608 bytes (8 MiB / 64 Mbit) |
| Formato | `.z64` big-endian nativo (magic `80 37 12 40`) |
| Título interno | `SUPER MARIO 64` |
| Código de juego | `NSME` → N (cartucho), SM (Super Mario 64), E (USA) |
| Revisión | 0 |
| CRC1 / CRC2 | `635A2BFF` / `8B022326` |
| MD5 | `20B854B239203BAF6C961B850A4A51A2` |
| SHA-1 | `9BEF1128717F958171A4AFAC3ED78EE2BB4E86CE` |
| SHA-256 | `17CE077343C6133F8C9F2D6D6D9A4AB62C8CD2AA57C40AEA1F490B4C8BB21D91` |

El SHA-1 coincide con el que exige la decompilación pública `n64decomp/sm64` para
`baserom.us.z64` (versión US). Es una base válida para extraer assets y obtener un build "matching".

Hay una segunda copia idéntica (mismo SHA-1) en `Downloads\`. No se ha tocado.

Nota: el archivo del Escritorio tiene atributo `ReparsePoint` (OneDrive). Se leyó correctamente;
conviene mantener la copia de trabajo fuera de la sincronización si da problemas de bloqueo al compilar.

## 2. Código fuente decompilado

**No existe** ningún proyecto decompilado de SM64 en el Escritorio ni en el perfil del usuario
(búsqueda de carpetas `*sm64*` hasta profundidad 3).

Base legítima propuesta: `https://github.com/n64decomp/sm64` (código C decompilado, sin assets;
los assets se extraen localmente de la ROM propia del usuario mediante `extract_assets.py`).
No se ha descargado nada todavía: requiere autorización explícita.

## 3. Herramientas disponibles

| Herramienta | Estado |
|---|---|
| Python 3.12 | ✅ `AppData\Local\Programs\Python\Python312` |
| Git | ✅ |
| CMake | ✅ |
| MSVC 2022 Build Tools (cl.exe 14.44) | ✅ — compilador **host** x64, no genera código N64 |
| Vortex64 (emulador N64 propio, build MSVC debug) | ✅ `Vortex64-Windows\build\windows-msvc-debug\bin\vortex64-cli.exe` |
| WSL | ❌ no instalado |
| MSYS2 / make / gcc | ❌ |
| Toolchain MIPS (`mips-linux-gnu-*` o `mips64-elf-*`) | ❌ |
| Docker | ❌ |

## 4. Qué es técnicamente posible ahora

- **Sí, ya:** escribir los módulos de la IA en C portable (C89/C99 sin libc pesada, sin `malloc`,
  tipos de `ultratypes.h`) y probarlos/benchmarkearlos en el host con MSVC
  (tests de lógica, determinismo, validación de guardado, presupuestos de memoria en bytes).
- **No, todavía:** compilar una ROM. Falta (a) la decompilación y (b) un entorno Linux-like con
  binutils MIPS + `make`. Sin eso no hay build N64 ni pruebas en emulador.
- **Restricción de persistencia descubierta (importante para el diseño):** SM64 US usa EEPROM 4 Kbit
  = **512 bytes** en total. El juego ya usa 4 archivos × 2 copias (`struct SaveFile`, 0x38 bytes c/u)
  + `struct MainMenuSaveData` × 2 (0x20 c/u) = 0x200 bytes: **la EEPROM está llena**.
  Opciones: (1) cambiar el tipo de save a EEPROM 16 Kbit (2 KiB) en el header/`osEepromProbe`,
  dejando los 512 bytes originales intactos y usando bloques 64+ para la IA; (2) SRAM 32 KiB.
  Recomendado (1): mínimo cambio, compatible con emuladores y flashcarts. Esto se confirmará
  leyendo `src/game/save_file.c` una vez se tenga el código.

## 5. Pasos para desbloquear Fase 2

1. Instalar WSL2 + Ubuntu (lo hace el usuario: `wsl --install`, requiere admin y reinicio).
2. Dentro de Ubuntu: `sudo apt install build-essential git binutils-mips-linux-gnu python3 pkg-config zlib1g-dev libcapstone-dev`.
3. Clonar `n64decomp/sm64` dentro de `SM64-Living-ROM/sm64` (o en el FS de WSL por velocidad).
4. Copiar `rom/baserom.us.z64` a la raíz del repo y ejecutar `make VERSION=us -j`.
5. Verificar que `build/us/sm64.us.z64` tiene SHA-1 `9bef1128…` (build matching = toolchain correcto).
6. Solo después empezar a integrar módulos (Fase 3) con `NON_MATCHING=1`.

## 6. Problemas encontrados

- Sin toolchain N64 ni decomp: bloqueante para compilar ROM.
- EEPROM 4 Kbit llena: la persistencia de la IA exige ampliar el tipo de guardado.
