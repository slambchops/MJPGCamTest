#ifndef ENZO_UTILS_H
#define ENZO_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vpu_common.h"

#include <android/log.h>
#include <jni.h>
#include <stddef.h>

/* For allocating buffers */
#include "vpu_io.h"

#define LOG_TAG_ENZO_CODEC "EnzoCodecLib"
#define info_msg(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG_ENZO_CODEC,__VA_ARGS__)
#define warn_msg(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG_ENZO_CODEC,__VA_ARGS__)
#define err_msg(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG_ENZO_CODEC,__VA_ARGS__)

/* The amount of NAL slices into which an encoded picture can be divided */
#define MAX_NAL_PER_PICTURE	300

typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;

/* Color space enumeration */
enum {
	NA_MODE	= 0,
	NV12	= 1,
	YUV420P = 2,
	YUV422P = 3,
	YUYV	= 4
};
/* Data source enumeration */
enum {
	NA_SRC		= 0,
	FILE_SRC	= 1,
	V4L2_CAM 	= 2,
	VPU_CODEC	= 3,
	BUFFER		= 4
};
/* Media data type enumeration */
enum {
	NA_TYPE		= 0,
	RAW_VIDEO	= 1,
	H264AVC 	= 2,
	MJPEG		= 3
};
/* Encoder picture type enumeration */
enum {
	I_FRAME		= 0,
	P_FRAME		= 1,
	B_FRAME		= 2
};

/* Decoder return status */
enum {
	DEC_NEW_FRAME		= 0,
	DEC_NO_NEW_FRAME	= -1,
	DEC_ERROR		= -2
};

/* H264 NAL types */
enum {
	CODED_SLICE_NON_IDR	= 1,
	CODED_SLICE_DAT_PART_A	= 2,
	CODED_SLICE_DAT_PART_B	= 3,
	CODED_SLICE_DAT_PART_C	= 4,
	CODED_SLICE_IDR		= 5,
	SEI			= 6,
	SEQ_PARAM_SET		= 7,
	PIC_PARAM_SET		= 8,
	ACCESS_UNIT_DELIM	= 9,
	END_OF_SEQUENCE		= 10,
	END_OF_STREAM		= 11,
	FILLER_DATA		= 12,
	SEQ_PARAM_SET_EXT	= 13
};

struct nalInfoStruct {
	int nalType;
	int nalNumber;
	int nalLength[MAX_NAL_PER_PICTURE];
};

/* The mediaBuffer struct is used to share data between different components
   involved in encode, decode, display, and video processing. */
struct mediaBuffer {

	int dataType;
	/* Specifies the format of the data in the buffer.
	   For example, is it raw data from a camera or 
	   encoded data from an encoder block. */

	int dataSource;
	/* The source of the data; was it produced by the
	   encoder block, or from a v4l2 camera, etc... */

	int colorSpace;
	/* The color space of the data payload. This mainly
	   applies to raw video, but in some cases it may be
	   useful to know the color space of encoded data. */

	int width;
	/* Width of the entire video buffer. This may or may not be
	   the actual width of the image contained in the buffer */

	int height;
	/* Height of the entire video buffer. This may or may not be
	   the actual height of the image contained in the buffer */

	int imageWidth;
	/* This is the width of the image contained in the media buffer.
	   Most of the time, this will equal the width of the entire buffer. */

	int imageHeight;
	/* This is the height of the image contained in the media buffer.
	   Most of the time, this will equal the height of the entire
	   buffer. */

	int frameType;
	/* This indicates whether the frame is I, P, or B frame.
	   0 = I frame
	   1 = P frame
	   2 = B frame */

	int fd;
	/* The file descriptor (if the data is coming from a file */

	int bufOutSize;
	/* The size of the data pointed to by bufOut. This
	   value is only valid AFTER a component has put
	   data into the mediaBuffer. */

	unsigned char *vBufOut;
	/* Virtual pointer to the data the media component has
	   finished processing. The media component is 
	   responsible for allocating its own memory,
	   and will only supply the address of that data
	   in the bufOut pointer. Data should NEVER be
	   written to this address; it is read only. */

	unsigned char *pBufOut;
	/* Physical address of data. Not all processes will produce
	   physically contiguous buffers, so this pointer may be NULL */

	struct nalInfoStruct nalInfo;

	vpu_mem_desc desc;
};

int freadn(int fd, void *vptr, size_t n);
int fwriten(int fd, void *vptr, size_t n);

int mediaBufferInit(struct mediaBuffer *medBuf, int size);
int mediaBufferDeinit(struct mediaBuffer *medBuf);

#ifdef __cplusplus
}
#endif

#endif
