#include "wels/codec_api.h"

typedef int(*create_encoder)(ISVCEncoder** ppEncoder);
typedef void (*destroy_encoder)(ISVCEncoder* pEncoder);

class Openh264Encoder
{
public:
	Openh264Encoder();
	~Openh264Encoder();
	bool init(int width, int height);
	void encode(unsigned char* buf);
	void rgba_convert_i420(unsigned char* src, unsigned char* dest, int width, int height);
private:
	ISVCEncoder* encoder;
	SEncParamBase param;
	create_encoder m_create_encoder;
	destroy_encoder m_destroy_encoder;
};