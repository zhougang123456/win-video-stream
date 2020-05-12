#include <list>
#include "D3D11.h"
#define STREAM_START_CONDITION 20
#define GRADUAL_FRAME_CONDITION 0.2
#define STREAM_FRAMES_RESET_CONDITION 100
#define MAX_DETECT_TIME 200
#define MAX_STREAM_DETECT_TIME 1000
#define MAX_STREAMS 50
#define STREAM_MIN_SIZE (200 * 200)

typedef struct Candidate
{
	RECT rect;
	INT32 time;
	INT32 count;
	INT32 gradual_count;
	INT32 last_count;
}Candidate;

typedef struct Stream
{
	RECT rect;
	INT32 id;
	INT32 time;
}Stream;

typedef struct Drawable
{
	BYTE* data;
	INT32 width;
	INT32 height;
	INT32 bpp;
};

typedef std::list<Candidate*> Candidates;
typedef std::list<Stream*> Streams;

class VideoStream
{
public:
	VideoStream();
	~VideoStream();
	void Add_Stream(RECT* rect, INT32 time, Drawable* drawable);
	bool Is_StreamStart(void);
	void Stream_Timeout(INT32 time);
	void Stream_Reset(void);
private:
	Candidates m_candidate;
	Streams m_stream;
	bool m_stream_id[MAX_STREAMS];
	INT32 Get_StreamId();
	void Set_StreamId(INT32 id, bool allowed);
};


