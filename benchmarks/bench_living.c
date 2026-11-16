/*
 * Host micro-benchmarks + operation counts.
 * IMPORTANT: wall-clock numbers here are x64 host numbers, NOT N64 numbers.
 * What transfers to N64 are the counts (calls, probes, blocks written, bytes),
 * which the cost model in docs/ uses. Real N64 timing must be measured in an
 * emulator/hardware build (osGetCount around living_update).
 */
#include <stdio.h>
#include <windows.h>
#include "../tests/sim_player.h"

static double now_us(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double) c.QuadPart * 1e6 / (double) f.QuadPart;
}

int main(void) {
    int v, f, frames = 90 * LIVING_FRAMES_PER_SEC;
    double t0, tUpd = 0, tUnload = 0, tApply = 0, worstFrame = 0;
    long updCalls = 0, sampleFrames = 0;
    u32 probesUpd = 0, probesApply = 0;
    s16 coins = 0, stars = 0;
    LivingInput in;
    u8 buf[700];
    u16 crc = 0;
    int i;

    mock_reset_eeprom();
    living_boot(1);
    gLiving.mem.playSec = LIVING_DORMANT_SEC;

    for (v = 0; v < 40; v++) {
        int profile = v % P_COUNT;
        u32 p0;
        sim_terrain();
        gMock.player.x = 0; gMock.player.z = 0;
        living_area_loaded(9, 1);
        coins = 0; stars = 0;
        for (f = 0; f < frames; f++) {
            double a, d;
            sim_input(profile, v, f, frames, &in, &coins, &stars);
            p0 = gMock.probes;
            a = now_us();
            living_update(&in);
            living_storage_tick();
            d = now_us() - a;
            if (f == 0) { tApply += d; probesApply += gMock.probes - p0; }
            else {
                tUpd += d; updCalls++;
                probesUpd += gMock.probes - p0;
                if (d > worstFrame) worstFrame = d;
                if (gLiving.trk.sampleTimer == 0) sampleFrames++;
            }
        }
        t0 = now_us();
        living_area_unloading();
        tUnload += now_us() - t0;
        mock_unload_area();
        lsm_flush(&gLiving.sm);
    }

    t0 = now_us();
    for (i = 0; i < 10000; i++) {
        u16 n = lsm_serialize(&gLiving.mem, buf, sizeof(buf));
        crc = (u16) (crc + lsm_crc16(buf, n, 0xFFFF));
    }
    printf("SM64 Living ROM - host benchmark (x64, NOT N64 timing)\n");
    printf("living_update        avg %.3f us/frame over %ld frames, worst %.1f us\n", tUpd / updCalls, updCalls, worstFrame);
    printf("  sample frames      %ld (1 every %d frames)\n", sampleFrames, LIVING_SAMPLE_PERIOD);
    printf("  floor probes       %.4f per steady frame (events only)\n", (double) probesUpd / updCalls);
    printf("area enter (apply)   avg %.2f us, %.1f floor probes per apply\n", tApply / 40, (double) probesApply / 40);
    printf("area exit (reflect)  avg %.2f us\n", tUnload / 40);
    printf("serialize+crc16      avg %.2f us (640 B) [crc %04X]\n", (now_us() - t0) / 10000, crc);
    printf("EEPROM blocks written over 40 visits: %u (skipped unchanged: %u), commits %u\n",
           gLiving.sm.blocksWritten, gLiving.sm.blocksSkipped, gLiving.sm.commits);
    printf("  -> avg %.1f blocks per commit; at 1 block/frame = %.1f frames of trickle per commit\n",
           (double) gLiving.sm.blocksWritten / gLiving.sm.commits, (double) gLiving.sm.blocksWritten / gLiving.sm.commits);
    printf("RAM: LivingState %u bytes\n", (unsigned) sizeof(LivingState));
    return 0;
}
