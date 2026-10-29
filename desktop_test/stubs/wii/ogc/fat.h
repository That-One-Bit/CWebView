/* desktop stub: fat.h - mirrors libogc's <fat.h> (the Wii app loop's
 * fatInitDefault call; the real one mounts sd: for stdio). */
#ifndef STUB_OGC_FAT_H
#define STUB_OGC_FAT_H

static inline int fatInitDefault(void) { return 1; }

#endif /* STUB_OGC_FAT_H */
