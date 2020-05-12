#include "macros.hpp"
#include "lz-common.hpp"

typedef void GlzEncDictImageContext;
typedef void GlzUsrImageContext;
typedef struct GlzEncoderUsrContext GlzEncoderUsrContext;

/* Interface for using the dictionary for encoding.
   Data structures are exposed for the encoder for efficiency
   purposes. */
typedef struct WindowImage WindowImage;
typedef struct WindowImageSegment WindowImageSegment;

#define HASH_SIZE_LOG 20
#define HASH_CHAIN_SIZE 1

#define HASH_SIZE (1 << HASH_SIZE_LOG)
#define HASH_MASK (HASH_SIZE - 1)

#define MAX_IMAGE_SEGS_NUM (0xffffffff)
#define NULL_IMAGE_SEG_ID MAX_IMAGE_SEGS_NUM
#define INIT_IMAGE_SEGS_NUM 1000

typedef struct HashEntry HashEntry;

struct HashEntry {
	UINT32 image_seg_idx;
	UINT32 ref_pix_idx;
};

struct WindowImage {
	UINT64 id;
	LzImageType type;
	int size;                    // in pixels
	UINT32 first_seg;
	GlzUsrImageContext* usr_context;
	WindowImage* next;
	UINT8 is_alive;
};

/* Images can be separated into several chunks. The basic unit of the
   dictionary window is one image segment. Each segment is encoded separately.
   An encoded match can refer to only one segment.*/
typedef struct WindowImageSegment {
	WindowImage* image;
	void* lines;
	void* lines_end;
	UINT32 pixels_num;            // Number of pixels in the segment
	UINT64 pixels_so_far;         // Total no. pixels passed through the window till this segment.
									// NOTE - never use size delta independently. It should
									// always be used with respect to a previous size delta
	UINT32 next;
} WindowImageSegment;

class SharedDictionary 
{
public:
	struct {
		/* The segments storage. A dynamic array.
		   By referring to a segment by its index, instead of address,
		   we save space in the hash entries (32bit instead of 64bit) */
		WindowImageSegment* segs;
		UINT32 segs_quota;

		/* The window is manged as a linked list rather than as a cyclic
		   array in order to keep the indices of the segments consistent
		   after reallocation */

		   /* the window in a resolution of image segments */
		UINT32 used_segs_head;             // the latest head
		UINT32 used_segs_tail;
		UINT32 free_segs_head;

		UINT32* encoders_heads; // Holds for each encoder (by id), the window head when
											 // it started the encoding.
											 // The head is NULL_IMAGE_SEG_ID when the encoder is
											 // not encoding.

		/* the window in a resolution of images. But here the head contains the oldest head*/
		WindowImage* used_images_tail;
		WindowImage* used_images_head;
		WindowImage* free_images;

		UINT64 pixels_so_far;
		UINT32 size_limit;                 // max number of pixels in a window (per encoder)
	} window;


	HashEntry htab[HASH_SIZE];
	UINT64 last_image_id;
	UINT32 max_encoders;
	HANDLE lock;
	SRWLOCK  rw_alloc_lock;
	GlzEncoderUsrContext* cur_usr; // each encoder has other context.

	/* size        : maximal number of pixels occupying the window
	   max_encoders_num: maximal number of encoders that use the dictionary
       usr         : callbacks */
	SharedDictionary(UINT32 size, UINT32 max_encoders_num, GlzEncoderUsrContext* usr);
	~SharedDictionary();
	/* returns the window capacity in pixels */
	UINT32 glz_enc_dictionary_get_size(void);
	/* image: the context returned by the encoder when the image was encoded.
	   NOTE - you should use this routine only when no encoder uses the dictionary.*/
	void glz_enc_dictionary_remove_image(GlzEncDictImageContext* image, GlzEncoderUsrContext* usr);

	/*
		Add the image to the tail of the window.
		If possible, release images from the head of the window.
		Also perform concurrency related operations.

		usr_image_context: when an image is released from the window due to capacity overflow,
						   usr_image_context is given as a parameter to the free_image callback.

		image_head_dist  : the number of images between the current image and the head of the
						   window that is associated with the encoder.
	*/
	WindowImage* glz_dictionary_pre_encode(UINT32 encoder_id, GlzEncoderUsrContext* usr,
		LzImageType image_type,
		int image_width, int image_height, int image_stride,
		UINT8* first_lines, unsigned int num_first_lines,
		GlzUsrImageContext* usr_image_context,
		UINT32* image_head_dist);

	/*
	   Performs concurrency related operations.
	   If possible, release images from the head of the window.
	*/
	void glz_dictionary_post_encode(UINT32 encoder_id, GlzEncoderUsrContext* usr);
	
private:
	bool glz_dictionary_window_create(UINT32 size);
	void glz_enc_dictionary_reset(GlzEncoderUsrContext* usr);
	void glz_dictionary_window_reset(void);
	void glz_dictionary_reset_hash(void);
	void glz_dictionary_window_reset_images(void);
	void glz_dictionary_window_destroy(void);
	void glz_dictionary_window_kill_image(WindowImage* image);
	void glz_dictionary_window_remove_head(UINT32 encoder_id, WindowImage* end_image);
	void glz_dictionary_window_free_image_segs(WindowImage* image);
	void glz_dictionary_window_free_image(WindowImage* image);
	WindowImage* glz_dictionary_window_get_new_head(int new_image_size);
	bool glz_dictionary_is_in_use(void);
	WindowImage* glz_dictionary_window_add_image(LzImageType image_type,
		int image_size, int image_height,
		int image_stride, UINT8* first_lines,
		unsigned int num_first_lines,
		GlzUsrImageContext* usr_image_context);
	WindowImage* glz_dictionary_window_alloc_image(void);
	UINT32 glz_dictionary_window_alloc_image_seg(WindowImage* image,
		int size, int stride,
		UINT8* lines, unsigned int num_lines);
	UINT32 glz_dictionary_window_alloc_image_seg_impl(void);
	void glz_dictionary_window_segs_realloc(void);

};


#define IMAGE_SEG_IS_EARLIER(dict, dst_seg, src_seg) (                     \
    ((src_seg) == NULL_IMAGE_SEG_ID) || (((dst_seg) != NULL_IMAGE_SEG_ID)  \
    && ((dict)->window.segs[(dst_seg)].pixels_so_far <                     \
       (dict)->window.segs[(src_seg)].pixels_so_far)))

#define UPDATE_HASH(dict, hval, seg, pix) { \
    (dict)->htab[hval].image_seg_idx = seg; \
    (dict)->htab[hval].ref_pix_idx = pix;   \
}

/* checks if the reference segment is located in the range of the window
   of the current encoder */
#define REF_SEG_IS_VALID(dict, enc_id, ref_seg, src_seg) ( \
    ((ref_seg) == (src_seg)) ||                            \
    ((ref_seg)->image &&                                   \
     (ref_seg)->image->is_alive &&                         \
     (src_seg->image->type == ref_seg->image->type) &&     \
     (ref_seg->pixels_so_far <= src_seg->pixels_so_far) && \
     ((dict)->window.segs[                                 \
        (dict)->window.encoders_heads[enc_id]].pixels_so_far <= \
        ref_seg->pixels_so_far)))

