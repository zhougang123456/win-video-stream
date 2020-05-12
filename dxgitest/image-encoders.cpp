#include "image-encoders.hpp"
#include <stdio.h>

/* Remove from the to_free list and the instances_list.
   When no instance is left - the RedGlzDrawable is released too. (and the qxl drawable too, if
   it is not used by Drawable).
   NOTE - 1) can be called only by the display channel that created the drawable
		  2) it is assumed that the instance was already removed from the dictionary*/
static void glz_drawable_instance_item_free(GlzDrawableInstanceItem* instance)
{
	RedGlzDrawable* glz_drawable;

	if (!instance) {
		return;
	}
	if (!instance->glz_drawable) {
		return;
	}

	glz_drawable = instance->glz_drawable;

	if (!(glz_drawable->instances_count > 0)) {
		return;
	}

	ring_remove(&instance->glz_link);
	glz_drawable->instances_count--;

	// when the remove callback is performed from the channel that the
	// drawable belongs to, the instance is not added to the 'to_free' list
	if (ring_item_is_linked(&instance->free_link)) {
		ring_remove(&instance->free_link);
	}

	if (ring_is_empty(&glz_drawable->instances)) {
		if (!(glz_drawable->instances_count == 0)) {
			return;
		}

		if (glz_drawable->has_drawable) {
			ring_remove(&glz_drawable->drawable_link);
		}
		red_drawable_unref(glz_drawable->red_drawable);
		glz_drawable->encoders->shared_data->glz_drawable_count--;
		if (ring_item_is_linked(&glz_drawable->link)) {
			ring_remove(&glz_drawable->link);
		}
		free(glz_drawable);
	}
}
static void* glz_usr_malloc(GlzEncoderUsrContext* usr, int size)
{
	return malloc(size);
}

static void glz_usr_free(GlzEncoderUsrContext* usr, void* ptr)
{
	free(ptr);
}

/* Allocate more space for compressed buffer.
 * The pointer returned in io_ptr is garanteed to be aligned to 4 bytes.
 */
static int encoder_usr_more_space(EncoderData* enc_data, BYTE** io_ptr)
{
	RedCompressBuf* buf;

	buf = (RedCompressBuf*) malloc(sizeof(RedCompressBuf));
	enc_data->bufs_tail->send_next = buf;
	enc_data->bufs_tail = buf;
	buf->send_next = NULL;
	*io_ptr = buf->buf.bytes;
	return sizeof(buf->buf);
}

static inline int encoder_usr_more_lines(EncoderData* enc_data, BYTE** lines)
{
	struct SpiceChunk* chunk;

	if (enc_data->u.lines_data.reverse) {
		if (!(enc_data->u.lines_data.next >= 0)) {
			return 0;
		}
	}
	else {
		if (!(enc_data->u.lines_data.next < enc_data->u.lines_data.chunks->num_chunks)) {
			return 0;
		}
	}

	chunk = &enc_data->u.lines_data.chunks->chunk[enc_data->u.lines_data.next];
	if (chunk->len % enc_data->u.lines_data.stride) {
		return 0;
	}

	if (enc_data->u.lines_data.reverse) {
		enc_data->u.lines_data.next--;
		*lines = chunk->data + chunk->len - enc_data->u.lines_data.stride;
	}
	else {
		enc_data->u.lines_data.next++;
		*lines = chunk->data;
	}

	return chunk->len / enc_data->u.lines_data.stride;
}

static int glz_usr_more_space(GlzEncoderUsrContext* usr, BYTE** io_ptr)
{
	EncoderData* usr_data = &(((GlzData*)usr)->data);
	return encoder_usr_more_space(usr_data, io_ptr);
}

static int glz_usr_more_lines(GlzEncoderUsrContext* usr, BYTE** lines)
{
	EncoderData* usr_data = &(((GlzData*)usr)->data);
	return encoder_usr_more_lines(usr_data, lines);
}

