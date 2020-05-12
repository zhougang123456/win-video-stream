#include "glz-encoder-dict.hpp"

struct GlzEncoderUsrContext {
	
	void* (*malloc)(GlzEncoderUsrContext* usr, int size);
	void (*free)(GlzEncoderUsrContext* usr, void* ptr);

	// get the next chunk of the image which is entered to the dictionary. If the image is down to
	// top, return it from the last line to the first one (stride should always be positive)
	int (*more_lines)(GlzEncoderUsrContext* usr, UINT8** lines);

	// get the next chunk of the compressed buffer.return number of bytes in the chunk.
	int (*more_space)(GlzEncoderUsrContext* usr, UINT8** io_ptr);

	// called when an image is removed from the dictionary, due to the window size limit
	void (*free_image)(GlzEncoderUsrContext* usr, GlzUsrImageContext* image);

};

typedef struct rgb32_pixel_t {
	BYTE b;
	BYTE g;
	BYTE r;
	BYTE pad;
} rgb32_pixel_t;

typedef struct rgb24_pixel_t {
	BYTE b;
	BYTE g;
	BYTE r;
} rgb24_pixel_t;

#define PIXEL rgb32_pixel_t

/* Holds a specific data for one encoder, and data that is relevant for the current image encoded */
class GlzEncoder 
{
public:
	GlzEncoderUsrContext* usr;
	UINT8 id;
	SharedDictionary* dict;

	struct {
		LzImageType type;
		UINT32 id;
		UINT32 first_win_seg;
	} cur_image;

	struct {
		UINT8* start;
		UINT8* now;
		UINT8* end;
		size_t bytes_count;
		UINT8* last_copy;  // pointer to the last byte in which copy count was written
	} io;

	GlzEncoder();
	~GlzEncoder();
	bool init(UINT8 id, SharedDictionary* dictionary, GlzEncoderUsrContext* usr_ctx);
	/*
		assumes width is in pixels and stride is in bytes
		usr_context       : when an image is released from the window due to capacity overflow,
						    usr_context is given as a parameter to the free_image callback.
		o_enc_dict_context: if glz_enc_dictionary_remove_image is called, it should be
						    called with the o_enc_dict_context that is associated with
						    the image.
		return: the number of bytes in the compressed data and sets o_enc_dict_context
		NOTE  : currently supports only rgb images in which width*bytes_per_pixel = stride OR
				palette images in which stride equals the min number of bytes to hold a line.
				The stride should be > 0
	*/
	int glz_encode(LzImageType type, int width, int height,
		int top_down, UINT8* lines, unsigned int num_lines, int stride,
		UINT8* io_ptr, unsigned int num_io_bytes, GlzUsrImageContext* usr_context,
		GlzEncDictImageContext** o_enc_dict_context);

private:
	bool encoder_reset(UINT8* io_ptr, UINT8* io_ptr_end);
	int more_io_bytes(void);
	void encode(UINT8 byte);
	void encode_32(unsigned int word);
	void encode_64(UINT64 word);
	void encode_copy_count(UINT8 copy_count);
	void update_copy_count(UINT8 copy_count);
	void compress_output_prev(void);
	void glz_rgb32_compress(void);
	void compress_seg(UINT32 seg_idx, PIXEL* from, int copied);
	void encode_match(UINT32 image_distance, size_t pixel_distance, size_t len);
	size_t do_match(SharedDictionary* dict,
					WindowImageSegment* ref_seg, const PIXEL* ref,
					const PIXEL* ref_limit,
					WindowImageSegment* ip_seg, const PIXEL* ip,
					const PIXEL* ip_limit,
					int pix_per_byte,
					size_t* o_image_dist, size_t* o_pix_distance);
};


