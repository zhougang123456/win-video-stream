#include "glz-encoder.hpp"

GlzEncoder::GlzEncoder()
{
}

GlzEncoder::~GlzEncoder()
{
}

bool GlzEncoder::init(UINT8 id, SharedDictionary* dictionary, GlzEncoderUsrContext* usr_ctx)
{
	if (!usr_ctx || !usr_ctx->malloc || !usr_ctx->free || !usr_ctx->more_space) {
		return false;
	}
	id = id;
	usr = usr_ctx;
	dict = dictionary;
	return true;
}

bool GlzEncoder::encoder_reset(UINT8* io_ptr, UINT8* io_ptr_end)
{
	if (!(io_ptr <= io_ptr_end)) {
		return false;
	}
	io.bytes_count = io_ptr_end - io_ptr;
	io.start = io_ptr;
	io.now = io_ptr;
	io.end = io_ptr_end;
	io.last_copy = NULL;

	return TRUE;
}

/**************************************************************************
* Handling writing the encoded image to the output buffer
***************************************************************************/
int GlzEncoder::more_io_bytes(void)
{
	UINT8* io_ptr;
	int num_io_bytes = usr->more_space(usr, &io_ptr);
	io.bytes_count += num_io_bytes;
	io.now = io_ptr;
	io.end = io.now + num_io_bytes;
	return num_io_bytes;
}

void GlzEncoder::encode(UINT8 byte)
{
	if (io.now == io.end) {
		if (more_io_bytes() <= 0) {
			//printf"%s: no more bytes\n", __FUNCTION__);
		}
		if ((!io.now)) {
			return;
		}
	}

	if (!(io.now < io.end)) {
		return;
	}
	*(io.now++) = byte;
}

void GlzEncoder::encode_32(unsigned int word)
{
	encode((UINT8)(word >> 24));
	encode((UINT8)(word >> 16) & 0x0000ff);
	encode((UINT8)(word >> 8) & 0x0000ff);
	encode((UINT8)(word & 0x0000ff));
}

void GlzEncoder::encode_64(UINT64 word)
{
	encode_32((UINT32)(word >> 32));
	encode_32((UINT32)(word & 0xffffffffu));
}

void GlzEncoder::encode_copy_count(UINT8 copy_count)
{
	encode(copy_count);
	io.last_copy = io.now - 1; // io_now cannot be the first byte of the buffer
}

void GlzEncoder::update_copy_count(UINT8 copy_count)
{
	if (!(io.last_copy)) {
		return;
	}
	*(io.last_copy) = copy_count;
}

// decrease the io ptr by 1
void GlzEncoder::compress_output_prev(void)
{
	// io_now cannot be the first byte of the buffer
	io.now--;
	// the function should be called only when copy count is written unnecessarily by glz_compress
	if (!(io.now == io.last_copy)) {
		return;
	}
}

#define LZ_EXPECT_CONDITIONAL(c) (c)
#define LZ_UNEXPECT_CONDITIONAL(c) (c)

#define BOUND_OFFSET 2
#define LIMIT_OFFSET 6
#define MIN_FILE_SIZE 4

#define MAX_PIXEL_SHORT_DISTANCE 4096       // (1 << 12)
#define MAX_PIXEL_MEDIUM_DISTANCE 131072    // (1 << 17)  2 ^ (12 + 5)
#define MAX_PIXEL_LONG_DISTANCE 33554432    // (1 << 25)  2 ^ (12 + 5 + 8)
#define MAX_IMAGE_DIST 16777215             // (1 << 24 - 1)

#define SHORT_PIX_IMAGE_DIST_LEVEL_1 64 //(1 << 6)
#define SHORT_PIX_IMAGE_DIST_LEVEL_2 16384 // (1 << 14)
#define SHORT_PIX_IMAGE_DIST_LEVEL_3 4194304 // (1 << 22)
#define FAR_PIX_IMAGE_DIST_LEVEL_1 256 // (1 << 8)
#define FAR_PIX_IMAGE_DIST_LEVEL_2 65536 // (1 << 16)
#define FAR_PIX_IMAGE_DIST_LEVEL_3 16777216 // (1 << 24)

/* if image_distance = 0, pixel_distance is the distance between the matching pixels.
  Otherwise, it is the offset from the beginning of the referred image */

