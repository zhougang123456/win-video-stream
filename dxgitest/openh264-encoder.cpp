#include "openh264-encoder.hpp"
#include "string.h"
#include <malloc.h>
#include "macros.hpp"
#include <stdio.h>

#define DUMP_H264
#ifdef  DUMP_H264
static void dump_h264(unsigned char* buf, int data_size)
{
	char file_str[200];
	sprintf(file_str, "d:\\tmp\\output.h264");
	static FILE* f = NULL;
	if (f == NULL) {
		f = fopen(file_str, "wb");
	}
	if (!f) {
		return;
	}
	fwrite(buf, 1, data_size, f);
	//fclose(f);
}
#endif //  

Openh264Encoder::Openh264Encoder()
{
	
}

Openh264Encoder::~Openh264Encoder()
{
	if (encoder) {
		encoder->Uninitialize();
		m_destroy_encoder(encoder);
	}
}

bool Openh264Encoder::init(int width, int height)
{	
	HMODULE openh264handle = LoadLibraryW(L"openh264-2.1.0-win64.dll");
	if (openh264handle) {
		m_create_encoder = (create_encoder)::GetProcAddress(openh264handle, "WelsCreateSVCEncoder");
		m_destroy_encoder = (destroy_encoder)::GetProcAddress(openh264handle, "WelsDestroySVCEncoder");
	} else {
		//printf("load openh264 failed!\n");
		return false;
	}
	if (!m_create_encoder || !m_destroy_encoder) {
		return false;
	}
	int ret;
	int rv = m_create_encoder(&encoder);
	if (rv != 0) {
		return false;
	}
	if (encoder == NULL) {
		return false;
	}
	
	memset(&param, 0, sizeof(SEncParamBase));
	param.iUsageType = CAMERA_VIDEO_REAL_TIME;
	param.fMaxFrameRate = 30;
    param.iPicWidth = width;
	param.iPicHeight = height;
	param.iTargetBitrate = 5000000;
	ret = encoder->Initialize(&param);
	return !!ret;
}

void Openh264Encoder::encode(unsigned char* buf)
{
	encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &param);
	int videoFormat = videoFormatI420;
	encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
	int frameSize = param.iPicWidth * param.iPicHeight * 3 / 2;
	SFrameBSInfo info;
	memset(&info, 0, sizeof(SFrameBSInfo));
	SSourcePicture pic;
	memset(&pic, 0, sizeof(SSourcePicture));
	pic.iPicWidth = param.iPicWidth;
	pic.iPicHeight = param.iPicHeight;
	pic.iColorFormat = videoFormatI420;
	pic.iStride[0] = pic.iPicWidth;
	pic.iStride[1] = pic.iStride[2] = pic.iPicWidth >> 1;
	pic.pData[0] = buf;
	pic.pData[1] = pic.pData[0] + param.iPicWidth * param.iPicHeight;
	pic.pData[2] = pic.pData[1] + (param.iPicWidth * param.iPicHeight >> 2);
	int rv = encoder->EncodeFrame(&pic, &info);
	if (rv != cmResultSuccess) {
		return;
	}
	if (info.eFrameType != videoFrameTypeSkip) {
		int type = info.eFrameType;
		unsigned char* outbuffer = NULL;
		int bufferSize = 0;
		for (int i = 0; i < info.iLayerNum; ++i) {
			const SLayerBSInfo& layerInfo = info.sLayerInfo[i];
			int layerSize = 0;
			for (int j = 0; j < layerInfo.iNalCount; ++j) {
				layerSize += layerInfo.pNalLengthInByte[j];
			}
			if (outbuffer == NULL) {
				outbuffer = (unsigned char*)malloc(sizeof(unsigned char) * (bufferSize + layerSize));
			} else {
				outbuffer = (unsigned char*)realloc(outbuffer, sizeof(unsigned char) * (bufferSize + layerSize));
			}
			memcpy((unsigned char*)(outbuffer + bufferSize), (unsigned char*)layerInfo.pBsBuf, layerSize);
			bufferSize += layerSize;
		}
		printf("buffer size %d\n", bufferSize);
#ifdef DUMP_H264
		dump_h264(outbuffer, bufferSize);
#endif 
		free(outbuffer);
	}
}

void Openh264Encoder::rgba_convert_i420(unsigned char* src, unsigned char* dest, int width, int height)
{
	unsigned char* dst_y_even;
	unsigned char* dst_y_odd;
	unsigned char* dst_u;
	unsigned char* dst_v;
	const unsigned char* src_even;
	const unsigned char* src_odd;
	int i, j;

	src_even = (const unsigned char*)src;
	src_odd = src_even + width * 4;

	// it's planar

	dst_y_even = (unsigned char*)dest;
	dst_y_odd = dst_y_even + width;
	dst_u = dst_y_even + width * height;
	dst_v = dst_u + ((width * height) >> 2);

	// NB this doesn't work perfectly for u and v values of the edges of the video if your video size is not divisible by 2. FWIW.
	for (i = 0; i < height / 2; ++i)
	{
		for (j = 0; j < width / 2; ++j)
		{
			short r, g, b;
			b = *src_even++;
			g = *src_even++;
			r = *src_even++;
			++src_even;
			*dst_y_even++ = ((r * 66 + g * 129 + b * 25 + 128) >> 8) + 16;
			short sum_r = r, sum_g = g, sum_b = b;

			b = *src_even++;
			g = *src_even++;
			r = *src_even++;
			++src_even;
			*dst_y_even++ = ((r * 66 + g * 129 + b * 25 + 128) >> 8) + 16;
			sum_r += r;
			sum_g += g;
			sum_b += b;

			b = *src_odd++;
			g = *src_odd++;
			r = *src_odd++;
			++src_odd;
			*dst_y_odd++ = ((r * 66 + g * 129 + b * 25 + 128) >> 8) + 16;
			sum_r += r;
			sum_g += g;
			sum_b += b;

			b = *src_odd++;
			g = *src_odd++;
			r = *src_odd++;
			++src_odd;
			*dst_y_odd++ = ((r * 66 + g * 129 + b * 25 + 128) >> 8) + 16;
			sum_r += r;
			sum_g += g;
			sum_b += b;

			// compute ave's of this 2x2 bloc for its u and v values
			// could use Catmull-Rom interpolation possibly? http://msdn.microsoft.com/en-us/library/Aa904813#yuvformats_420formats_16bitsperpixel
			// rounding by one? don't care enough... 39 ms -> 36.8
			//sum_r += 2;
			//sum_g += 2;
			//sum_b += 2;

			// divide by 4 to average
			sum_r /= 4;
			sum_g /= 4;
			sum_b /= 4;

			*dst_u++ = ((sum_r * -38 - sum_g * 74 + sum_b * 112 + 128) >> 8) + 128; // only one
			*dst_v++ = ((sum_r * 112 - sum_g * 94 - sum_b * 18 + 128) >> 8) + 128; // only one
		}

		dst_y_even += width;
		dst_y_odd += width;
		src_even += width * 4;
		src_odd += width * 4;
	}
}
