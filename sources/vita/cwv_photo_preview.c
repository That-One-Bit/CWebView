#include "cwv_photo_preview.h"

#include <psp2/photoexport.h>
#include <psp2/photo_review_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PISKEL_VITA_DATA_URI_PREFIX "data:image/png;base64,"
#define PISKEL_VITA_DATA_URI_PREFIX_LEN 22
#define PISKEL_VITA_MAX_PNG_SIZE (16 * 1024 * 1024)
#define PISKEL_VITA_PHOTO_PATH_SIZE 1024

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int decode_base64(const char *src, size_t src_len, unsigned char *dst, size_t dst_cap)
{
    size_t i = 0;
    size_t out = 0;

    while (i < src_len) {
        int a, b, c, d;
        unsigned int value;

        while (i < src_len && (src[i] == '\r' || src[i] == '\n' || src[i] == ' ' || src[i] == '\t')) i++;
        if (i >= src_len) break;
        a = base64_value(src[i++]);

        while (i < src_len && (src[i] == '\r' || src[i] == '\n' || src[i] == ' ' || src[i] == '\t')) i++;
        if (i >= src_len) return -1;
        b = base64_value(src[i++]);
        if (a < 0 || b < 0) return -1;

        while (i < src_len && (src[i] == '\r' || src[i] == '\n' || src[i] == ' ' || src[i] == '\t')) i++;
        c = (i < src_len && src[i] != '=') ? base64_value(src[i]) : -2;
        if (i < src_len) i++;

        while (i < src_len && (src[i] == '\r' || src[i] == '\n' || src[i] == ' ' || src[i] == '\t')) i++;
        d = (i < src_len && src[i] != '=') ? base64_value(src[i]) : -2;
        if (i < src_len) i++;

        if (c == -1 || d == -1) return -1;
        if (c == -2 && d != -2) return -1;

        value = ((unsigned int)a << 18) | ((unsigned int)b << 12);
        if (out >= dst_cap) return -1;
        dst[out++] = (unsigned char)(value >> 16);

        if (c >= 0) {
            value |= (unsigned int)c << 6;
            if (out >= dst_cap) return -1;
            dst[out++] = (unsigned char)(value >> 8);
            if (d >= 0) {
                value |= (unsigned int)d;
                if (out >= dst_cap) return -1;
                dst[out++] = (unsigned char)value;
            }
        } else if (d >= 0) {
            return -1;
        }
    }

    return (int)out;
}

int piskel_vita_open_photo_preview_data_uri(const char *data_uri)
{
    const char *base64;
    size_t encoded_len;
    size_t max_decoded;
    unsigned char *png = NULL;
    char exported_path[PISKEL_VITA_PHOTO_PATH_SIZE];
    PhotoExportParam export_param;
    ScePhotoReviewDialogParam review_param;
    int decoded_size;
    int result;

    if (!data_uri || strncmp(data_uri, PISKEL_VITA_DATA_URI_PREFIX,
                             PISKEL_VITA_DATA_URI_PREFIX_LEN) != 0) {
        return -1;
    }

    base64 = data_uri + PISKEL_VITA_DATA_URI_PREFIX_LEN;
    encoded_len = strlen(base64);
    max_decoded = (encoded_len / 4) * 3 + 3;
    if (max_decoded == 0 || max_decoded > PISKEL_VITA_MAX_PNG_SIZE) {
        return -2;
    }

    png = (unsigned char *)malloc(max_decoded);
    if (!png) return -3;

    decoded_size = decode_base64(base64, encoded_len, png, max_decoded);
    if (decoded_size <= 0) {
        free(png);
        return -4;
    }

    /* The exporter is the supported VitaSDK route for turning arbitrary image
     * data into a photo that the system photo-review UI can display. */
    result = sceSysmoduleLoadModule(SCE_SYSMODULE_PHOTO_EXPORT);
    if (result < 0 && result != SCE_SYSMODULE_LOADED) {
        free(png);
        return result;
    }

    memset(&export_param, 0, sizeof(export_param));
    export_param.version = 1;

    memset(exported_path, 0, sizeof(exported_path));
    result = scePhotoExportFromData(png, (SceSize)decoded_size, &export_param,
                                    NULL, NULL, NULL,
                                    exported_path, sizeof(exported_path));
    free(png);
    if (result < 0) return result;

    /* Photo review is a system dialog: it takes over the display and gives the
     * user a separate system-photo-viewer experience without leaving Piskel. */
    memset(&review_param, 0, sizeof(review_param));
    scePhotoReviewDialogParamInit(&review_param);
    review_param.unk_0x50 = 1;
    snprintf((char *)review_param.path, sizeof(review_param.path), "%s", exported_path);

    result = scePhotoReviewDialogInit(&review_param);
    return result;
}
