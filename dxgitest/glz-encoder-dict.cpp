#include "glz-encoder.hpp"

/* allocate window fields (no reset)*/
 bool SharedDictionary::glz_dictionary_window_create(UINT32 size)
{
	if (size > LZ_MAX_WINDOW_SIZE) {
		return FALSE;
	}

	window.size_limit = size;
	window.segs = (WindowImageSegment*)(cur_usr->malloc(cur_usr, sizeof(WindowImageSegment) * INIT_IMAGE_SEGS_NUM));

	if (!window.segs) {
		return FALSE;
	}

	window.segs_quota = INIT_IMAGE_SEGS_NUM;

	window.encoders_heads = (UINT32*)cur_usr->malloc(cur_usr,sizeof(UINT32) * max_encoders);

	if (!window.encoders_heads) {
		cur_usr->free(cur_usr, window.segs);
		return FALSE;
	}

	window.used_images_head = NULL;
	window.used_images_tail = NULL;
	window.free_images = NULL;
	window.pixels_so_far = 0;

	return TRUE;
}

/* turning all used images to free ones. If they are alive, calling the free_image callback for
   each one */
void SharedDictionary::glz_dictionary_window_reset_images(void)
{
	WindowImage* tmp;
	while (window.used_images_head) {
		tmp = window.used_images_head;
		window.used_images_head = window.used_images_head->next;
		if (tmp->is_alive) {
			cur_usr->free_image(cur_usr, tmp->usr_context);
		}
		tmp->next = window.free_images;
		tmp->is_alive = FALSE;
		window.free_images = tmp;
	}
	window.used_images_tail = NULL;
}

void SharedDictionary::glz_dictionary_reset_hash(void)
{
	memset(htab, 0, sizeof(HashEntry) * HASH_SIZE * HASH_CHAIN_SIZE);
}

/* initializes an empty window (segs and encoder_heads should be pre allocated.
   resets the image infos, and calls the free_image usr callback*/
void SharedDictionary::glz_dictionary_window_reset(void)
{
	INT32 i;
	WindowImageSegment* seg, * last_seg;

	last_seg = window.segs + window.segs_quota;
	/* reset free segs list */
	window.free_segs_head = 0;
	for (seg = window.segs, i = 0; seg < last_seg; seg++, i++) {
		seg->next = i + 1;
		seg->image = NULL;
		seg->lines = NULL;
		seg->lines_end = NULL;
		seg->pixels_num = 0;
		seg->pixels_so_far = 0;
	}
	window.segs[window.segs_quota - 1].next = NULL_IMAGE_SEG_ID;

	window.used_segs_head = NULL_IMAGE_SEG_ID;
	window.used_segs_tail = NULL_IMAGE_SEG_ID;

	// reset encoders heads
	for (i = 0; i < max_encoders; i++) {
		window.encoders_heads[i] = NULL_IMAGE_SEG_ID;
	}

	glz_dictionary_window_reset_images();
}


/*  NOTE - you should use this routine only when no encoder uses the dictionary. */
void SharedDictionary::glz_enc_dictionary_reset(GlzEncoderUsrContext* usr)
{
	
	cur_usr = usr;
	last_image_id = 0;
	glz_dictionary_window_reset();
	glz_dictionary_reset_hash();
}


UINT32 SharedDictionary::glz_enc_dictionary_get_size(void)
{
	return window.size_limit;
}

static inline int __get_pixels_num(LzImageType image_type, unsigned int num_lines, int stride)
{
	if (IS_IMAGE_TYPE_RGB[image_type]) {
		return num_lines * stride / RGB_BYTES_PER_PIXEL[image_type];
	}
	else {
		return num_lines * stride * PLT_PIXELS_PER_BYTE[image_type];
	}
}

/* Returns the logical head of the window after we add an image with the give size to its tail.
   Returns NULL when the window is empty, of when we have to empty the window in order
   to insert the new image. */
WindowImage* SharedDictionary::glz_dictionary_window_get_new_head(int new_image_size)
{
	UINT32 cur_win_size;
	WindowImage* cur_head;

	if ((UINT32)new_image_size > window.size_limit) {
		//printf("image is bigger than window\n");
	}

	if (!(new_image_size < window.size_limit)) {
		return NULL;
	}

	// the window is empty
	if (!window.used_images_head) {
		return NULL;
	}

	if (!(window.used_segs_head != NULL_IMAGE_SEG_ID)){
		return NULL;
	}
	if (!(window.used_segs_tail != NULL_IMAGE_SEG_ID)) {
		return NULL;
	}

	// used_segs_head is the latest logical head (the physical head may preceed it)
	cur_head = window.segs[window.used_segs_head].image;
	cur_win_size =window.segs[window.used_segs_tail].pixels_num +
		window.segs[window.used_segs_tail].pixels_so_far -
		window.segs[window.used_segs_head].pixels_so_far;

	while ((cur_win_size + new_image_size) > window.size_limit) {
		if (!cur_head) {
			return NULL;
		}
		cur_win_size -= cur_head->size;
		cur_head = cur_head->next;
	}

	return cur_head;
}

