#pragma once
#include <stdint.h>
typedef struct AHardwareBuffer AHardwareBuffer;
typedef struct AHardwareBuffer_Desc { uint32_t width,height,layers,format,stride; uint64_t usage; uint32_t rfu0,rfu1; } AHardwareBuffer_Desc;
#define AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN (3ULL)
#define AHARDWAREBUFFER_USAGE_CPU_READ_MASK (0xFULL)
#ifdef __cplusplus
extern "C" {
#endif
void AHardwareBuffer_describe(const AHardwareBuffer*, AHardwareBuffer_Desc*);
int AHardwareBuffer_lock(AHardwareBuffer*, uint64_t, int, const void*, void**);
int AHardwareBuffer_unlock(AHardwareBuffer*, int*);
#ifdef __cplusplus
}
#endif
