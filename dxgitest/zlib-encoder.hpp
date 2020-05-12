#include "macros.hpp"
#include <zlib.h>

#define ZLIB_DEFAULT_COMPRESSION_LEVEL 9
#define MIN_GLZ_SIZE_FOR_ZLIB 100

typedef struct ZlibEncoderUsrContext ZlibEncoderUsrContext;
struct ZlibEncoderUsrContext {
	int (*more_space)(ZlibEncoderUsrContext* usr, UINT8** io_ptr);
	int (*more_input)(ZlibEncoderUsrContext* usr, UINT8** input);
};
class ZlibEncoder
{
public:
	ZlibEncoder();
	~ZlibEncoder();
	bool init(ZlibEncoderUsrContext* usr_context, int level);
	int encode(int level, int input_size, UINT8* io_ptr, unsigned int num_io_bytes);
private:
	ZlibEncoderUsrContext* usr;
	z_stream strm;
	int last_level;
};
