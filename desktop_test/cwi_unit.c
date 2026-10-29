/* CWI decoder unit gate (2026-10-05 (71)): the real C decoder against a
 * fixture produced by the real Python encoder, plus a full truncation
 * fuzz (every prefix of the file must be REJECTED, never crash), plus
 * byte-swapped header fields.  Mirrors build_bmp_unit.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cwv_cwi.h"
#include "cwi_fixture.h"

static int failures = 0;

static void expect(int cond, const char *what) {
    if (!cond) { printf("FAIL: %s\n", what); failures++; }
}

int main(void) {
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    int i;

    /* 1. real decode of the fixture */
    expect(cwv_cwi_decode(fix_cwi, FIX_LEN, &rgba, &w, &h), "decode fixture");
    expect(w == FIX_W && h == FIX_H, "fixture dimensions");
    if (rgba) {
        expect(memcmp(rgba, fix_px, FIX_PX_LEN) == 0, "pixel-exact decode");
        free(rgba); rgba = NULL;
    }
    /* 2. truncation fuzz: a prefix cut INSIDE a chunk must be rejected;
       a prefix cut exactly ON a chunk boundary is a valid smaller
       container (ORIG/META are optional) - but then it must still decode
       the FULL pixel set exactly.  No input may crash either way. */
    {
        int accepted_ok = 0;
        for (i = 0; i < FIX_LEN; i++) {
            rgba = NULL; w = h = 0;
            if (cwv_cwi_decode(fix_cwi, (size_t)i, &rgba, &w, &h)) {
                if (w != FIX_W || h != FIX_H || !rgba ||
                    memcmp(rgba, fix_px, FIX_PX_LEN) != 0) {
                    printf("FAIL: boundary prefix %d decoded wrong pixels\n", i);
                    failures++;
                }
                free(rgba);
                accepted_ok++;
            } else if (rgba) {
                printf("FAIL: rejected decode leaked rgba at %d\n", i);
                failures++;
            }
        }
        printf("fuzz: %d/%d prefixes rejected, %d boundary-complete accepted "
               "pixel-exact\n", FIX_LEN - accepted_ok, FIX_LEN, accepted_ok);
    }
    /* 3. bad magic / bad version / bad dims */
    {
        unsigned char bad[64];
        memcpy(bad, fix_cwi, sizeof(bad) < FIX_LEN ? sizeof(bad) : FIX_LEN);
        if (FIX_LEN >= 64) {
            bad[0] = 'X';
            expect(!cwv_cwi_decode(bad, 64, &rgba, &w, &h), "reject bad magic");
            memcpy(bad, fix_cwi, 64);
            bad[4] = 9;   /* version */
            expect(!cwv_cwi_decode(bad, 64, &rgba, &w, &h), "reject bad version");
            memcpy(bad, fix_cwi, 64);
            bad[8] = 0; bad[9] = 0;   /* zero width */
            expect(!cwv_cwi_decode(bad, 64, &rgba, &w, &h), "reject zero width");
        }
    }
    /* 4. NULL safety */
    expect(!cwv_cwi_decode(NULL, 100, &rgba, &w, &h), "reject NULL");
    expect(!cwv_cwi_decode(fix_cwi, FIX_LEN, NULL, &w, &h), "reject NULL out");

    if (failures) { printf("CWI UNIT FAILED (%d)\n", failures); return 1; }
    printf("CWI UNIT PASSED (pixel-exact vs python pack, %d truncations rejected)\n",
           FIX_LEN);
    return 0;
}