/* compute the size of the encoding except for the match length*/
static inline int get_encode_ref_size(UINT32 image_distance, size_t pixel_distance)

{
	int encode_size;
	/* encoding the rest of the pixel distance and the image_dist and its 2 control bits */

	/* The first 2 MSB bits indicate how many more bytes should be read for image dist */
	if (pixel_distance < MAX_PIXEL_SHORT_DISTANCE) {
		if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_1) {
			encode_size = 3;
		}
		else if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_2) {
			encode_size = 4;
		}
		else if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_3) {
			encode_size = 5;
		}
		else {
			encode_size = 6;
		}
	}
	else {
		/* the third MSB bit indicates if the pixel_distance is medium/long*/
		UINT8 long_dist_control = (pixel_distance < MAX_PIXEL_MEDIUM_DISTANCE) ? 0 : 32;
		if (image_distance == 0) {
			encode_size = 3;
		}
		else if (image_distance < FAR_PIX_IMAGE_DIST_LEVEL_1) {
			encode_size = 4;
		}
		else if (image_distance < FAR_PIX_IMAGE_DIST_LEVEL_2) {
			encode_size = 5;
		}
		else {
			encode_size = 6;
		}

		if (long_dist_control) {
			encode_size++;
		}
	}
	return encode_size;
}

/* if image_distance = 0, pixel_distance is the distance between the matching pixels.
  Otherwise, it is the offset from the beginning of the referred image */
/* actually performing the encoding */
void GlzEncoder::encode_match(UINT32 image_distance, size_t pixel_distance, size_t len)

{
	/* encoding the match length + Long/Short dist bit +  12 LSB pixels of pixel_distance*/
	if (len < 7) {
		if (pixel_distance < MAX_PIXEL_SHORT_DISTANCE) {
			encode((UINT8)((len << 5) + (pixel_distance & 0x0f)));
		}
		else {
			encode((UINT8)((len << 5) + 16 + (pixel_distance & 0x0f)));
		}
		encode((UINT8)((pixel_distance >> 4) & 255));
	}
	else {
		if (pixel_distance < MAX_PIXEL_SHORT_DISTANCE) {
			encode((UINT8)((7 << 5) + (pixel_distance & 0x0f)));
		}
		else {
			encode((UINT8)((7 << 5) + 16 + (pixel_distance & 0x0f)));
		}
		for (len -= 7; len >= 255; len -= 255) {
			encode(255);
		}
		encode((UINT8)len);
		encode((UINT8)((pixel_distance >> 4) & 255));
	}
	/* encoding the rest of the pixel distance and the image_dist and its 2 control bits */
	/* The first 2 MSB bits indicate how many more bytes should be read for image dist */
	if (pixel_distance < MAX_PIXEL_SHORT_DISTANCE) {
		if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_1) {
			encode((UINT8)(image_distance & 0x3f));
		}
		else if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_2) {
			encode((UINT8)((1 << 6) + (image_distance & 0x3f)));
			encode((UINT8)((image_distance >> 6) & 255));
		}
		else if (image_distance < SHORT_PIX_IMAGE_DIST_LEVEL_3) {
			encode((UINT8)((1 << 7) + (image_distance & 0x3f)));
			encode((UINT8)((image_distance >> 6) & 255));
			encode((UINT8)((image_distance >> 14) & 255));
		}
		else {
			encode((UINT8)((1 << 7) + (1 << 6) + (image_distance & 0x3f)));
			encode((UINT8)((image_distance >> 6) & 255));
			encode((UINT8)((image_distance >> 14) & 255));
			encode((UINT8)((image_distance >> 22) & 255));
		}
	}
	else {
		/* the third MSB bit indicates if the pixel_distance is medium/long*/
		UINT8 long_dist_control = (pixel_distance < MAX_PIXEL_MEDIUM_DISTANCE) ? 0 : 32;
		if (image_distance == 0) {
			encode((UINT8)(long_dist_control + ((pixel_distance >> 12) & 31)));
		}
		else if (image_distance < FAR_PIX_IMAGE_DIST_LEVEL_1) {
			encode((UINT8)(long_dist_control + (1 << 6) + ((pixel_distance >> 12) & 31)));
			encode((UINT8)(image_distance & 255));
		}
		else if (image_distance < FAR_PIX_IMAGE_DIST_LEVEL_2) {
			encode((UINT8)(long_dist_control + (1 << 7) + ((pixel_distance >> 12) & 31)));
			encode((UINT8)(image_distance & 255));
			encode((UINT8)((image_distance >> 8) & 255));
		}
		else {
			encode((UINT8)(long_dist_control + (1 << 7) + (1 << 6) +
				((pixel_distance >> 12) & 31)));
			encode((UINT8)(image_distance & 255));
			encode((UINT8)((image_distance >> 8) & 255));
			encode((UINT8)((image_distance >> 16) & 255));
		}

		if (long_dist_control) {
			encode((UINT8)((pixel_distance >> 17) & 255));
		}
	}
}