static void glz_usr_free_image(GlzEncoderUsrContext* usr, GlzUsrImageContext* image)
{
	GlzData* lz_data = (GlzData*)usr;
	GlzDrawableInstanceItem* glz_drawable_instance = (GlzDrawableInstanceItem*)image;
	ImageEncoders* drawable_enc = glz_drawable_instance->glz_drawable->encoders;
	ImageEncoders* this_enc = SPICE_CONTAINEROF(lz_data, ImageEncoders, m_glz_data);
	if (this_enc == drawable_enc) {
		glz_drawable_instance_item_free(glz_drawable_instance);
	}
	else {
		/* The glz dictionary is shared between all DisplayChannelClient
		 * instances that belong to the same client, and glz_usr_free_image
		 * can be called by the dictionary code
		 * (glz_dictionary_window_remove_head). Thus this function can be
		 * called from any DisplayChannelClient thread, hence the need for
		 * this check.
		 */
		WaitForSingleObject(drawable_enc->glz_drawables_inst_to_free_lock, INFINITE);
		ring_add_before(&glz_drawable_instance->free_link,
			&drawable_enc->glz_drawables_inst_to_free);
		ReleaseMutex(drawable_enc->glz_drawables_inst_to_free_lock);
	}
}

static int zlib_usr_more_space(ZlibEncoderUsrContext* usr, UINT8** io_ptr)
{
	EncoderData* usr_data = &(((ZlibData*)usr)->data);
	return encoder_usr_more_space(usr_data, io_ptr);
}

static int zlib_usr_more_input(ZlibEncoderUsrContext* usr, UINT8** input)
{
	EncoderData* usr_data = &(((ZlibData*)usr)->data);
	int buf_size;

	if (!usr_data->u.compressed_data.next) {
		if (!(usr_data->u.compressed_data.size_left == 0)) {
			//prinf("error\n");
			return -1;
		}
		return 0;
	}

	*input = usr_data->u.compressed_data.next->buf.bytes;
	buf_size = MIN(sizeof(usr_data->u.compressed_data.next->buf),
		usr_data->u.compressed_data.size_left);

	usr_data->u.compressed_data.next = usr_data->u.compressed_data.next->send_next;
	usr_data->u.compressed_data.size_left -= buf_size;
	return buf_size;
}

static int jpeg_usr_more_space(JpegEncoderUsrContext* usr, UINT8** io_ptr)
{
	EncoderData* usr_data = &(((JpegData*)usr)->data);
	return encoder_usr_more_space(usr_data, io_ptr);
}

static int jpeg_usr_more_lines(JpegEncoderUsrContext* usr, UINT8** lines)
{
	EncoderData* usr_data = &(((JpegData*)usr)->data);
	return encoder_usr_more_lines(usr_data, lines);
}

void ImageEncoders::image_encoders_init_glz_data(void)
{
	
	m_glz_data.usr.malloc = glz_usr_malloc;
	m_glz_data.usr.free = glz_usr_free;
	m_glz_data.usr.more_space = glz_usr_more_space;
	m_glz_data.usr.more_lines = glz_usr_more_lines;
	m_glz_data.usr.free_image = glz_usr_free_image;
}

void ImageEncoders::image_encoders_init_zlib(void)
{

	m_zlib_data.usr.more_space = zlib_usr_more_space;
	m_zlib_data.usr.more_input = zlib_usr_more_input;

	zlib = new ZlibEncoder();
	

	if (!zlib->init(&m_zlib_data.usr, ZLIB_DEFAULT_COMPRESSION_LEVEL)) {
		//printf("create zlib encoder failed\n");
	}

}

void ImageEncoders::image_encoders_init_jpeg(void)
{
	m_jpeg_data.usr.more_space = jpeg_usr_more_space;
	m_jpeg_data.usr.more_lines = jpeg_usr_more_lines;

	jpeg = new JpegEncoder();

	if (!jpeg->init(&m_jpeg_data.usr)) {
		//printf("create jpeg encoder failed\n");
	}

}

void ImageEncoders::image_encoders_init(ImageEncoderSharedData* data)
{	
	if (!data) {
		return;
	}

	shared_data = data;

	ring_init(&glz_drawables);
	ring_init(&glz_drawables_inst_to_free);
	glz_drawables_inst_to_free_lock = CreateMutex(NULL, FALSE, NULL);

	image_encoders_init_glz_data();
	image_encoders_init_zlib();
	zlib_level = ZLIB_DEFAULT_COMPRESSION_LEVEL;

	image_encoders_init_jpeg();
	jpeg_quality = JPEG_DEFAULT_COMPRESSION_QUALITY;
}

