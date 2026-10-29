#include <assert.h>
#include <string.h>
#include "core/cwv_mime.h"
int main(void) {
    assert(!strcmp(cwv_mime_from_name("index.HTML"), "text/html"));
    assert(!strcmp(cwv_mime_from_name("image.svg"), "image/svg+xml"));
    assert(!strcmp(cwv_mime_from_name("clip.MP4"), "video/mp4"));
    assert(!strcmp(cwv_mime_from_name("track.ogg"), "audio/ogg"));
    assert(!strcmp(cwv_mime_from_name("notes.txt"), "text/plain"));
    assert(!strcmp(cwv_mime_from_name("page.xhtml"), "application/xhtml+xml"));
    assert(!strcmp(cwv_mime_from_name("no-extension"), "application/octet-stream"));
    assert(cwv_mime_kind("image/png") == CWV_MIME_IMAGE);
    assert(cwv_mime_kind("audio/mpeg") == CWV_MIME_AUDIO);
    assert(cwv_mime_kind("video/webm") == CWV_MIME_VIDEO);
    assert(cwv_mime_kind("text/plain") == CWV_MIME_TEXT);
    assert(cwv_mime_kind("application/xhtml+xml") == CWV_MIME_XHTML);
    return 0;
}
