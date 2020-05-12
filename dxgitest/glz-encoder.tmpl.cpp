#define DJB2_START 5381
#define DJB2_HASH(hash, c) (hash = ((hash << 5) + hash) ^ (c)) //|{hash = ((hash << 5) + hash) + c;}

/*
	For each pixel type the following macros are defined:
	PIXEL                         : input type
	FNAME(name)
	ENCODE_PIXEL(encoder, pixel) : writing a pixel to the compressed buffer (byte by byte)
	SAME_PIXEL(pix1, pix2)         : comparing two pixels
	HASH_FUNC(value, pix_ptr)    : hash func of 3 consecutive pixels
*/




#if  defined(LZ_RGB24) || defined(LZ_RGB32)
#define ENCODE_PIXEL(e, pix) {encode(e, (pix).b); encode(e, (pix).g); encode(e, (pix).r);}
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
static inline size_t FNAME(do_match)(SharedDictionary* dict,
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

#if defined(LZ_PLT) || defined(LZ_RGB_ALPHA)
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

#endif


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
static void FNAME(compress_seg)(Encoder* encoder, UINT32 seg_idx, PIXEL* from, int copied)
{
	WindowImageSegment* seg = &encoder->dict->window.segs[seg_idx];
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
		encode_copy_count(encoder, MAX_COPY - 1);
	}


	while (LZ_EXPECT_CONDITIONAL(ip < ip_limit)) {
		const PIXEL* ref;
		const PIXEL* ref_limit;
		WindowImageSegment* ref_seg;
		uint32_t ref_seg_idx;
		size_t pix_dist;
		size_t image_dist;
		/* minimum match length */
		size_t len = 0;

		/* comparison starting-point */
		const PIXEL* anchor = ip;
#ifdef CHAINED_HASH
		int hash_id = 0;
		size_t best_len = 0;
		size_t best_pix_dist = 0;
		size_t best_image_dist = 0;
#endif

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

#ifdef CHAINED_HASH
		for (hash_id = 0; hash_id < HASH_CHAIN_SIZE; hash_id++) {
			ref_seg_idx = encoder->dict->htab[hval][hash_id].image_seg_idx;
#else
		ref_seg_idx = encoder->dict->htab[hval].image_seg_idx;
#endif
		ref_seg = encoder->dict->window.segs + ref_seg_idx;
		if (REF_SEG_IS_VALID(encoder->dict, encoder->id,
			ref_seg, seg)) {
#ifdef CHAINED_HASH
			ref = ((PIXEL*)ref_seg->lines) + encoder->dict->htab[hval][hash_id].ref_pix_idx;
#else
			ref = ((PIXEL*)ref_seg->lines) + encoder->dict->htab[hval].ref_pix_idx;
#endif
			ref_limit = (PIXEL*)ref_seg->lines_end;

			len = FNAME(do_match)(encoder->dict, ref_seg, ref, ref_limit, seg, ip, ip_bound,
				pix_per_byte,
				&image_dist, &pix_dist);

#ifdef CHAINED_HASH
			// TODO. not compare len but rather len - encode_size
			if (len > best_len) {
				best_len = len;
				best_pix_dist = pix_dist;
				best_image_dist = image_dist;
			}
#endif
		}

#ifdef CHAINED_HASH
		} // end chain loop
	len = best_len;
	pix_dist = best_pix_dist;
	image_dist = best_image_dist;
#endif

	/* update hash table */
	UPDATE_HASH(encoder->dict, hval, seg_idx, anchor - ((PIXEL*)seg->lines));

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
		update_copy_count(encoder, copy - 1);
	}
	else {
		/* back, to overwrite the copy count */
		compress_output_prev(encoder);
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
	GLZ_ASSERT(encoder->usr, len > 0);
	encode_match(encoder, image_dist, pix_dist, len);

	/* update the hash at match boundary */
#if defined(LZ_RGB16) || defined(LZ_RGB24) || defined(LZ_RGB32)
	if (ip > anchor)
#endif
	{
		HASH_FUNC(hval, ip);
		UPDATE_HASH(encoder->dict, hval, seg_idx, ip - ((PIXEL*)seg->lines));
	}
	ip++;
#if defined(LZ_RGB24) || defined(LZ_RGB32)
	if (ip > anchor)
#endif
	{
		HASH_FUNC(hval, ip);
		UPDATE_HASH(encoder->dict, hval, seg_idx, ip - ((PIXEL*)seg->lines));
	}
	ip++;
	/* assuming literal copy */
	encode_copy_count(encoder, MAX_COPY - 1);
	continue;

	literal:
#ifdef DEBUG_ENCODE
	printf(", copy");
	n_encoded++;
#endif
	ENCODE_PIXEL(encoder, *anchor);
	anchor++;
	ip = anchor;
	copy++;

	if (LZ_UNEXPECT_CONDITIONAL(copy == MAX_COPY)) {
		copy = 0;
		encode_copy_count(encoder, MAX_COPY - 1);
	}
	} // END LOOP (ip < ip_limit)


	/* left-over as literal copy */
ip_bound++;
while (ip <= ip_bound) {
#ifdef DEBUG_ENCODE
	printf(", copy");
	n_encoded++;
#endif
	ENCODE_PIXEL(encoder, *ip);
	ip++;
	copy++;
	if (copy == MAX_COPY) {
		copy = 0;
		encode_copy_count(encoder, MAX_COPY - 1);
	}
}

/* if we have copied something, adjust the copy length */
if (copy) {
	update_copy_count(encoder, copy - 1);
}
else {
	compress_output_prev(encoder);
}
#ifdef DEBUG_ENCODE
printf("\ntotal encoded=%d\n", n_encoded);
#endif
}


