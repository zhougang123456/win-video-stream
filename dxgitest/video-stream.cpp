
#include "video-stream.hpp";

#define SAMPLE_JUMP 15
#define SAME_PIXEL_WEIGHT 0.5
#define NOT_CONTRAST_PIXELS_WEIGHT -0.25
#define CONTRAST_PIXELS_WEIGHT 1.0
#define CONTRAST_TH 60
#define CONTRASTING(n) ((n) <= -CONTRAST_TH || (n) >= CONTRAST_TH)
#define GRADUAL_HIGH_RGB24_TH -0.03
// setting a more permissive threshold for stream identification in order
// not to miss streams that were artificially scaled on the guest (e.g., full screen view
// in window media player 12). see red_stream_add_frame
#define GRADUAL_MEDIUM_SCORE_TH 0.002

typedef enum {
	GRADUAL_INVALID,
	GRADUAL_NOT_AVAIL,
	GRADUAL_LOW,
	GRADUAL_MEDIUM,
	GRADUAL_HIGH,
} GradualType;

static const double PIX_PAIR_SCORE[] = {
	SAME_PIXEL_WEIGHT,
	CONTRAST_PIXELS_WEIGHT,
	NOT_CONTRAST_PIXELS_WEIGHT,
};

static bool Rect_Is_Equal(RECT* rect1, RECT* rect2)
{
	if (rect1->left == rect2->left && rect1->right == rect2->right
		&& rect1->top == rect2->top && rect1->bottom == rect2->bottom)
	{
		return true;
	}
	return false;
}

static bool Rect_Is_Contain(RECT* rect1, RECT* rect2)
{
	if (rect1->left <= rect2->left && rect1->right >= rect2->right
		&& rect1->top <= rect2->top && rect1->bottom >= rect2->bottom)
	{
		return true;
	}
	return false;
}

static void Copy_Rect(RECT* dest, RECT* src)
{
	dest->left = src->left;
	dest->right = src->right;
	dest->top = src->top;
	dest->bottom = src->bottom;
}

static inline int pixelcmp (BYTE* p1, BYTE* p2)
{
	int diff, any_different;

	diff = p1[0] - p2[0];
	any_different = diff;
	if (CONTRASTING(diff)) {
		return 1;
	}

	diff = p1[1] - p2[1];
	any_different |= diff;
	if (CONTRASTING(diff)) {
		return 1;
	}

	diff = p1[2] - p2[2];
	any_different |= diff;
	if (CONTRASTING(diff)) {
		return 1;
	}

	if (!any_different) {
		return 0;
	}
	else {
		return 2;
	}
}
static inline double pixels_square_score (BYTE* line1, BYTE* line2, int bpp)
{
	double ret;
	int any_different = 0;
	int cmp_res;
	cmp_res = pixelcmp (line1, line1 + bpp);
	any_different |= cmp_res;
	ret = PIX_PAIR_SCORE [cmp_res];
	cmp_res =pixelcmp (line1, line2);
	any_different |= cmp_res;
	ret += PIX_PAIR_SCORE [cmp_res];
	cmp_res = pixelcmp (line1, line2 + bpp);
	any_different |= cmp_res;
	ret += PIX_PAIR_SCORE [cmp_res];

	// ignore squares where all pixels are identical
	if (!any_different) {
		ret = 0;
	}

	return ret;
}

static GradualType get_graduality_level(Drawable* drawable)
{
	int width = drawable->width;
	int num_lines = drawable->height;
	BYTE* lines = drawable->data;
	int bpp = drawable->bpp;
	int jump = bpp * ((SAMPLE_JUMP % width) ? SAMPLE_JUMP : SAMPLE_JUMP - 1);
	BYTE* cur_pix = lines + width / 2 * bpp;
	BYTE* bottom_pix;
	BYTE* last_line = lines + (num_lines - 1) * width * bpp;
	int num_samples;
	double samples_sum_score;
	if ((width <= 1) || (num_lines <= 1) || (bpp <= 2 )) {
		num_samples = 1;
		samples_sum_score = 1.0;
		goto end;
	}

	num_samples = 0;
	samples_sum_score = 0;

	while (cur_pix < last_line) {
		if ((cur_pix + bpp - lines) % (width * bpp) == 0) { // last pixel in the row
			cur_pix -= bpp; // jump is bigger than 1 so we will not enter endless loop
		}
		bottom_pix = cur_pix + width * bpp;
		samples_sum_score += pixels_square_score(cur_pix, bottom_pix, bpp);
		num_samples++;
		cur_pix += jump * bpp;
	}
	num_samples *= 3;

end:

	if (num_samples == 0) {
		return GRADUAL_INVALID;
	}
	double score = samples_sum_score / num_samples;
	if (score < GRADUAL_HIGH_RGB24_TH) {
		return GRADUAL_HIGH;
	}
	if (score < GRADUAL_MEDIUM_SCORE_TH) {
		return GRADUAL_MEDIUM;
	}
	else {
		return GRADUAL_LOW;
	}
}

VideoStream::VideoStream()
{
	m_candidate.clear();
	m_stream.clear();
	
}

VideoStream::~VideoStream()
{	
	Candidates::iterator candidate_iter;
	for (candidate_iter = m_candidate.begin(); candidate_iter != m_candidate.end(); )
	{
		Candidate* candidate = (Candidate*)* candidate_iter;
		candidate_iter = m_candidate.erase(candidate_iter);
		free(candidate);
	}
	Streams::iterator stream_iter;
	for (stream_iter = m_stream.begin(); stream_iter != m_stream.end(); )
	{	
		Stream* stream = (Stream*)* stream_iter;
		stream_iter = m_stream.erase(stream_iter);
		free(stream);
	}
	
}

