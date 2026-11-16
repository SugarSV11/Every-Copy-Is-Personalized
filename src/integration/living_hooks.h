/*
 * Hooks called from the SM64 decompilation (only when built with LIVING_ROM=1).
 * Each hook is a single call inserted in an existing engine function; see
 * docs/INTEGRACION.md for the exact call sites.
 */
#ifndef LIVING_HOOKS_H
#define LIVING_HOOKS_H

#ifdef LIVING_ROM
void living_hook_boot(void);             /* game_init.c: after save_file_load_all() */
void living_hook_area_loaded(void);      /* area.c: end of load_area() */
void living_hook_area_unloading(void);   /* area.c: start of unload_area(); level_script.c: start of
                                            level_cmd_clear_level() (level exits skip unload_area) */
void living_hook_update(void);           /* level_update.c: play_mode_normal(), after area_update_objects() */
void living_hook_death(void);            /* level_update.c: level_trigger_warp() death/void warps */
void living_hook_game_saved(void);       /* save_file.c: end of save_file_do_save() */
void living_hook_render(void);           /* area.c: render_game(), after the 3D scene, before the HUD */
void living_hook_frame(void);            /* game_init.c: main loop, after level_script_execute() */
#endif

#endif
