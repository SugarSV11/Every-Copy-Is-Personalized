@echo off
rem Host build of the Living ROM core (logic tests + benchmarks). Requires VS 2022 Build Tools.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist builds\host mkdir builds\host
set CORE=src\living.c src\ai\behavior_tracker.c src\ai\player_memory.c src\ai\personality_engine.c src\ai\decision_engine.c src\world\procedural_generator.c src\world\mutation_engine.c src\world\geometry_mutator.c src\world\object_mutator.c src\world\event_system.c src\world\passage_shifter.c src\persistence\save_manager.c src\persistence\memory_storage.c
set FLAGS=/nologo /W4 /WX /O2 /DLIVING_HOST /D_CRT_SECURE_NO_WARNINGS /Fobuilds\host\
cl %FLAGS% %CORE% tests\mock_world.c tests\sim_player.c tests\test_living.c /Febuilds\host\test_living.exe || exit /b 1
cl %FLAGS% %CORE% tests\mock_world.c tests\sim_player.c benchmarks\bench_living.c /Febuilds\host\bench_living.exe || exit /b 1
echo BUILD OK
