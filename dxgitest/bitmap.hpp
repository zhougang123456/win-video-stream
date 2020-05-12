#include "macros.hpp"

typedef enum BitmapFlags {
	BITMAP_FLAGS_PAL_CACHE_ME = (1 << 0),
	BITMAP_FLAGS_PAL_FROM_CACHE = (1 << 1),
	BITMAP_FLAGS_TOP_DOWN = (1 << 2),

	BITMAP_FLAGS_MASK = 0x7
} BitmapFlags;

static inline int bitmap_fmt_is_rgb(UINT8 fmt)
{
	static const int fmt_is_rgb[] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };

	if (!(fmt < SPICE_N_ELEMENTS(fmt_is_rgb))) {
		return 0;
	}

	return fmt_is_rgb[fmt];
}

typedef enum BitmapFmt {
	BITMAP_FMT_INVALID,
	BITMAP_FMT_1BIT_LE,
	BITMAP_FMT_1BIT_BE,
	BITMAP_FMT_4BIT_LE,
	BITMAP_FMT_4BIT_BE,
	BITMAP_FMT_8BIT,
	BITMAP_FMT_16BIT,
	BITMAP_FMT_24BIT,
	BITMAP_FMT_32BIT,
	BITMAP_FMT_RGBA,
	BITMAP_FMT_8BIT_A,

	BITMAP_FMT_ENUM_END
} BitmapFmt;

enum {
	IMAGE_GROUP_DRIVER,
	IMAGE_GROUP_DEVICE,
	IMAGE_GROUP_RED,
	IMAGE_GROUP_DRIVER_DONT_CACHE,
};