void VideoStream::Add_Stream(RECT* rect, INT32 time, Drawable* drawable)
{
	bool Is_Candidate = false;
	bool Is_Gradual = false;
	if ((rect->right - rect->left) * (rect->bottom - rect->top) > STREAM_MIN_SIZE)
	{
		Is_Candidate = true;
		if (drawable == NULL) {
			Is_Gradual = true;
		}
		else {
			Is_Gradual = get_graduality_level(drawable) > GRADUAL_LOW ? true : false;
		}
		/*if (Is_Gradual) {
			printf("is gradual\n");
		}
		else {
			printf("is not gradual\n");
		}*/
	}
	Streams::iterator stream_iter;
	for (stream_iter = m_stream.begin();
		stream_iter != m_stream.end(); )
	{
		Stream* stream = (Stream*)* stream_iter;
		if (time - stream->time > MAX_STREAM_DETECT_TIME)
		{
			Set_StreamId(stream->id, false);
			stream_iter = m_stream.erase(stream_iter);
			free(stream);
		}
		else if (Is_Candidate && Rect_Is_Contain(rect, &stream->rect))
		{
			stream->time = time;
			stream_iter++;
		}
		else
		{
			stream_iter++;
		}
	}
	if (Is_Candidate && m_candidate.empty())
	{

		Candidate* candidate = (Candidate*)malloc(sizeof(Candidate));
		Copy_Rect(&candidate->rect, rect);
		candidate->time = time;
		candidate->count = 1;
		if (Is_Gradual) {
			candidate->gradual_count = 1;
			candidate->last_count = candidate->count;
		}
		else {
			candidate->gradual_count = 0;
		}
		m_candidate.push_back(candidate);
		//m_candidate.remove(candidate);
	}
	else
	{
		Candidates::iterator candidate_iter;
		bool Is_Stream = false;
		for (candidate_iter = m_candidate.begin();
			candidate_iter != m_candidate.end(); )
		{
			Candidate* candidate = reinterpret_cast<Candidate*>(*candidate_iter);
			if (time - candidate->time > MAX_DETECT_TIME)
			{
				//printf("m_candidate size %d \n", (int)m_candidate.size());
				candidate_iter = m_candidate.erase(candidate_iter);
				free(candidate);
			}
			else if (Is_Candidate && Rect_Is_Equal(rect, &candidate->rect))
			{
				Is_Stream = true;
				candidate->count++;
				candidate->time = time;
				if (Is_Gradual) {
					if (candidate->count - candidate->last_count > STREAM_FRAMES_RESET_CONDITION) {
						candidate->count = 1;
						candidate->gradual_count = 1;
					}
					else {
						candidate->gradual_count++;
					}
					candidate->last_count = candidate->count;
				}
				
				if (candidate->count >= STREAM_START_CONDITION && 
					candidate->gradual_count >= candidate->count * GRADUAL_FRAME_CONDITION)
				{
					if (Get_StreamId() >= 0) {
						Stream* stream = (Stream*)malloc(sizeof(Stream));
						Copy_Rect(&stream->rect, rect);
						stream->time = time;
						stream->id = Get_StreamId();
						Set_StreamId(stream->id, true);
						m_stream.push_back(stream);
					}
					candidate_iter = m_candidate.erase(candidate_iter);
					free(candidate);
				}
				else {
					candidate_iter++;
				}
			}
			else {
				candidate_iter++;
			}
			//printf("m_candidate size %d \n", (int)m_candidate.size());
		}
		if (Is_Candidate && !Is_Stream)
		{
			Candidate* candidate = (Candidate*)malloc(sizeof(Candidate));
			Copy_Rect(&candidate->rect, rect);
			candidate->time = time;
			candidate->count = 1;
			if (Is_Gradual) {
				candidate->gradual_count = 1;
				candidate->last_count = candidate->count;
			}
			else {
				candidate->gradual_count = 0;
			}
			m_candidate.push_back(candidate);
		}
	}

}

bool VideoStream::Is_StreamStart(void)
{
	
	//printf("m_stream size %d \n", (int)m_stream.size());
	if (m_stream.empty()) {
		return false;
	}
	return true;
}

void VideoStream::Stream_Timeout(INT32 time)
{
	Streams::iterator stream_iter;
	for (stream_iter = m_stream.begin();
		stream_iter != m_stream.end(); )
	{
		Stream* stream = (Stream*)* stream_iter;
		if (time - stream->time > MAX_STREAM_DETECT_TIME)
		{
			Set_StreamId(stream->id, false);
			stream_iter = m_stream.erase(stream_iter);
			free(stream);
		}
		else
		{
			stream_iter++;
		}
	}
}

void VideoStream::Stream_Reset(void)
{
	Candidates::iterator candidate_iter;
	for (candidate_iter = m_candidate.begin(); candidate_iter != m_candidate.end(); )
	{
		Candidate* candidate = (Candidate*)* candidate_iter;
		candidate_iter = m_candidate.erase(candidate_iter);
		free(candidate);
	}
	Streams::iterator stream_iter;
	for (stream_iter = m_stream.begin(); stream_iter != m_stream.end(); )
	{
		Stream* stream = (Stream*)* stream_iter;
		Set_StreamId(stream->id, false);
		stream_iter = m_stream.erase(stream_iter);
		free(stream);
	}
}

INT32 VideoStream::Get_StreamId()
{
	for (int i = 0; i < MAX_STREAMS; i++) {
		if (!m_stream_id[i]) {
			return i;
		}
	}
	return -1;
}

void VideoStream::Set_StreamId(INT32 id, bool allowed)
{
	m_stream_id[id] = allowed;
}
