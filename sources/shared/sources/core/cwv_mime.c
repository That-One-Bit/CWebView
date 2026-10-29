#include "core/cwv_mime.h"
#include <ctype.h>
#include <string.h>

typedef struct CwvMimeMap { const char *ext; const char *type; } CwvMimeMap;

static const CwvMimeMap g_mime_map[] = {
    {"html", "text/html"}, {"htm", "text/html"},
    {"xhtml", "application/xhtml+xml"}, {"xht", "application/xhtml+xml"},
    {"css", "text/css"}, {"txt", "text/plain"}, {"log", "text/plain"},
    {"md", "text/markdown"}, {"markdown", "text/markdown"},
    {"csv", "text/csv"}, {"xml", "application/xml"}, {"json", "application/json"},
    {"js", "text/javascript"}, {"mjs", "text/javascript"}, {"cjs", "text/javascript"},
    {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"}, {"jpe", "image/jpeg"}, {"gif", "image/gif"},
    {"webp", "image/webp"}, {"bmp", "image/bmp"}, {"ico", "image/vnd.microsoft.icon"},
    {"avif", "image/avif"}, {"tif", "image/tiff"}, {"tiff", "image/tiff"},
    {"mp3", "audio/mpeg"}, {"mp2", "audio/mpeg"}, {"m4a", "audio/mp4"},
    {"aac", "audio/aac"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"},
    {"oga", "audio/ogg"}, {"opus", "audio/opus"}, {"flac", "audio/flac"},
    {"mid", "audio/midi"}, {"midi", "audio/midi"},
    {"mp4", "video/mp4"}, {"m4v", "video/mp4"}, {"webm", "video/webm"},
    {"ogv", "video/ogg"}, {"mov", "video/quicktime"}, {"avi", "video/x-msvideo"},
    {"mkv", "video/x-matroska"}, {"mpeg", "video/mpeg"}, {"mpg", "video/mpeg"},
    {"pdf", "application/pdf"}, {"wasm", "application/wasm"},
    {"zip", "application/zip"}, {"gz", "application/gzip"},
    {"", "application/octet-stream"}
};

static int ext_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

const char *cwv_mime_from_name(const char *name)
{
    const char *dot = NULL, *p;
    size_t i;
    if (!name || !*name) return "application/octet-stream";
    for (p = name; *p; ++p) if (*p == '.') dot = p;
    if (!dot || !dot[1]) return "application/octet-stream";
    ++dot;
    for (i = 0; i < sizeof(g_mime_map) / sizeof(g_mime_map[0]); ++i)
        if (ext_equal(dot, g_mime_map[i].ext)) return g_mime_map[i].type;
    return "application/octet-stream";
}

CwvMimeKind cwv_mime_kind(const char *mime)
{
    if (!mime) return CWV_MIME_UNKNOWN;
    if (!strcmp(mime, "text/html")) return CWV_MIME_HTML;
    if (!strcmp(mime, "application/xhtml+xml")) return CWV_MIME_XHTML;
    if (!strncmp(mime, "image/", 6)) return CWV_MIME_IMAGE;
    if (!strncmp(mime, "audio/", 6)) return CWV_MIME_AUDIO;
    if (!strncmp(mime, "video/", 6)) return CWV_MIME_VIDEO;
    if (!strncmp(mime, "text/", 5) || !strcmp(mime, "application/json") ||
        !strcmp(mime, "application/xml") || !strcmp(mime, "application/javascript"))
        return CWV_MIME_TEXT;
    if (!strcmp(mime, "application/pdf")) return CWV_MIME_PDF;
    if (!strcmp(mime, "application/octet-stream")) return CWV_MIME_BINARY;
    return CWV_MIME_UNKNOWN;
}
