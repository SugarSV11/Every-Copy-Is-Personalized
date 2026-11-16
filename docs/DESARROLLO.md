# SM64 Living ROM Engine

IA local para Super Mario 64 (US) que observa al jugador, desarrolla una personalidad,
modifica el mundo (rutas, estructuras, objetos, NPC, ambiente, eventos) y lo recuerda
entre sesiones. Sin red, sin LLM, sin redes neuronales: máquinas de estados, utilidad,
memoria persistente y generación determinista, en C89 sin `malloc`.

**Estado:** fases 1–10 completadas. La ROM compila con IDO 5.3 y se ha ejecutado en
Vortex64: la IA observa, desarrolla personalidad, muta el mundo y recuerda entre
sesiones (EEPROM 16K). Coste medido en la ROM: ~8 µs/frame. Falta la verificación
visual jugando a mano. Detalle: [docs/FASES.md](docs/FASES.md).

ROM lista: `builds/sm64_living_20261116_135236.us.z64` (necesita la entrada de su
`.rdb.txt` en `Config/Project64.rdb` para guardar la memoria de la IA).

```
rom/            baserom.us.z64 (copia verificada; el original del Escritorio no se toca)
sm64/           clon de n64decomp/sm64 con los hooks aplicados (git diff para verlos)
src/            núcleo de la IA (ai/, world/, persistence/) + integration/ con el motor
tests/          mundo simulado, jugadores sintéticos, batería de pruebas, resultados
benchmarks/     microbenchmarks host y resultados
tools/          apply_living_hooks.py, wsl_mirror_build.sh, run_selftest.ps1, read_living_eep.py…
builds/         binarios host y ROMs generadas (nunca se sobrescriben)
build_host.bat  compila tests + benchmarks con MSVC
```

## Probar en PC

```bat
build_host.bat
builds\host\test_living.exe
builds\host\bench_living.exe
```

## Compilar la ROM (WSL, Ubuntu 24.04)

```bash
wsl -d Ubuntu-24.04 -u root -- bash tools/wsl_mirror_build.sh
```

Autoprueba en el emulador: `wsl ... wsl_mirror_build.sh LIVING_SELFTEST=1` y luego
`powershell tools/run_selftest.ps1`.

No se distribuye ninguna ROM ni asset de Nintendo.
