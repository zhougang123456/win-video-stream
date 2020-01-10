#pragma once
#include <list>
#include "D3D11.h"
#define STREAM_START_CONDITION 10
#define MAX_DETECT_TIME 200
#define MAX_STREAM_DETECT_TIME 1000
#define MAX_STREAMS 50
#define STREAM_MIN_SIZE (200 * 200)
typedef struct Candidate
{
	RECT rect;
	INT32 time;
	INT32 count;
}Candidate;
typedef struct Stream
{
	RECT rect;
	INT32 id;
	INT32 time;
}Stream;
typedef std::list<Candidate*> Candidates;
typedef std::list<Stream*> Streams;

class VideoStream
{
public:
	VideoStream();
	~VideoStream();
	void Add_Stream(RECT* rect, INT32 time);
	bool Is_StreamStart(void);
	void Stream_Timeout(INT32 time);

private:
	Candidates m_candidate;
	Streams m_stream;
	bool m_stream_id[MAX_STREAMS];
	INT32 Get_StreamId();
	void Set_StreamId(INT32 id, bool allowed);
};


