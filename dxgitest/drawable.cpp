#include "drawable.hpp"

static void red_put_image(SpiceImage* red)
{
	if (red == NULL)
		return;

	switch (red->descriptor.type) {
	case IMAGE_TYPE_BITMAP:
		free(red->u.bitmap.palette);
		chunks_destroy(red->u.bitmap.data);
		break;
	}
	free(red);
}

static void red_put_qmask(SpiceQMask* red)
{
	red_put_image(red->bitmap);
}

static void red_put_copy(SpiceCopy* red)
{
	red_put_image(red->src_bitmap);
	red_put_qmask(&red->mask);
}

void red_put_drawable(RedDrawable* red)
{

	switch (red->type) {
	case DRAW_COPY:
		red_put_copy(&red->u.copy);
		break;
	}
}

SpiceChunks* chunks_new_linear(BYTE* data, UINT32 len)
{
	SpiceChunks* chunks;

	chunks = (SpiceChunks*)malloc(sizeof(SpiceChunks));
	chunks->flags = CHUNKS_FLAGS_FREE;
	chunks->num_chunks = 1;
	chunks->chunk = (SpiceChunk*)malloc(sizeof(SpiceChunk) * chunks->num_chunks);
	chunks->data_size = chunks->chunk[0].len = len;
	chunks->chunk[0].data = data;
	return chunks;
}

void chunks_destroy(SpiceChunks* chunks)
{
	unsigned int i;
	if (chunks->flags & CHUNKS_FLAGS_FREE) {
		for (i = 0; i < chunks->num_chunks; i++) {
			free(chunks->chunk[i].data);
		}
	}
	free(chunks);
}

void red_drawable_unref(RedDrawable* red_drawable)
{
	if (--red_drawable->refs) {
		return;
	}
	red_put_drawable(red_drawable);
	free(red_drawable);
}

static inline void get_rect(SpiceRect* dest, SpiceRect* src)
{
	dest->left = src->left;
	dest->right = src->right;
	dest->top = src->top;
	dest->bottom = src->bottom;
}

void red_drawable_get(RedDrawable* red_drawable, SpiceRect* rect, BYTE* data, UINT32 time)
{
	if (red_drawable == NULL){
		return;
	}
	red_drawable->refs = 1;
	red_drawable->surface_id = 0;
	red_drawable->effect = EFFECT_OPAQUE;
	red_drawable->type = DRAW_COPY;
	get_rect(&red_drawable->bbox, rect);
	red_drawable->mm_time = time;
	red_drawable->u.copy.scale_mode = IMAGE_SCALE_MODE_NEAREST;
	red_drawable->u.copy.mask.bitmap = 0;
	red_drawable->u.copy.rop_descriptor = ROPD_OP_PUT;
	red_drawable->u.copy.src_area.right = red_drawable->bbox.right - red_drawable->bbox.left;
	red_drawable->u.copy.src_area.bottom = red_drawable->bbox.bottom - red_drawable->bbox.top;
	INT32 height = red_drawable->u.copy.src_area.bottom;
	INT32 width = red_drawable->u.copy.src_area.right;
	INT32 format = BITMAP_FMT_32BIT;
	red_drawable->u.copy.src_bitmap = (SpiceImage*)malloc(sizeof(SpiceImage));
	memset(red_drawable->u.copy.src_bitmap, 0, sizeof(SpiceImage));
	red_drawable->u.copy.src_bitmap->descriptor.id = (((UINT64)0) << 32)
		| (((UINT32)IMAGE_GROUP_DRIVER_DONT_CACHE << 30) | (((UINT32)((width) & 0x1FFF)
			| ((UINT32)((height) & 0x1FFF) << 13) | ((UINT32)(format) << 26))));
	red_drawable->u.copy.src_bitmap->descriptor.type = IMAGE_TYPE_BITMAP;
	red_drawable->u.copy.src_bitmap->descriptor.flags = 0;
	red_drawable->u.copy.src_bitmap->descriptor.width = width;
	red_drawable->u.copy.src_bitmap->descriptor.height = height;
	SpiceBitmap* bitmap = &red_drawable->u.copy.src_bitmap->u.bitmap;
	bitmap->format = BITMAP_FMT_32BIT; //SPICE_BITMAP_FMT_RGBA;
	bitmap->flags = 0;
	bitmap->x = width;
	bitmap->y = height;
	bitmap->stride = width * 4;
	bitmap->palette = NULL;
	SpiceChunks* chunks = chunks_new_linear(data, bitmap->y * bitmap->stride);
	bitmap->data = chunks;
	
}