static GlzSharedDictionary* glz_shared_dictionary_new(UINT8 id, SharedDictionary* dict)
{
	if (dict == NULL) {
		return NULL;
	}

	GlzSharedDictionary* shared_dict = (GlzSharedDictionary * )malloc(sizeof(GlzSharedDictionary));
	memset(shared_dict, 0, sizeof(GlzSharedDictionary));
	shared_dict->dict = dict;
	shared_dict->id = id;
	shared_dict->refs = 1;
	shared_dict->migrate_freeze = FALSE;
	InitializeSRWLock(&shared_dict->encode_lock);

	return shared_dict;
}

GlzSharedDictionary* ImageEncoders::create_glz_dictionary(UINT8 id, int window_size)
{
	//printf("Lz Window %d Size=%d\n", id, window_size);

	SharedDictionary* glz_dict = new SharedDictionary(window_size, MAX_LZ_ENCODERS, &m_glz_data.usr);
	return glz_shared_dictionary_new(id, glz_dict);
}

bool ImageEncoders::image_encoders_get_glz_dictionary(UINT8 id, int window_size)
{
	GlzSharedDictionary* shared_dict;
	if (glz_dict) {
		return false;
	}
	shared_dict = create_glz_dictionary(id, window_size);
	glz_dict = shared_dict;
	return shared_dict != NULL;
}

bool ImageEncoders::image_encoders_glz_create(UINT8 id)
{	
	glz = new GlzEncoder();
	return glz->init(id, glz_dict->dict, &m_glz_data.usr);
}

static const LzImageType bitmap_fmt_to_lz_image_type[] = {
	LZ_IMAGE_TYPE_INVALID,
	LZ_IMAGE_TYPE_PLT1_LE,
	LZ_IMAGE_TYPE_PLT1_BE,
	LZ_IMAGE_TYPE_PLT4_LE,
	LZ_IMAGE_TYPE_PLT4_BE,
	LZ_IMAGE_TYPE_PLT8,
	LZ_IMAGE_TYPE_RGB16,
	LZ_IMAGE_TYPE_RGB24,
	LZ_IMAGE_TYPE_RGB32,
	LZ_IMAGE_TYPE_RGBA,
	LZ_IMAGE_TYPE_A8
};

static void encoder_data_init(EncoderData* data)
{
	data->bufs_tail = (RedCompressBuf*)malloc(sizeof(RedCompressBuf));
	data->bufs_head = data->bufs_tail;
	data->bufs_head->send_next = NULL;
}

RedGlzDrawable* ImageEncoders::get_glz_drawable(RedDrawable* red_drawable,GlzImageRetention* glz_retention)
{
	

	RingItem* item, * next;

	// TODO - I don't really understand what's going on here, so doing the technical equivalent
	// now that we have multiple glz_dicts, so the only way to go from dcc to drawable glz is to go
	// over the glz_ring (unless adding some better data structure then a ring)
	/*SAFE_FOREACH(item, next, TRUE, &glz_retention->ring, ret, LINK_TO_GLZ(item)) {
		if (ret->encoders == enc) {
			return ret;
		}
	}*/

	RedGlzDrawable* ret = (RedGlzDrawable*)malloc(sizeof(RedGlzDrawable));
	memset(ret, 0, sizeof(RedGlzDrawable));

	ret->encoders = this;
	ret->red_drawable = red_drawable_ref(red_drawable);
	ret->has_drawable = TRUE;
	ret->instances_count = 0;
	ring_init(&ret->instances);

	ring_item_init(&ret->link);
	ring_item_init(&ret->drawable_link);
	ring_add_before(&ret->link, &glz_drawables);
	//ring_add(&glz_retention->ring, &ret->drawable_link);
	shared_data->glz_drawable_count++;
	return ret;
}

