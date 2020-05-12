#include "macros.hpp"
#include "jpeglib.h"

#define JPEG_DEFAULT_COMPRESSION_QUALITY 85

typedef enum {
	JPEG_IMAGE_TYPE_INVALID,
	JPEG_IMAGE_TYPE_RGB16,
	/* in byte per color types, the notation is according to the order of the
	   colors in the memory */
	JPEG_IMAGE_TYPE_BGR24,
	JPEG_IMAGE_TYPE_BGRX32,
} JpegEncoderImageType;

typedef struct JpegEncoderUsrContext JpegEncoderUsrContext;

struct JpegEncoderUsrContext {
	int (*more_space)(JpegEncoderUsrContext* usr, UINT8** io_ptr);
	int (*more_lines)(JpegEncoderUsrContext* usr, UINT8** lines);
};
class JpegEncoder
{
public:
	JpegEncoder();
	~JpegEncoder();
	JpegEncoderUsrContext* usr;
	struct jpeg_destination_mgr dest_mgr;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	struct {
		JpegEncoderImageType type;
		int width;
		int height;
		int stride;
		unsigned int out_size;
		void (*convert_line_to_RGB24) (void* line, int width, UINT8** out_line);
	} cur_image;
	bool init(JpegEncoderUsrContext* usr_context);
	int encode(int quality, JpegEncoderImageType type,
			int width, int height, UINT8* lines, unsigned int num_lines, int stride,
			UINT8* io_ptr, unsigned int num_io_bytes);
private:
	void do_jpeg_encode(UINT8* lines, unsigned int num_lines);
};
