#ifndef VPU_COMMON_H
#define VPU_COMMON_H

#include "vpu_io.h"

#define STREAM_BUF_SIZE		0x200000
#define STREAM_FILL_SIZE	0x40000
#define STREAM_READ_SIZE	(512 * 8)
#define STREAM_END_SIZE		0
#define PS_SAVE_SIZE		0x080000

#define NUM_FRAME_BUFS		32
#define FB_INDEX_MASK		(NUM_FRAME_BUFS - 1)

struct frame_buf {
	int addrY;
	int addrCb;
	int addrCr;
	int strideY;
	int strideC;
	int mvColBuf;
	vpu_mem_desc desc;
};

static int fb_index;
static struct frame_buf *fbarray[NUM_FRAME_BUFS];
static struct frame_buf fbpool[NUM_FRAME_BUFS];

struct frame_buf *framebuf_alloc(int stdMode, int format,
				 int strideY, int height, int mvCol);
void framebuf_free(struct frame_buf *fb);
void framebuf_init(void);
struct frame_buf *get_framebuf(void);
void put_framebuf(struct frame_buf *fb);

#endif