#define LZ_RGB32
#define DJB2_START 5381
#define DJB2_HASH(hash, c) (hash = ((hash << 5) + hash) ^ (c)) //|{hash = ((hash << 5) + hash) + c;}

/*
	For each pixel type the following macros are defined:
	PIXEL                         : input type
	FNAME(name)
	ENCODE_PIXEL(pixel) : writing a pixel to the compressed buffer (byte by byte)
	SAME_PIXEL(pix1, pix2)         : comparing two pixels
	HASH_FUNC(value, pix_ptr)    : hash func of 3 consecutive pixels
*/


#if  defined(LZ_RGB24) || defined(LZ_RGB32)
#define ENCODE_PIXEL(pix) {encode((pix).b); encode((pix).g); encode((pix).r);}
#define MIN_REF_ENCODE_SIZE 2
#define MAX_REF_ENCODE_SIZE 2
#define SAME_PIXEL(p1, p2) ((p1).r == (p2).r && (p1).g == (p2).g && (p1).b == (p2).b)
#define HASH_FUNC(v, p) {    \
    v = DJB2_START;          \
    DJB2_HASH(v, p[0].r);    \
    DJB2_HASH(v, p[0].g);    \
    DJB2_HASH(v, p[0].b);    \
    DJB2_HASH(v, p[1].r);    \
    DJB2_HASH(v, p[1].g);    \
    DJB2_HASH(v, p[1].b);    \
    DJB2_HASH(v, p[2].r);    \
    DJB2_HASH(v, p[2].g);    \
    DJB2_HASH(v, p[2].b);    \
    v &= HASH_MASK;          \
    }
#endif

#define PIXEL_ID(pix_ptr, seg_ptr, pix_per_byte) \
    (((pix_ptr) - ((PIXEL *)(seg_ptr)->lines)) * pix_per_byte + (seg_ptr)->pixels_so_far)

#define PIXEL_DIST(src_pix_ptr, src_seg_ptr, ref_pix_ptr, ref_seg_ptr, pix_per_byte) \
    ((PIXEL_ID(src_pix_ptr,src_seg_ptr, pix_per_byte) - \
    PIXEL_ID(ref_pix_ptr, ref_seg_ptr, pix_per_byte)) / pix_per_byte)

/* returns the length of the match. 0 if no match.
  if image_distance = 0, pixel_distance is the distance between the matching pixels.
  Otherwise, it is the offset from the beginning of the referred image */
