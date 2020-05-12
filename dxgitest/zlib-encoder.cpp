#include "zlib-encoder.hpp"

ZlibEncoder::ZlibEncoder()
{
}

ZlibEncoder::~ZlibEncoder()
{
	deflateEnd(&strm);
}

bool ZlibEncoder::init(ZlibEncoderUsrContext* usr_context, int level)
{	
	int z_ret;

	if (!usr_context->more_space || !usr_context->more_input) {
		return NULL;
	}

	usr = usr_context;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	z_ret = deflateInit(&strm, level);
	last_level = level;
	if (z_ret != Z_OK) {
		//printf("zlib error\n");
		return false;
	}

	return true;
}

int ZlibEncoder::encode(int level, int input_size, UINT8* io_ptr, unsigned int num_io_bytes)
{
	int flush;
	int enc_size = 0;
	int out_size = 0;
	int z_ret;

	z_ret = deflateReset(&strm);

	if (z_ret != Z_OK) {
		//printf("deflateReset failed\n");
		return -1;
	}

	strm.next_out = io_ptr;
	strm.avail_out = num_io_bytes;

	if (level != last_level) {
		if (strm.avail_out == 0) {
			strm.avail_out = usr->more_space(usr, &strm.next_out);
			if (strm.avail_out == 0) {
				//printf("not enough space\n");
				return -1;
			}
		}
		z_ret = deflateParams(&strm, level, Z_DEFAULT_STRATEGY);
		if (z_ret != Z_OK) {
			//printf("deflateParams failed");
			return -1;
		}
		last_level = level;
	}


	do {
		strm.avail_in = usr->more_input(usr, &strm.next_in);
		if (strm.avail_in <= 0) {
			//printf("more input failed");
		}
		enc_size += strm.avail_in;
		flush = (enc_size == input_size) ? Z_FINISH : Z_NO_FLUSH;
		while (1) {
			int deflate_size = strm.avail_out;
			z_ret = deflate(&strm, flush);
			if (!(z_ret != Z_STREAM_ERROR)) {
				return -1;
			}
			out_size += deflate_size - strm.avail_out;
			if (strm.avail_out) {
				break;
			}

			strm.avail_out = usr->more_space(usr, &strm.next_out);
			if (strm.avail_out == 0) {
				//prinf("not enough space\n");
				return -1;
			}
		}
	} while (flush != Z_FINISH);

	if (!(z_ret == Z_STREAM_END)) {
		return -1;
	}
	return out_size;
}
