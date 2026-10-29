#ifndef CWV_MIME_H
#define CWV_MIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return a stable MIME type for a filename/extension. Matching is
 * case-insensitive. Unknown extensions return application/octet-stream.
 * This is extension-based metadata, not a security/content-sniffing API. */
const char *cwv_mime_from_name(const char *name);

/* File-template class used by CWMenu's file browser. */
typedef enum CwvMimeKind {
    CWV_MIME_UNKNOWN = 0,
    CWV_MIME_HTML,
    CWV_MIME_XHTML,
    CWV_MIME_IMAGE,
    CWV_MIME_AUDIO,
    CWV_MIME_VIDEO,
    CWV_MIME_TEXT,
    CWV_MIME_PDF,
    CWV_MIME_BINARY
} CwvMimeKind;

CwvMimeKind cwv_mime_kind(const char *mime_type);

#ifdef __cplusplus
}
#endif
#endif