size_t GlzEncoder::do_match(SharedDictionary* dict,
	WindowImageSegment* ref_seg, const PIXEL* ref,
	const PIXEL* ref_limit,
	WindowImageSegment* ip_seg, const PIXEL* ip,
	const PIXEL* ip_limit,
	int pix_per_byte,
	size_t* o_image_dist, size_t* o_pix_distance)
{
	int encode_size;
	const PIXEL* tmp_ip = ip;
	const PIXEL* tmp_ref = ref;

	if (ref > (ref_limit - MIN_REF_ENCODE_SIZE)) {
		return 0; // in case the hash entry is not relevant
	}

	/* min match length == MIN_REF_ENCODE_SIZE (depends on pixel type) */

	if (!SAME_PIXEL(*tmp_ref, *tmp_ip)) {
		return 0;
	}
	else {
		tmp_ref++;
		tmp_ip++;
	}

	if (!SAME_PIXEL(*tmp_ref, *tmp_ip)) {
		return 0;
	}
	else {
		tmp_ref++;
		tmp_ip++;
	}

	* o_image_dist = ip_seg->image->id - ref_seg->image->id;

	if (!(*o_image_dist)) { // the ref is inside the same image - encode distance
		*o_pix_distance = PIXEL_DIST(ip, ip_seg, ref, ref_seg, pix_per_byte);
	}
	else { // the ref is at different image - encode offset from the image start
		*o_pix_distance = PIXEL_DIST(ref, ref_seg,
			(PIXEL*)(dict->window.segs[ref_seg->image->first_seg].lines),
			&dict->window.segs[ref_seg->image->first_seg],
			pix_per_byte);
	}

	if ((*o_pix_distance == 0) || (*o_pix_distance >= MAX_PIXEL_LONG_DISTANCE) ||
		(*o_image_dist > MAX_IMAGE_DIST)) {
		return 0;
	}

	/* continue the match*/
	while ((tmp_ip < ip_limit) && (tmp_ref < ref_limit)) {
		if (!SAME_PIXEL(*tmp_ref, *tmp_ip)) {
			break;
		}
		else {
			tmp_ref++;
			tmp_ip++;
		}
	}

	if ((tmp_ip - ip) > MAX_REF_ENCODE_SIZE) {
		return (tmp_ip - ip);
	}

	encode_size = get_encode_ref_size(*o_image_dist, *o_pix_distance);

	// min number of identical pixels for a match
#if defined(LZ_RGB16)
	encode_size /= 2;
#elif defined(LZ_RGB24) || defined(LZ_RGB32)
	encode_size /= 3;
#endif

	encode_size++; // the minimum match
	// match len is smaller than the encoding - not worth encoding
	if ((tmp_ip - ip) < encode_size) {
		return 0;
	}
	return (tmp_ip - ip);
}

