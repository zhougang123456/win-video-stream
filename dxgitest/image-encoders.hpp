#include "ring.hpp"
#include <csetjmp>
#include "glz-encoder.hpp"
#include "zlib-encoder.hpp"
#include "jpeg-encoder.hpp"
#include "drawable.hpp"

typedef struct RedGlzDrawable RedGlzDrawable;
typedef struct GlzDrawableInstanceItem GlzDrawableInstanceItem;

#define MAX_GLZ_DRAWABLE_INSTANCES 2
#define MAX_CACHE_CLIENTS 1
#define MAX_LZ_ENCODERS MAX_CACHE_CLIENTS

#define LINK_TO_GLZ(ptr) SPICE_CONTAINEROF((ptr), RedGlzDrawable, \
                                           drawable_link)
#define DRAWABLE_FOREACH_GLZ_SAFE(drawable, link, next, glz) \
    SAFE_FOREACH(link, next, drawable, &(drawable)->glz_retention.ring, glz, LINK_TO_GLZ(link))

typedef struct ImageEncoderSharedData ImageEncoderSharedData;
typedef struct GlzSharedDictionary GlzSharedDictionary;
typedef struct GlzImageRetention GlzImageRetention;
typedef struct RedCompressBuf RedCompressBuf;


struct ImageEncoderSharedData {
	UINT32 glz_drawable_count;
};

struct GlzImageRetention {
	Ring ring;
};

#define RED_COMPRESS_BUF_SIZE (1024 * 64)
struct RedCompressBuf {
	RedCompressBuf* send_next;

	/* This buffer provide space for compression algorithms.
	 * Some algorithms access the buffer as an array of 32 bit words
	 * so is defined to make sure is always aligned that way.
	 */
	union {
		UINT8  bytes[RED_COMPRESS_BUF_SIZE];
		UINT32 words[RED_COMPRESS_BUF_SIZE / 4];
	} buf;
};

typedef struct compress_send_data_t {
	RedCompressBuf* comp_buf;
	UINT32 comp_buf_size;
	SpicePalette* lzplt_palette;
	bool is_lossy;
} compress_send_data_t;

struct GlzSharedDictionary {
	SharedDictionary* dict;
	UINT32 refs;
	UINT8 id;
	SRWLOCK  encode_lock;
	int migrate_freeze;
};

typedef struct {
	RedCompressBuf* bufs_head;
	RedCompressBuf* bufs_tail;
	jmp_buf jmp_env;
	union {
		struct {
			SpiceChunks* chunks;
			int next;
			int stride;
			int reverse;
		} lines_data;
		struct {
			RedCompressBuf* next;
			int size_left;
		} compressed_data; // for encoding data that was already compressed by another method
	} u;
} EncoderData;

typedef struct {
	GlzEncoderUsrContext usr;
	EncoderData data;
} GlzData;

typedef struct {
	ZlibEncoderUsrContext usr;
	EncoderData data;
} ZlibData;

typedef struct {
	JpegEncoderUsrContext usr;
	EncoderData data;
} JpegData;

class ImageEncoders 
{
public:
	ImageEncoderSharedData* shared_data;
	GlzData m_glz_data;
	Ring glz_drawables;               // all the living lz drawable, ordered by encoding time
	Ring glz_drawables_inst_to_free;               // list of instances to be freed
	HANDLE glz_drawables_inst_to_free_lock;
	/* global lz encoding entities */
	GlzSharedDictionary* glz_dict;

	ImageEncoders();
	~ImageEncoders();

	void image_encoders_init(ImageEncoderSharedData* data);
	bool image_encoders_get_glz_dictionary(UINT8 id, int window_size);
	bool image_encoders_glz_create(UINT8 id);
	bool image_encoders_compress_glz(SpiceImage* dest,
		SpiceBitmap* src, RedDrawable* red_drawable,
		GlzImageRetention* glz_retention,
		compress_send_data_t* o_comp_data);

	bool image_encoders_compress_jpeg(SpiceImage* dest,
		SpiceBitmap* src, compress_send_data_t* o_comp_data);
private:
	
	GlzEncoder* glz;
	RedGlzDrawable* get_glz_drawable(RedDrawable* red_drawable,GlzImageRetention* glz_retention);
	void image_encoders_init_glz_data(void);
	GlzSharedDictionary* create_glz_dictionary(UINT8 id, int window_size);
	void image_encoders_release_glz(void);
	void image_encoders_free_glz_drawables(void);

	int zlib_level;
	ZlibEncoder* zlib;
	ZlibData m_zlib_data;
	void image_encoders_init_zlib(void);

	int jpeg_quality;
	JpegData m_jpeg_data;
	JpegEncoder* jpeg;
	void image_encoders_init_jpeg(void);
	
	
};

struct GlzDrawableInstanceItem {
	RingItem glz_link;
	RingItem free_link;
	GlzEncDictImageContext* context;
	RedGlzDrawable* glz_drawable;
};

struct RedGlzDrawable {
	RingItem link;    // ordered by the time it was encoded
	RingItem drawable_link;
	RedDrawable* red_drawable;
	GlzDrawableInstanceItem instances_pool[MAX_GLZ_DRAWABLE_INSTANCES];
	Ring instances;
	UINT8 instances_count;
	bool has_drawable;
	ImageEncoders* encoders;
};

