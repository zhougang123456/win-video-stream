
#include "video-stream.hpp";

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

bool VideoStream::Is_StreamStart(RECT* rect, INT32 time)
{
	bool Is_Candidate = false;
	if ((rect->right - rect->left) * (rect->bottom - rect->top) > STREAM_MIN_SIZE)
	{
		Is_Candidate = true;
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
	if (m_candidate.empty())
	{

		Candidate* candidate = (Candidate*)malloc(sizeof(Candidate));
		Copy_Rect(&candidate->rect, rect);
		candidate->time = time;
		candidate->count = 1;
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
				if (candidate->count >= STREAM_START_CONDITION)
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
		if (!Is_Stream)
		{
			Candidate* candidate = (Candidate*)malloc(sizeof(Candidate));
			Copy_Rect(&candidate->rect, rect);
			candidate->time = time;
			candidate->count = 1;
			m_candidate.push_back(candidate);
		}
	}

	//printf("m_stream size %d \n", (int)m_stream.size());
	if (m_stream.empty()) {
		return false;
	}

	return true;
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
