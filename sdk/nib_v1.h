#ifndef NIJIN_NIB_V1_H
#define NIJIN_NIB_V1_H
/* NIB/1 raw-file & mmap wire layout, C11 / C++20. Strictly 7-bit ASCII. */
#include <float.h>
#include <stdint.h>
#include <string.h>

enum {
    NIB1_HEADER_BYTES = 64, NIB1_DESC_BYTES = 128, NIB1_PAYLOAD_ALIGN = 64,
    NIB1_H_MAGIC = 0, NIB1_H_VERSION = 8, NIB1_H_STAGE = 12,
    NIB1_H_FRAMES = 16, NIB1_H_WIDTH = 20, NIB1_H_HEIGHT = 24, NIB1_H_COUNT = 28,
    NIB1_H_QUICK_ID = 32, NIB1_H_META_OFFSET = 40, NIB1_H_META_BYTES = 48,
    NIB1_H_DIRECTORY = 56,
    NIB1_D_NAME = 0, NIB1_D_NAME_BYTES = 40, NIB1_D_TYPE = 40, NIB1_D_KIND = 42,
    NIB1_D_ORIGIN = 44, NIB1_D_CODEC = 46, NIB1_D_FRAME = 48, NIB1_D_LAYER = 52,
    NIB1_D_SHAPE = 56, NIB1_D_AFFINE = 72, NIB1_D_BBOX = 96,
    NIB1_D_OFFSET = 112, NIB1_D_BYTES = 120
};

enum {
    NIB1_U8 = 1, NIB1_U16 = 2, NIB1_I32 = 3, NIB1_F16 = 4, NIB1_F32 = 5, NIB1_F64 = 6,
    NIB1_I8 = 8, NIB1_I16 = 9, NIB1_U32 = 10, NIB1_I64 = 11, NIB1_U64 = 12, NIB1_BOOL = 13,
    NIB1_SCALAR = 0, NIB1_VECTOR = 1, NIB1_COV2 = 2, NIB1_ND = 3
};

/* Epistemic origin & per-pixel provenance flags (origin=0 is host-only). */
enum {
    NIB1_ORIG_NIJIN_MEASURED   = 0,
    NIB1_ORIG_CO_CALIBRATED    = 1,
    NIB1_ORIG_TESTABLE_GEOM    = 2,
    NIB1_ORIG_PARTITION_MASK   = 3,
    NIB1_ORIG_GENERATIVE_PRIOR = 4,
    NIB1_PROV_MEASURED         = 0x01,
    NIB1_PROV_CALIBRATED       = 0x02,
    NIB1_PROV_EXT_GEOMETRY     = 0x04,
    NIB1_PROV_EXT_ROUTING      = 0x08,
    NIB1_PROV_GENERATIVE       = 0x10
};

static inline uint16_t nib1_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static inline uint32_t nib1_u32(const uint8_t *p) {
    return (uint32_t)nib1_u16(p) | ((uint32_t)nib1_u16(p + 2) << 16);
}
static inline uint64_t nib1_u64(const uint8_t *p) {
    return (uint64_t)nib1_u32(p) | ((uint64_t)nib1_u32(p + 4) << 32);
}

#if defined(__cplusplus)
static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
              "NIB1 float reader requires binary32");
#else
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "NIB1 float reader requires binary32");
#endif
static inline float nib1_f32(const uint8_t *p) {
    uint32_t bits = nib1_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
#endif
