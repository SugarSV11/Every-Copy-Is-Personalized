/*
 * Storage backend: 8-byte EEPROM blocks. Implemented by integration/living_sm64.c
 * (osEepromRead / osEepromWrite) and by tests/mock_world.c (RAM + fault injection).
 * memory_storage.c wraps the backend with the hard rule that blocks below
 * LIVING_EEP_FIRST_BLOCK (the original SM64 save) can never be written.
 */
#ifndef LIVING_MEMORY_STORAGE_H
#define LIVING_MEMORY_STORAGE_H

#include "../living_types.h"

#define LSTORE_OK 0
#define LSTORE_ERR_RANGE (-10)
#define LSTORE_ERR_NODEV (-11)

/* backend (provided by the platform) */
s32 lstore_backend_present(void);              /* 1 if a 16 Kbit EEPROM answered */
s32 lstore_backend_read(u8 block, u8 *buf8);
s32 lstore_backend_write(u8 block, const u8 *buf8);

/* guarded API used by the save manager */
s32 lstore_present(void);
s32 lstore_read(u8 block, u8 *buf8);
s32 lstore_write(u8 block, const u8 *buf8);
u16 lstore_rejected_writes(void);

#endif