/* compresses one segment starting from 'from'.
   In order to encode a match, we use pixels resolution when we encode RGB image,
   and bytes count when we encode PLT.
*/
void GlzEncoder::compress_seg(UINT32 seg_idx, PIXEL* from, int copied)
{
	WindowImageSegment* seg = &dict->window.segs[seg_idx];
	const PIXEL* ip = from;
	const PIXEL* ip_bound = (PIXEL*)(seg->lines_end) - BOUND_OFFSET;
	const PIXEL* ip_limit = (PIXEL*)(seg->lines_end) - LIMIT_OFFSET;
	int hval;
	int copy = copied;
#ifdef  LZ_PLT
	int pix_per_byte = PLT_PIXELS_PER_BYTE[encoder->cur_image.type];
#else
	int pix_per_byte = 1;
#endif

#ifdef DEBUG_ENCODE
	int n_encoded = 0;
#endif

	if (copy == 0) {
		encode_copy_count(MAX_COPY - 1);
	}

	while (LZ_EXPECT_CONDITIONAL(ip < ip_limit)) {
		const PIXEL* ref;
		const PIXEL* ref_limit;
		WindowImageSegment* ref_seg;
		UINT32 ref_seg_idx;
		size_t pix_dist;
		size_t image_dist;
		/* minimum match length */
		size_t len = 0;

		/* comparison starting-point */
		const PIXEL* anchor = ip;

		/* check for a run */

		if (LZ_EXPECT_CONDITIONAL(ip > (PIXEL*)(seg->lines))) {
			if (SAME_PIXEL(ip[-1], ip[0]) && SAME_PIXEL(ip[0], ip[1]) && SAME_PIXEL(ip[1], ip[2])) {
				PIXEL x;
				pix_dist = 1;
				image_dist = 0;

				ip += 3;
				ref = anchor + 2;
				ref_limit = (PIXEL*)(seg->lines_end);
				len = 3;

				x = *ref;

				while (ip < ip_bound) { // TODO: maybe separate a run from the same seg or from
									   // different ones in order to spare ref < ref_limit
					if (!SAME_PIXEL(*ip, x)) {
						ip++;
						break;
					}
					else {
						ip++;
						len++;
					}
				}

				goto match;
			} // END RLE MATCH
		}

		/* find potential match */
		HASH_FUNC(hval, ip);

		ref_seg_idx = dict->htab[hval].image_seg_idx;
		ref_seg = dict->window.segs + ref_seg_idx;
		if (REF_SEG_IS_VALID(dict, id,
			ref_seg, seg)) {
			ref = ((PIXEL*)ref_seg->lines) + dict->htab[hval].ref_pix_idx;
			ref_limit = (PIXEL*)ref_seg->lines_end;
			len = do_match(dict, ref_seg, ref, ref_limit, seg, ip, ip_bound,
				pix_per_byte,
				&image_dist, &pix_dist);
		}

		/* update hash table */
		UPDATE_HASH(dict, hval, seg_idx, anchor - ((PIXEL*)seg->lines));

		if (!len) {
			goto literal;
		}

	match:        // RLE or dictionary (both are encoded by distance from ref (-1) and length)
#ifdef DEBUG_ENCODE
		printf(", match(%zu, %zu, %zu)", image_dist, pix_dist, len);
		n_encoded += len;
#endif

		/* distance is biased */
		if (!image_dist) {
			pix_dist--;
		}

		/* if we have copied something, adjust the copy count */
		if (copy) {
			/* copy is biased, '0' means 1 byte copy */
			update_copy_count(copy - 1);
		}
		else {
			/* back, to overwrite the copy count */
			compress_output_prev();
		}

		/* reset literal counter */
		copy = 0;

		/* length is biased, '1' means a match of 3 pixels for PLT and alpha*/
		/* for RGB 16 1 means 2 */
		/* for RGB24/32 1 means 1...*/
		ip = anchor + len - 2;

#if defined(LZ_RGB16)
		len--;
#elif defined(LZ_PLT) || defined(LZ_RGB_ALPHA)
		len -= 2;
#endif
		if (!(len > 0)) {
			return;
		}
		encode_match(image_dist, pix_dist, len);

	/* update the hash at match boundary */
#if defined(LZ_RGB24) || defined(LZ_RGB32)
		if (ip > anchor)
#endif
		{
			HASH_FUNC(hval, ip);
			UPDATE_HASH(dict, hval, seg_idx, ip - ((PIXEL*)seg->lines));
		}
		ip++;
#if defined(LZ_RGB24) || defined(LZ_RGB32)
		if (ip > anchor)
#endif
		{
			HASH_FUNC(hval, ip);
			UPDATE_HASH(dict, hval, seg_idx, ip - ((PIXEL*)seg->lines));
		}
		ip++;
		/* assuming literal copy */
		encode_copy_count(MAX_COPY - 1);
		continue;

	literal:
#ifdef DEBUG_ENCODE
		printf(", copy");
		n_encoded++;
#endif
		ENCODE_PIXEL(*anchor);
		anchor++;
		ip = anchor;
		copy++;

		if (LZ_UNEXPECT_CONDITIONAL(copy == MAX_COPY)) {
			copy = 0;
			encode_copy_count(MAX_COPY - 1);
		}
	} // END LOOP (ip < ip_limit)


	/* left-over as literal copy */
	ip_bound++;
	while (ip <= ip_bound) {
#ifdef DEBUG_ENCODE
		printf(", copy");
		n_encoded++;
#endif
		ENCODE_PIXEL(*ip);
		ip++;
		copy++;
		if (copy == MAX_COPY) {
			copy = 0;
			encode_copy_count(MAX_COPY - 1);
		}
	}

	/* if we have copied something, adjust the copy length */
	if (copy) {
		update_copy_count(copy - 1);
	}
	else {
		compress_output_prev();
	}
#ifdef DEBUG_ENCODE
	printf("\ntotal encoded=%d\n", n_encoded);
#endif
}


/*  If the file is very small, copies it.
	copies the first two pixels of the first segment, and sends the segments
	one by one to compress_seg.
	the number of bytes compressed are stored inside encoder. */