bool SharedDictionary::glz_dictionary_is_in_use(void)
{
	UINT32 i = 0;
	for (i = 0; i < max_encoders; i++) {
		if (window.encoders_heads[i] != NULL_IMAGE_SEG_ID) {
			return TRUE;
		}
	}
	return FALSE;
}

/* moves all the segments that were associated with the images to the free segments */
void SharedDictionary::glz_dictionary_window_free_image_segs(WindowImage* image)
{
	UINT32 old_free_head = window.free_segs_head;
	UINT32 seg_id, next_seg_id;

	if (!(image->first_seg != NULL_IMAGE_SEG_ID)) {
		return;
	}
	window.free_segs_head = image->first_seg;

	// retrieving the last segment of the image
	for (seg_id = image->first_seg, next_seg_id = window.segs[seg_id].next;
		(next_seg_id != NULL_IMAGE_SEG_ID) && (window.segs[next_seg_id].image == image);
		seg_id = next_seg_id, next_seg_id = window.segs[seg_id].next) {
	}

	// concatenate the free list
	window.segs[seg_id].next = old_free_head;
}

/* moves image to free list and "kill" it. Calls the free_image callback if was alive. */
void SharedDictionary::glz_dictionary_window_free_image(WindowImage* image)
{
	if (image->is_alive) {
		cur_usr->free_image(cur_usr, image->usr_context);
	}
	image->is_alive = FALSE;
	image->next = window.free_images;
	window.free_images = image;
}

/* remove from the window (and free relevant data) the images between the oldest physical head
   (inclusive) and the end_image (exclusive). If end_image is NULL, empties the window*/
void SharedDictionary::glz_dictionary_window_remove_head(UINT32 encoder_id, WindowImage* end_image)
{
	// note that the segs list heads (one per encoder) may be different than the
	// used_segs_head and it is updated somewhere else
	while (window.used_images_head != end_image) {
		WindowImage* image = window.used_images_head;

		glz_dictionary_window_free_image_segs(image);
		window.used_images_head = image->next;
		glz_dictionary_window_free_image(image);
	}

	if (!window.used_images_head) {
		window.used_segs_head = NULL_IMAGE_SEG_ID;
		window.used_segs_tail = NULL_IMAGE_SEG_ID;
		window.used_images_tail = NULL;
	}
	else {
		window.used_segs_head = end_image->first_seg;
	}
}

/* NOTE - it also updates the used_images_list*/
WindowImage* SharedDictionary::glz_dictionary_window_alloc_image(void)
{
	WindowImage* ret;

	if (window.free_images) {
		ret = window.free_images;
		window.free_images = ret->next;
	}
	else {
		if (!(ret = (WindowImage*)cur_usr->malloc(cur_usr,
			sizeof(*ret)))) {
			return NULL;
		}
	}

	ret->next = NULL;
	if (window.used_images_tail) {
		window.used_images_tail->next = ret;
	}
	window.used_images_tail = ret;

	if (!window.used_images_head) {
		window.used_images_head = ret;
	}
	return ret;
}

void SharedDictionary::glz_dictionary_window_segs_realloc(void)
{
	WindowImageSegment* new_segs;
	UINT32 new_quota = (MAX_IMAGE_SEGS_NUM < (window.segs_quota * 2)) ? MAX_IMAGE_SEGS_NUM : (window.segs_quota * 2);
	WindowImageSegment* seg;
	UINT32 i;

	AcquireSRWLockExclusive(&rw_alloc_lock);

	if (window.segs_quota == MAX_IMAGE_SEGS_NUM) {
		//printf("overflow in image segments window\n");
	}

	new_segs = (WindowImageSegment*)cur_usr->malloc(cur_usr, sizeof(WindowImageSegment) * new_quota);

	if (!new_segs) {
		//printf("realloc of dictionary window failed\n");
	}

	memcpy(new_segs, window.segs, sizeof(WindowImageSegment) * window.segs_quota);

	// resetting the new elements
	for (i = window.segs_quota, seg = new_segs + i; i < new_quota; i++, seg++) {
		seg->image = NULL;
		seg->lines = NULL;
		seg->lines_end = NULL;
		seg->pixels_num = 0;
		seg->pixels_so_far = 0;
		seg->next = i + 1;
	}
	new_segs[new_quota - 1].next = window.free_segs_head;
	window.free_segs_head = window.segs_quota;

	cur_usr->free(cur_usr, window.segs);
	window.segs = new_segs;
	window.segs_quota = new_quota;

	ReleaseSRWLockExclusive(&rw_alloc_lock);
}