/* allocates new instance and adds it to instances in the given drawable.
   NOTE - the caller should set the glz_instance returned by the encoder by itself.*/
static GlzDrawableInstanceItem* add_glz_drawable_instance(RedGlzDrawable* glz_drawable)
{
	if (!(glz_drawable->instances_count < MAX_GLZ_DRAWABLE_INSTANCES)) {
		return NULL;
	}
	// NOTE: We assume the additions are performed consecutively, without removals in the middle
	GlzDrawableInstanceItem* ret = glz_drawable->instances_pool + glz_drawable->instances_count;
	glz_drawable->instances_count++;

	ring_item_init(&ret->free_link);
	ring_item_init(&ret->glz_link);
	ring_add(&glz_drawable->instances, &ret->glz_link);
	ret->context = NULL;
	ret->glz_drawable = glz_drawable;

	return ret;
}

static void encoder_data_reset(EncoderData* data)
{
	RedCompressBuf* buf = data->bufs_head;
	while (buf) {
		RedCompressBuf* next = buf->send_next;
		free(buf);
		buf = next;
	}
	data->bufs_head = data->bufs_tail = NULL;
}

bool ImageEncoders::image_encoders_compress_glz(SpiceImage* dest,
	SpiceBitmap* src, RedDrawable* red_drawable,
	GlzImageRetention* glz_retention,
	compress_send_data_t* o_comp_data)
{
	if (!(bitmap_fmt_is_rgb(src->format))){
		return FALSE;
	}
	GlzData* glz_data = &m_glz_data;
	ZlibData* zlib_data;
	LzImageType type = bitmap_fmt_to_lz_image_type[src->format];
	
	GlzDrawableInstanceItem* glz_drawable_instance;
	int glz_size;
	int zlib_size;

	if ((src->x * src->y) >= glz_dict->dict->glz_enc_dictionary_get_size()) {
		return FALSE;
	}

	AcquireSRWLockShared(&glz_dict->encode_lock);
	/* using the global dictionary only if it is not frozen */
	if (glz_dict->migrate_freeze) {
		ReleaseSRWLockShared(&glz_dict->encode_lock);
		return FALSE;
	}

	encoder_data_init(&glz_data->data);

	RedGlzDrawable* glz_drawable = get_glz_drawable(red_drawable, glz_retention);
	glz_drawable_instance = add_glz_drawable_instance(glz_drawable);

	glz_data->data.u.lines_data.chunks = src->data;
	glz_data->data.u.lines_data.stride = src->stride;
	glz_data->data.u.lines_data.next = 0;
	glz_data->data.u.lines_data.reverse = 0;

	glz_size = glz->glz_encode(type, src->x, src->y,
		(src->flags & BITMAP_FLAGS_TOP_DOWN), NULL, 0,
		src->stride, glz_data->data.bufs_head->buf.bytes,
		sizeof(glz_data->data.bufs_head->buf),
		glz_drawable_instance,
		&glz_drawable_instance->context);
	if (!glz_size < MIN_GLZ_SIZE_FOR_ZLIB) {
		goto glz;
	}
	zlib_data = &m_zlib_data;
	encoder_data_init(&zlib_data->data);

	zlib_data->data.u.compressed_data.next = glz_data->data.bufs_head;
	zlib_data->data.u.compressed_data.size_left = glz_size;

	zlib_size = zlib->encode(zlib_level,
		glz_size, zlib_data->data.bufs_head->buf.bytes,
		sizeof(zlib_data->data.bufs_head->buf));

	// the compressed buffer is bigger than the original data
	if (zlib_size >= glz_size) {
		encoder_data_reset(&zlib_data->data);
		goto glz;
	}
	else {
		encoder_data_reset(&glz_data->data);
	}

	dest->descriptor.type = IMAGE_TYPE_ZLIB_GLZ_RGB;
	dest->u.zlib_glz.glz_data_size = glz_size;
	dest->u.zlib_glz.data_size = zlib_size;

	o_comp_data->comp_buf = zlib_data->data.bufs_head;
	o_comp_data->comp_buf_size = zlib_size;

	ReleaseSRWLockShared(&glz_dict->encode_lock);
	return TRUE;

glz:
	ReleaseSRWLockShared(&glz_dict->encode_lock);

	dest->descriptor.type = IMAGE_TYPE_GLZ_RGB;
	dest->u.lz_rgb.data_size = glz_size;

	o_comp_data->comp_buf = glz_data->data.bufs_head;
	o_comp_data->comp_buf_size = glz_size;
	
	return TRUE;
}

