#include "jpeg-encoder.hpp"

JpegEncoder::JpegEncoder()
{
}

JpegEncoder::~JpegEncoder()
{
	jpeg_destroy_compress(&cinfo);
}

/* jpeg destination manager callbacks */

static void dest_mgr_init_destination(j_compress_ptr cinfo)
{
	JpegEncoder* enc = (JpegEncoder*)cinfo->client_data;
	if (enc->dest_mgr.free_in_buffer == 0) {
		enc->dest_mgr.free_in_buffer = enc->usr->more_space(enc->usr,
			&enc->dest_mgr.next_output_byte);

		if (enc->dest_mgr.free_in_buffer == 0) {
			//printf("not enough space\n");
			return;
		}
	}

	enc->cur_image.out_size = enc->dest_mgr.free_in_buffer;
}

static Boolean dest_mgr_empty_output_buffer(j_compress_ptr cinfo)
{
	JpegEncoder* enc = (JpegEncoder*)cinfo->client_data;
	enc->dest_mgr.free_in_buffer = enc->usr->more_space(enc->usr,
		&enc->dest_mgr.next_output_byte);

	if (enc->dest_mgr.free_in_buffer == 0) {
		//printf("not enough space");
		return FALSE;
	}
	enc->cur_image.out_size += enc->dest_mgr.free_in_buffer;
	return TRUE;
}

static void dest_mgr_term_destination(j_compress_ptr cinfo)
{
	JpegEncoder* enc = (JpegEncoder*)cinfo->client_data;
	enc->cur_image.out_size -= enc->dest_mgr.free_in_buffer;
}

bool JpegEncoder::init(JpegEncoderUsrContext* usr_context)
{	
	if (!usr_context->more_space || !usr_context->more_lines) {
		return false;
	}
	usr = usr_context;
	dest_mgr.init_destination = dest_mgr_init_destination;
	dest_mgr.empty_output_buffer = dest_mgr_empty_output_buffer;
	dest_mgr.term_destination = dest_mgr_term_destination;
	cinfo.err = jpeg_std_error(&jerr);
	int size = sizeof(cinfo);
	jpeg_create_compress(&cinfo);
	cinfo.client_data = this;
	cinfo.dest = &dest_mgr;

	return true;
}

static void convert_RGB16_to_RGB24(void* line, int width, UINT8** out_line)
{
	UINT16* src_line = (UINT16 *)line;
	UINT8* out_pix;
	int x;

	if (!(out_line && *out_line)) {
		return;
	}

	out_pix = *out_line;

	for (x = 0; x < width; x++) {
		UINT16 pixel = *src_line++;
		*out_pix++ = ((pixel >> 7) & 0xf8) | ((pixel >> 12) & 0x7);
		*out_pix++ = ((pixel >> 2) & 0xf8) | ((pixel >> 7) & 0x7);
		*out_pix++ = ((pixel << 3) & 0xf8) | ((pixel >> 2) & 0x7);
	}
}

static void convert_BGR24_to_RGB24(void* in_line, int width, UINT8** out_line)
{
	int x;
	UINT8* out_pix;
	UINT8* line = (UINT8*)in_line;
	if (!(out_line && *out_line)) {
		return;
	}

	out_pix = *out_line;

	for (x = 0; x < width; x++) {
		*out_pix++ = line[2];
		*out_pix++ = line[1];
		*out_pix++ = line[0];
		line += 3;
	}
}

static void convert_BGRX32_to_RGB24(void* line, int width, UINT8** out_line)
{
	UINT32* src_line = (UINT32 *)line;
	UINT8* out_pix;
	int x;

	if (!(out_line && *out_line)) {
		return;
	}

	out_pix = *out_line;

	for (x = 0; x < width; x++) {
		UINT32 pixel = *src_line++;
		*out_pix++ = (pixel >> 16) & 0xff;
		*out_pix++ = (pixel >> 8) & 0xff;
		*out_pix++ = pixel & 0xff;
	}
}

#define FILL_LINES() {                                                  \
                                   \
}

void JpegEncoder::do_jpeg_encode(UINT8* lines, unsigned int num_lines)
{
	UINT8* lines_end;
	UINT8* RGB24_line;
	int stride, width;
	JSAMPROW row_pointer[1];
	width = cur_image.width;
	stride = cur_image.stride;

	RGB24_line = (UINT8*)malloc(sizeof(UINT8) * width * 3);

	lines_end = lines + (stride * num_lines);

	for (; cinfo.next_scanline < cinfo.image_height; lines += stride) {
		if (lines == lines_end) {
			int n = usr->more_lines(usr, &lines);               
			if (n <= 0) {	
				//printf("more lines failed\n");                              
				return;                                                     
			}                                                               
			lines_end = lines + n * stride;                                 
		}
		cur_image.convert_line_to_RGB24(lines, width, &RGB24_line);
		row_pointer[0] = RGB24_line;
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	free(RGB24_line);
}

int JpegEncoder::encode(int quality, JpegEncoderImageType type,
	int width, int height, UINT8* lines, unsigned int num_lines, int stride,
	UINT8* io_ptr, unsigned int num_io_bytes)
{
	cur_image.type = type;
	cur_image.width = width;
	cur_image.height = height;
	cur_image.stride = stride;
	cur_image.out_size = 0;

	switch (type) {
	case JPEG_IMAGE_TYPE_RGB16:
		cur_image.convert_line_to_RGB24 = convert_RGB16_to_RGB24;
		break;
	case JPEG_IMAGE_TYPE_BGR24:
		cur_image.convert_line_to_RGB24 = convert_BGR24_to_RGB24;
		break;
	case JPEG_IMAGE_TYPE_BGRX32:
		cur_image.convert_line_to_RGB24 = convert_BGRX32_to_RGB24;
		break;
	default:
		//printf("bad image type\n");
		;
	}

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);

	dest_mgr.next_output_byte = io_ptr;
	dest_mgr.free_in_buffer = num_io_bytes;

	jpeg_start_compress(&cinfo, TRUE);

	do_jpeg_encode(lines, num_lines);

	jpeg_finish_compress(&cinfo);
	return cur_image.out_size;
}
