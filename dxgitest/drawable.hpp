#include "bitmap.hpp"

enum {
	DRAW_NOP,
	DRAW_COPY,
	COPY_BITS,
};

typedef enum ImageType {

	IMAGE_TYPE_BITMAP,
	IMAGE_TYPE_QUIC,
	IMAGE_TYPE_RESERVED,
	IMAGE_TYPE_LZ_PLT = 100,
	IMAGE_TYPE_LZ_RGB,
	IMAGE_TYPE_GLZ_RGB,
	IMAGE_TYPE_FROM_CACHE,
	IMAGE_TYPE_SURFACE,
	IMAGE_TYPE_JPEG,
	IMAGE_TYPE_FROM_CACHE_LOSSLESS,
	IMAGE_TYPE_ZLIB_GLZ_RGB,
	IMAGE_TYPE_JPEG_ALPHA,
	IMAGE_TYPE_LZ4,

	SPICE_IMAGE_TYPE_ENUM_END
} ImageType;

enum {
	CHUNKS_FLAGS_UNSTABLE = (1 << 0),
	CHUNKS_FLAGS_FREE = (1 << 1)
};

typedef enum ImageScaleMode {
	IMAGE_SCALE_MODE_INTERPOLATE,
	IMAGE_SCALE_MODE_NEAREST,

	IMAGE_SCALE_MODE_ENUM_END
} ImageScaleMode;

typedef enum Ropd {
	ROPD_INVERS_SRC = (1 << 0),
	ROPD_INVERS_BRUSH = (1 << 1),
	ROPD_INVERS_DEST = (1 << 2),
	ROPD_OP_PUT = (1 << 3),
	ROPD_OP_OR = (1 << 4),
	ROPD_OP_AND = (1 << 5),
	ROPD_OP_XOR = (1 << 6),
	ROPD_OP_BLACKNESS = (1 << 7),
	ROPD_OP_WHITENESS = (1 << 8),
	ROPD_OP_INVERS = (1 << 9),
	ROPD_INVERS_RES = (1 << 10),

	ROPD_MASK = 0x7ff

} Ropd;
typedef enum EffectType
{
	EFFECT_BLEND = 0,
	EFFECT_OPAQUE = 1,
	EFFECT_REVERT_ON_DUP = 2,
	EFFECT_BLACKNESS_ON_DUP = 3,
	EFFECT_WHITENESS_ON_DUP = 4,
	EFFECT_NOP_ON_DUP = 5,
	EFFECT_NOP = 6,
	EFFECT_OPAQUE_BRUSH = 7
} EffectType;

typedef struct SpiceChunk {
	BYTE* data;
	UINT32 len;
} SpiceChunk;
typedef struct SpiceChunks {
	UINT32     data_size;
	UINT32     num_chunks;
	UINT32     flags;
	SpiceChunk*   chunk;
} SpiceChunks;

typedef struct SpicePoint {
	INT32 x;
	INT32 y;
} SpicePoint;

typedef struct SpiceRect {
	INT32 left;
	INT32 top;
	INT32 right;
	INT32 bottom;
} SpiceRect;

typedef struct SpiceImageDescriptor {
	UINT64 id;
	UINT8 type;
	UINT8 flags;
	UINT32 width;
	UINT32 height;
} SpiceImageDescriptor;

typedef struct SpicePalette {
	UINT64 unique;
	UINT16 num_ents;
	UINT32* ents;
} SpicePalette;

typedef struct SpiceBitmap {
	UINT8 format;
	UINT8 flags;
	UINT32 x;
	UINT32 y;
	UINT32 stride;
	SpicePalette* palette;
	UINT64 palette_id;
	SpiceChunks* data;
} SpiceBitmap;

typedef struct SpiceZlibGlzRGBData {
	UINT32 glz_data_size;
	UINT32 data_size;
	SpiceChunks* data;
} SpiceZlibGlzRGBData;

typedef struct SpiceQUICData {
	UINT32 data_size;
	SpiceChunks* data;
} SpiceLZRGBData, SpiceJPEGData;

typedef struct SpiceImage {
	SpiceImageDescriptor descriptor;
	union {
		SpiceBitmap         bitmap;
		SpiceLZRGBData      lz_rgb;
		SpiceZlibGlzRGBData zlib_glz;
		SpiceJPEGData       jpeg;
	} u;
} SpiceImage;


typedef struct SpiceQMask {
	UINT8 flags;
	SpicePoint pos;
	SpiceImage* bitmap;
} SpiceQMask;


typedef struct SpiceCopy {
	SpiceImage* src_bitmap;
	SpiceRect src_area;
	UINT16 rop_descriptor;
	UINT8 scale_mode;
	SpiceQMask mask;
} SpiceCopy;

typedef struct RedDrawable {
	int refs;
	UINT32 surface_id;
	UINT8 effect;
	UINT8 type;
	SpiceRect bbox;
	UINT32 mm_time;
	union {
		SpiceCopy copy;
		struct {
			SpicePoint src_pos;
		} copy_bits;
	} u;
	//GlzImageRetention glz_retention;
} RedDrawable;

void chunks_destroy(SpiceChunks* chunks);

SpiceChunks* chunks_new_linear(BYTE* data, UINT32 len);

static inline RedDrawable* red_drawable_ref(RedDrawable* drawable)
{
	drawable->refs++;
	return drawable;
}

void red_drawable_unref(RedDrawable* red_drawable);

void red_drawable_get(RedDrawable* red_drawable, SpiceRect* rect, BYTE* data, UINT32 time);