bool ImageEncoders::image_encoders_compress_jpeg(SpiceImage* dest,
	SpiceBitmap* src, compress_send_data_t* o_comp_data)
{	
	JpegData* jpeg_data = &m_jpeg_data;
	//LzData* lz_data = &enc->lz_data;
	
	//LzContext* lz = enc->lz;
	volatile JpegEncoderImageType jpeg_in_type;
	int jpeg_size = 0;
	volatile int has_alpha = FALSE;
	int alpha_lz_size = 0;
	int comp_head_filled;
	int comp_head_left;
	int stride;
	//uint8_t* lz_out_start_byte;

	switch (src->format) {
	case BITMAP_FMT_16BIT:
		jpeg_in_type = JPEG_IMAGE_TYPE_RGB16;
		break;
	case BITMAP_FMT_24BIT:
		jpeg_in_type = JPEG_IMAGE_TYPE_BGR24;
		break;
	case BITMAP_FMT_32BIT:
		jpeg_in_type = JPEG_IMAGE_TYPE_BGRX32;
		break;
	/*case BITMAP_FMT_RGBA:
		jpeg_in_type = JPEG_IMAGE_TYPE_BGRX32;
		has_alpha = TRUE;
		break;*/
	default:
		return FALSE;
	}

	encoder_data_init(&jpeg_data->data);

	if (setjmp(jpeg_data->data.jmp_env)) {
		encoder_data_reset(&jpeg_data->data);
		return FALSE;
	}

	/*if (src->data->flags & CHUNKS_FLAGS_UNSTABLE) {
		spice_chunks_linearize(src->data);
	}*/

	jpeg_data->data.u.lines_data.chunks = src->data;
	jpeg_data->data.u.lines_data.stride = src->stride;
	if ((src->flags & BITMAP_FLAGS_TOP_DOWN)) {
		jpeg_data->data.u.lines_data.next = 0;
		jpeg_data->data.u.lines_data.reverse = 0;
		stride = src->stride;
	}
	else {
		jpeg_data->data.u.lines_data.next = src->data->num_chunks - 1;
		jpeg_data->data.u.lines_data.reverse = 1;
		stride = -1 * src->stride;
	}
	jpeg_size = jpeg->encode(jpeg_quality, jpeg_in_type,
		src->x, src->y, NULL,
		0, stride, jpeg_data->data.bufs_head->buf.bytes,
		sizeof(jpeg_data->data.bufs_head->buf));

	// the compressed buffer is bigger than the original data
	if (jpeg_size > (src->y * src->stride)) {
		longjmp(jpeg_data->data.jmp_env, 1);
	}

	if (!has_alpha) {
		dest->descriptor.type = IMAGE_TYPE_JPEG;
		dest->u.jpeg.data_size = jpeg_size;

		o_comp_data->comp_buf = jpeg_data->data.bufs_head;
		o_comp_data->comp_buf_size = jpeg_size;
		o_comp_data->is_lossy = TRUE;

		return TRUE;
	}

	/*lz_data->data.bufs_head = jpeg_data->data.bufs_tail;
	lz_data->data.bufs_tail = lz_data->data.bufs_head;

	comp_head_filled = jpeg_size % sizeof(lz_data->data.bufs_head->buf);
	comp_head_left = sizeof(lz_data->data.bufs_head->buf) - comp_head_filled;
	lz_out_start_byte = lz_data->data.bufs_head->buf.bytes + comp_head_filled;

	lz_data->data.u.lines_data.chunks = src->data;
	lz_data->data.u.lines_data.stride = src->stride;
	lz_data->data.u.lines_data.next = 0;
	lz_data->data.u.lines_data.reverse = 0;

	alpha_lz_size = lz_encode(lz, LZ_IMAGE_TYPE_XXXA, src->x, src->y,
		!!(src->flags & SPICE_BITMAP_FLAGS_TOP_DOWN),
		NULL, 0, src->stride,
		lz_out_start_byte,
		comp_head_left);

	 the compressed buffer is bigger than the original data
	if ((jpeg_size + alpha_lz_size) > (src->y * src->stride)) {
		longjmp(jpeg_data->data.jmp_env, 1);
	}

	dest->descriptor.type = SPICE_IMAGE_TYPE_JPEG_ALPHA;
	dest->u.jpeg_alpha.flags = 0;
	if (src->flags & SPICE_BITMAP_FLAGS_TOP_DOWN) {
		dest->u.jpeg_alpha.flags |= SPICE_JPEG_ALPHA_FLAGS_TOP_DOWN;
	}

	dest->u.jpeg_alpha.jpeg_size = jpeg_size;
	dest->u.jpeg_alpha.data_size = jpeg_size + alpha_lz_size;

	o_comp_data->comp_buf = jpeg_data->data.bufs_head;
	o_comp_data->comp_buf_size = jpeg_size + alpha_lz_size;
	o_comp_data->is_lossy = TRUE;*/
	
	return TRUE;
}

