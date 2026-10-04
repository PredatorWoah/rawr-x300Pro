#pragma once
#include <stdint.h>
typedef struct AImage AImage;
typedef int media_status_t;
#define AMEDIA_OK 0
#define AIMAGE_FORMAT_RAW16 0x20
#define AIMAGE_FORMAT_RAW10 0x25
#ifdef __cplusplus
extern "C" {
#endif
media_status_t AImage_getFormat(const AImage*, int32_t*);
media_status_t AImage_getWidth(const AImage*, int32_t*);
media_status_t AImage_getHeight(const AImage*, int32_t*);
media_status_t AImage_getNumberOfPlanes(const AImage*, int32_t*);
media_status_t AImage_getPlaneRowStride(const AImage*, int, int32_t*);
media_status_t AImage_getPlanePixelStride(const AImage*, int, int32_t*);
#ifdef __cplusplus
}
#endif