/* NOTE - it doesn't update the used_segs list*/
UINT32 SharedDictionary::glz_dictionary_window_alloc_image_seg_impl(void)
{
	UINT32 seg_id;
	WindowImageSegment* seg;

	// TODO: when is it best to realloc? when full or when half full?
	if (window.free_segs_head == NULL_IMAGE_SEG_ID) {
		glz_dictionary_window_segs_realloc();
	}

	if (!(window.free_segs_head != NULL_IMAGE_SEG_ID)) {
		return 0;
	}

	seg_id = window.free_segs_head;
	seg = window.segs + seg_id;
	window.free_segs_head = seg->next;

	return seg_id;
}

UINT32 SharedDictionary::glz_dictionary_window_alloc_image_seg(WindowImage* image,
	int size, int stride,
	UINT8* lines, unsigned int num_lines)
{
	UINT32 seg_id = glz_dictionary_window_alloc_image_seg_impl();
	WindowImageSegment* seg = &window.segs[seg_id];

	seg->image = image;
	seg->lines = lines;
	seg->lines_end = lines + num_lines * stride;
	seg->pixels_num = size;
	seg->pixels_so_far = window.pixels_so_far;
	window.pixels_so_far += seg->pixels_num;

	seg->next = NULL_IMAGE_SEG_ID;

	return seg_id;
}

WindowImage* SharedDictionary::glz_dictionary_window_add_image(LzImageType image_type,
	int image_size, int image_height,
	int image_stride, UINT8* first_lines,
	unsigned int num_first_lines,
	GlzUsrImageContext* usr_image_context)
{
	unsigned int num_lines = num_first_lines;
	unsigned int row;
	UINT32 seg_id;
	UINT32 prev_seg_id = 0;
	UINT8* lines = first_lines;
	// alloc image info,update used head tail,  if used_head null - update  head
	WindowImage* image = glz_dictionary_window_alloc_image();
	image->id = last_image_id++;
	image->size = image_size;
	image->type = image_type;
	image->usr_context = usr_image_context;

	if (num_lines <= 0) {
		num_lines = cur_usr->more_lines(cur_usr, &lines);
		if (num_lines <= 0) {
			//printf("more lines failed\n");
		}
	}

	for (row = 0;;) {
		seg_id = glz_dictionary_window_alloc_image_seg(image,
			image_size * num_lines / image_height,
			image_stride,
			lines, num_lines);
		if (row == 0) {
			image->first_seg = seg_id;
		}
		else {
			window.segs[prev_seg_id].next = seg_id;
		}

		row += num_lines;
		if (row < (UINT32)image_height) {
			num_lines = cur_usr->more_lines(cur_usr, &lines);
			if (num_lines <= 0) {
				//printf("more lines failed\n");
			}
		}
		else {
			break;
		}
		prev_seg_id = seg_id;
	}

	if (window.used_segs_tail == NULL_IMAGE_SEG_ID) {
		window.used_segs_head = image->first_seg;
		window.used_segs_tail = seg_id;
	}
	else {
		int prev_tail = window.used_segs_tail;

		// The used segs may be in use by another thread which is during encoding
		// (read-only use - when going over the segs of an image,
		// see glz_encode_tmpl::compress).
		// Thus, the 'next' field of the list's tail can be accessed only
		// after all the new tail's data was set. Note that we are relying on
		// an atomic assignment (32 bit variable).
		// For the other thread that may read 'next' of the old tail, NULL_IMAGE_SEG_ID
		// is equivalent to a segment with an image id that is different
		// from the image id of the tail, so we don't need to further protect this field.
		window.segs[prev_tail].next = image->first_seg;
		window.used_segs_tail = seg_id;
	}
	image->is_alive = TRUE;

	return image;
}

