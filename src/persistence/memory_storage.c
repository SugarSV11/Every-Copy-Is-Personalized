#include "memory_storage.h"

static u16 sRejected;

s32 lstore_present(void) {
    return lstore_backend_present();
}

s32 lstore_read(u8 block, u8 *buf8) {
    if (block < LIVING_EEP_FIRST_BLOCK) {
        return LSTORE_ERR_RANGE;
    }
    return lstore_backend_read(block, buf8);
}

s32 lstore_write(u8 block, const u8 *buf8) {
    /* hard firewall: the original SM64 save (blocks 0..63) is never touched */
    if (block < LIVING_EEP_FIRST_BLOCK) {
        sRejected++;
        return LSTORE_ERR_RANGE;
    }
    return lstore_backend_write(block, buf8);
}

u16 lstore_rejected_writes(void) {
    return sRejected;
}