void GlzEncoder::glz_rgb32_compress(void)
{

	UINT32 seg_id = cur_image.first_win_seg;
	PIXEL* ip;
	int hval;

	// fetch the first image segment that is not too small
	while ((seg_id != NULL_IMAGE_SEG_ID) &&
		(dict->window.segs[seg_id].image->id == cur_image.id) &&
		((((PIXEL*)dict->window.segs[seg_id].lines_end) -
		((PIXEL*)dict->window.segs[seg_id].lines)) < 4)) {
		// coping the segment
		if (dict->window.segs[seg_id].lines != dict->window.segs[seg_id].lines_end) {
			ip = (PIXEL*)dict->window.segs[seg_id].lines;
			// Note: we assume MAX_COPY > 3
			encode_copy_count((UINT8)((((PIXEL*)dict->window.segs[seg_id].lines_end)
				- ((PIXEL*)dict->window.segs[seg_id].lines)) - 1));
			while (ip < (PIXEL*)dict->window.segs[seg_id].lines_end) {
				ENCODE_PIXEL(*ip);
				ip++;
			}
		}
		seg_id = dict->window.segs[seg_id].next;
	}

	if ((seg_id == NULL_IMAGE_SEG_ID) ||
		(dict->window.segs[seg_id].image->id != cur_image.id)) {
		return;
	}

	ip = (PIXEL*)dict->window.segs[seg_id].lines;

	encode_copy_count(MAX_COPY - 1);

	HASH_FUNC(hval, ip);
	UPDATE_HASH(dict, hval, seg_id, 0);

	ENCODE_PIXEL(*ip);
	ip++;
	ENCODE_PIXEL(*ip);
	ip++;
#ifdef DEBUG_ENCODE
	printf("copy, copy");
#endif
	// compressing the first segment
	compress_seg(seg_id, ip, 2);
	// compressing the next segments
	for (seg_id = dict->window.segs[seg_id].next;
		seg_id != NULL_IMAGE_SEG_ID && (
			dict->window.segs[seg_id].image->id == cur_image.id);
		seg_id = dict->window.segs[seg_id].next) {
		compress_seg(seg_id, (PIXEL*)dict->window.segs[seg_id].lines, 0);
	}
}

int GlzEncoder::glz_encode(LzImageType type, int width, int height,
	int top_down, UINT8* lines, unsigned int num_lines, int stride,
	UINT8* io_ptr, unsigned int num_io_bytes, GlzUsrImageContext* usr_context,
	GlzEncDictImageContext** o_enc_dict_context)
{
	WindowImage* dict_image = NULL;
	UINT8* io_ptr_end = io_ptr + num_io_bytes;
	UINT32 win_head_image_dist = 0;

	if (IS_IMAGE_TYPE_PLT[type]) {
		if (stride > (width / PLT_PIXELS_PER_BYTE[type])) {
			if (((width % PLT_PIXELS_PER_BYTE[type]) == 0) || (
				(stride - (width / PLT_PIXELS_PER_BYTE[type])) > 1)) {
				//printf("stride overflows (plt)\n");
			}
		}
	}
	else {
		if (stride != width * RGB_BYTES_PER_PIXEL[type]) {
			//printf("stride != width*bytes_per_pixel (rgb)\n");
		}
	}

	// assign the output buffer
	if (!encoder_reset(io_ptr, io_ptr_end)) {
		//printf("lz encoder io reset failed\n");
	}

	// first read the list of the image segments into the dictionary window
	dict_image = dict->glz_dictionary_pre_encode(id, usr,
		type, width, height, stride,
		lines, num_lines, usr_context, &win_head_image_dist);
	*o_enc_dict_context = (GlzEncDictImageContext*)dict_image;

	cur_image.type = type;
	cur_image.id = dict_image->id;
	cur_image.first_win_seg = dict_image->first_seg;

	encode_32(LZ_MAGIC);
	encode_32(LZ_VERSION);
	if (top_down) {
		encode((type & LZ_IMAGE_TYPE_MASK) | (1 << LZ_IMAGE_TYPE_LOG));
	}
	else {
		encode((type & LZ_IMAGE_TYPE_MASK));
	}

	encode_32(width);
	encode_32(height);
	encode_32(stride);
	encode_64(dict_image->id);
	encode_32(win_head_image_dist);

	switch (cur_image.type) {
	case LZ_IMAGE_TYPE_RGB24:
		//glz_rgb24_compress(encoder);
		break;
	case LZ_IMAGE_TYPE_RGB32:
		glz_rgb32_compress();
		break;
	case LZ_IMAGE_TYPE_INVALID:
	default:
		;
		//printf("bad image type\n");
	}
	dict->glz_dictionary_post_encode(id, usr);
	// move all the used segments to the free ones
	io.bytes_count -= (io.end - io.now);
	return io.bytes_count;
}
