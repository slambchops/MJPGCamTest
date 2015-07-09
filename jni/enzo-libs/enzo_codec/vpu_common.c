#include "vpu_common.h"
#include "enzo_utils.h"

#include <stdio.h>
#include <string.h>

struct frame_buf *framebuf_alloc(int stdMode, int format, int strideY, int height, int mvCol)
{
	struct frame_buf *fb;
	int err;
	int y_size, c_size, c_stride;

	//fb = get_framebuf();
	//if (fb == NULL) {
	//	err_msg("Frame buffer get_framebuf failed\n");
	//	return NULL;
	//}
	mvCol++;
	stdMode++;

	fb = calloc(1, sizeof(struct frame_buf));
	if (fb == NULL) {
		err_msg("Failed to allocate framebuffer\n");
		return NULL;
	}

	y_size = strideY * height;

	if ((format == YUV422P) || (format == YUYV)) {
		c_size = y_size / 2;
		c_stride = strideY / 2;
	} else {
		c_size = y_size / 4;
		c_stride = strideY / 4;
	}

	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
	fb->desc.size = y_size + c_size*2;

	//info_msg("y_size %d c_size %d\n", y_size, c_size);
	//info_msg("fb->desc.size %d mvCol %d\n", fb->desc.size, mvCol);

	err = IOGetPhyMem(&fb->desc);
	if (err) {
		err_msg("Frame buffer allocation failure\n");
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		free(fb);
		return NULL;
	}

	fb->addrY = fb->desc.phy_addr;
	fb->addrCb = fb->addrY + y_size;
	fb->addrCr = fb->addrCb + c_size;
	fb->strideY = strideY;
	fb->strideC =  c_stride;

	fb->desc.virt_uaddr = IOGetVirtMem(&(fb->desc));
	if (fb->desc.virt_uaddr <= 0) {
		IOFreePhyMem(&fb->desc);
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		err_msg("Frame buffer IOGetVirtMem failed\n");
		free(fb);
		return NULL;
	}

	return fb;
}

void framebuf_init(void)
{
	//int i;

	//for (i = 0; i < NUM_FRAME_BUFS; i++) {
	//	fbarray[i] = &fbpool[i];
	//}
}

void framebuf_free(struct frame_buf *fb)
{
	if (fb == NULL)
		return;

	if (fb->desc.virt_uaddr) {
		IOFreeVirtMem(&fb->desc);
	}

	if (fb->desc.phy_addr) {
		IOFreePhyMem(&fb->desc);
	}

	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
	//put_framebuf(fb);
	free(fb);
}

/*struct frame_buf *get_framebuf(void)
{
	struct frame_buf *fb;

	fb = fbarray[fb_index];
	fbarray[fb_index] = 0;

	++fb_index;
	fb_index &= FB_INDEX_MASK;

	return fb;
}

void put_framebuf(struct frame_buf *fb)
{
	--fb_index;
	fb_index &= FB_INDEX_MASK;

	fbarray[fb_index] = fb;
}*/