/*
 * Releases all the instances of the drawable from the dictionary and the display channel client.
 * The release of the last instance will also release the drawable itself and the qxl drawable
 * if possible.
 * NOTE - the caller should prevent encoding using the dictionary during this operation
 */
static void red_glz_drawable_free(RedGlzDrawable* glz_drawable)
{
	ImageEncoders* enc = glz_drawable->encoders;
	RingItem* head_instance = ring_get_head(&glz_drawable->instances);
	int cont = (head_instance != NULL);

	while (cont) {
		if (glz_drawable->instances_count == 1) {
			/* Last instance: glz_drawable_instance_item_free will free the glz_drawable */
			cont = FALSE;
		}
		GlzDrawableInstanceItem* instance = SPICE_CONTAINEROF(head_instance,
			GlzDrawableInstanceItem,
			glz_link);
		if (!ring_item_is_linked(&instance->free_link)) {
			// the instance didn't get out from window yet
			enc->glz_dict->dict->glz_enc_dictionary_remove_image(instance->context,&enc->m_glz_data.usr);
		}
		glz_drawable_instance_item_free(instance);

		if (cont) {
			head_instance = ring_get_head(&glz_drawable->instances);
		}
	}
}

/* Clear all lz drawables - enforce their removal from the global dictionary.
   NOTE - prevents encoding using the dictionary during the operation*/
void ImageEncoders::image_encoders_free_glz_drawables(void)
{
	RingItem* ring_link;

	if (!glz_dict) {
		return;
	}

	// assure no display channel is during global lz encoding
	AcquireSRWLockExclusive(&glz_dict->encode_lock);
	while ((ring_link = ring_get_head(&glz_drawables))) {
		RedGlzDrawable* drawable = SPICE_CONTAINEROF(ring_link, RedGlzDrawable, link);
		// no need to lock the to_free list, since we assured no other thread is encoding and
		// thus not other thread access the to_free list of the channel
		red_glz_drawable_free(drawable);
	}
	ReleaseSRWLockExclusive(&glz_dict->encode_lock);
}

/* destroy encoder, and dictionary if no one uses it*/
void ImageEncoders::image_encoders_release_glz(void)
{
	GlzSharedDictionary* shared_dict;

	image_encoders_free_glz_drawables();

	if (glz) {
		delete glz;
		glz = NULL;
	}

	if (!(shared_dict = glz_dict)) {
		return;
	}

	glz_dict = NULL;
	
	if (shared_dict->dict) {
		delete shared_dict->dict;
	}
	
	free(shared_dict);

}

ImageEncoders::ImageEncoders()
{
}

ImageEncoders::~ImageEncoders()
{
	image_encoders_release_glz();
	if (zlib) {
		delete zlib;
		zlib = NULL;
	}
	CloseHandle(glz_drawables_inst_to_free_lock);
	
	if (jpeg) {
		delete jpeg;
		jpeg = NULL;
	}
}