WindowImage* SharedDictionary::glz_dictionary_pre_encode(UINT32 encoder_id, GlzEncoderUsrContext* usr,
	LzImageType image_type,
	int image_width, int image_height, int image_stride,
	UINT8* first_lines, unsigned int num_first_lines,
	GlzUsrImageContext* usr_image_context,
	UINT32* image_head_dist)
{
	WindowImage* new_win_head, * ret;
	int image_size;


	WaitForSingleObject(lock, INFINITE);

	cur_usr = usr;
	if (!(window.encoders_heads[encoder_id] == NULL_IMAGE_SEG_ID)) {
		return NULL;
	}

	image_size = __get_pixels_num(image_type, image_height, image_stride);
	new_win_head = glz_dictionary_window_get_new_head(image_size);

	if (!glz_dictionary_is_in_use()) {
		glz_dictionary_window_remove_head(encoder_id, new_win_head);
	}

	ret = glz_dictionary_window_add_image(image_type, image_size, image_height, image_stride,
		first_lines, num_first_lines, usr_image_context);

	if (new_win_head) {
		window.encoders_heads[encoder_id] = new_win_head->first_seg;
		*image_head_dist = (UINT32)(ret->id - new_win_head->id); // shouldn't be greater than 32
																   // bit because the window size is
																   // limited to 2^25
	}
	else {
		window.encoders_heads[encoder_id] = ret->first_seg;
		*image_head_dist = 0;
	}


	// update encoders head  (the other heads were already updated)
	ReleaseMutex(lock);
	AcquireSRWLockShared(&rw_alloc_lock);
	return ret;
}

void SharedDictionary::glz_dictionary_post_encode(UINT32 encoder_id, GlzEncoderUsrContext* usr)
{
	UINT32 i;
	UINT32 early_head_seg = NULL_IMAGE_SEG_ID;
	UINT32 this_encoder_head_seg;

	ReleaseSRWLockShared(&rw_alloc_lock);
	WaitForSingleObject(lock, INFINITE);
	cur_usr = usr;

	if (!(window.encoders_heads[encoder_id] != NULL_IMAGE_SEG_ID)) {
		return;
	}
	// get the earliest head in use (not including this encoder head)
	for (i = 0; i < max_encoders; i++) {
		if (i != encoder_id) {
			if (IMAGE_SEG_IS_EARLIER(this, window.encoders_heads[i], early_head_seg)) {
				early_head_seg = window.encoders_heads[i];
			}
		}
	}

	// possible only if early_head_seg == NULL
	if (IMAGE_SEG_IS_EARLIER(this, window.used_segs_head, early_head_seg)) {
		early_head_seg = window.used_segs_head;
	}

	this_encoder_head_seg = window.encoders_heads[encoder_id];

	if (!(early_head_seg != NULL_IMAGE_SEG_ID)) {
		return;
	}

	if (IMAGE_SEG_IS_EARLIER(this, this_encoder_head_seg, early_head_seg)) {
		if (!(this_encoder_head_seg == window.used_images_head->first_seg)) {
			return;
		}
		glz_dictionary_window_remove_head(encoder_id, window.segs[early_head_seg].image);
	}


	window.encoders_heads[encoder_id] = NULL_IMAGE_SEG_ID;
	ReleaseMutex(lock);
}

/* logic removal only */
void SharedDictionary::glz_dictionary_window_kill_image(WindowImage* image)
{
	image->is_alive = FALSE;
}

/* doesn't call the remove image callback */
void SharedDictionary::glz_enc_dictionary_remove_image(GlzEncDictImageContext* opaque_image,
													   GlzEncoderUsrContext* usr)
{
	
	WindowImage* image = (WindowImage*)opaque_image;
	cur_usr = usr;
	if (!(opaque_image)) {
		return;
	}
	glz_dictionary_window_kill_image(image);
}

void SharedDictionary::glz_dictionary_window_destroy(void)
{
	glz_dictionary_window_reset_images();

	if (window.segs) {
		cur_usr->free(cur_usr, window.segs);
		window.segs = NULL;
	}

	while (window.free_images) {
		WindowImage* tmp = window.free_images;
		window.free_images = tmp->next;
		cur_usr->free(cur_usr, tmp);
	}

	if (window.encoders_heads) {
		cur_usr->free(cur_usr,window.encoders_heads);
		window.encoders_heads = NULL;
	}
}

SharedDictionary::SharedDictionary(UINT32 size, UINT32 max_encoders_num, GlzEncoderUsrContext* usr)
{

	cur_usr = usr;
	last_image_id = 0;
	max_encoders = max_encoders_num;

	lock = CreateMutex(NULL, FALSE, NULL);
	InitializeSRWLock(&rw_alloc_lock);
	window.encoders_heads = NULL;

	// alloc window fields and reset
	if (!glz_dictionary_window_create(size)) {
		return;
	}

	// reset window and hash
	glz_enc_dictionary_reset(usr);

}

SharedDictionary::~SharedDictionary()
{
	glz_dictionary_window_destroy();
	CloseHandle(lock);
}
