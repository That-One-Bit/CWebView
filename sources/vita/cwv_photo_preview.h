#ifndef PISKEL_VITA_PHOTO_PREVIEW_H
#define PISKEL_VITA_PHOTO_PREVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open a PNG supplied as a data URI in Vita's system photo-review UI.
 *
 * The caller owns the input string. This function copies/decodes the image
 * before invoking the system photo exporter, so the input may be released
 * immediately after the call returns.
 */
int piskel_vita_open_photo_preview_data_uri(const char *data_uri);

#ifdef __cplusplus
}
#endif

#endif