/*  If the file is very small, copies it.
	copies the first two pixels of the first segment, and sends the segments
	one by one to compress_seg.
	the number of bytes compressed are stored inside encoder. */
void FNAME(compress)(Encoder* encoder)
{
	uint32_t seg_id = encoder->cur_image.first_win_seg;
	PIXEL* ip;
	SharedDictionary* dict = encoder->dict;
	int hval;

	// fetch the first image segment that is not too small
	while ((seg_id != NULL_IMAGE_SEG_ID) &&
		(dict->window.segs[seg_id].image->id == encoder->cur_image.id) &&
		((((PIXEL*)dict->window.segs[seg_id].lines_end) -
		((PIXEL*)dict->window.segs[seg_id].lines)) < 4)) {
		// coping the segment
		if (dict->window.segs[seg_id].lines != dict->window.segs[seg_id].lines_end) {
			ip = (PIXEL*)dict->window.segs[seg_id].lines;
			// Note: we assume MAX_COPY > 3
			encode_copy_count(encoder, (uint8_t)(
				(((PIXEL*)dict->window.segs[seg_id].lines_end) -
				((PIXEL*)dict->window.segs[seg_id].lines)) - 1));
			while (ip < (PIXEL*)dict->window.segs[seg_id].lines_end) {
				ENCODE_PIXEL(encoder, *ip);
				ip++;
			}
		}
		seg_id = dict->window.segs[seg_id].next;
	}

	if ((seg_id == NULL_IMAGE_SEG_ID) ||
		(dict->window.segs[seg_id].image->id != encoder->cur_image.id)) {
		return;
	}

	ip = (PIXEL*)dict->window.segs[seg_id].lines;


	encode_copy_count(encoder, MAX_COPY - 1);

	HASH_FUNC(hval, ip);
	UPDATE_HASH(encoder->dict, hval, seg_id, 0);

	ENCODE_PIXEL(encoder, *ip);
	ip++;
	ENCODE_PIXEL(encoder, *ip);
	ip++;
#ifdef DEBUG_ENCODE
	printf("copy, copy");
#endif
	// compressing the first segment
	FNAME(compress_seg)(encoder, seg_id, ip, 2);

	// compressing the next segments
	for (seg_id = dict->window.segs[seg_id].next;
		seg_id != NULL_IMAGE_SEG_ID && (
			dict->window.segs[seg_id].image->id == encoder->cur_image.id);
		seg_id = dict->window.segs[seg_id].next) {
		FNAME(compress_seg)(encoder, seg_id, (PIXEL*)dict->window.segs[seg_id].lines, 0);
	}
}

