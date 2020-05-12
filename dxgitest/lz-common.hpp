typedef enum {
	LZ_IMAGE_TYPE_INVALID,
	LZ_IMAGE_TYPE_PLT1_LE,
	LZ_IMAGE_TYPE_PLT1_BE,      // PLT stands for palette
	LZ_IMAGE_TYPE_PLT4_LE,
	LZ_IMAGE_TYPE_PLT4_BE,
	LZ_IMAGE_TYPE_PLT8,
	LZ_IMAGE_TYPE_RGB16,
	LZ_IMAGE_TYPE_RGB24,
	LZ_IMAGE_TYPE_RGB32,
	LZ_IMAGE_TYPE_RGBA,
	LZ_IMAGE_TYPE_XXXA,
	LZ_IMAGE_TYPE_A8
} LzImageType;

/* change the max window size will require change in the encoding format*/
#define LZ_MAX_WINDOW_SIZE (1 << 25)
#define MAX_COPY 32

/* ASCII "LZ  " */
#define LZ_MAGIC 0x20205a4c
#define LZ_VERSION_MAJOR 1U
#define LZ_VERSION_MINOR 1U
#define LZ_VERSION ((LZ_VERSION_MAJOR << 16) | (LZ_VERSION_MINOR & 0xffff))

#define LZ_IMAGE_TYPE_MASK 0x0f
#define LZ_IMAGE_TYPE_LOG 4 // number of bits required for coding the image type

/* access to the arrays is based on the image types */
static const int IS_IMAGE_TYPE_PLT[] = { 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 };
static const int IS_IMAGE_TYPE_RGB[] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
static const int PLT_PIXELS_PER_BYTE[] = { 0, 8, 8, 2, 2, 1 };
static const int RGB_BYTES_PER_PIXEL[] = { 0, 1, 1, 1, 1, 1, 2, 3, 4, 4, 4, 1 };
