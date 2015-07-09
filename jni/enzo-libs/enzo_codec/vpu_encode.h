#ifndef VPU_ENCODE_H
#define VPU_ENCODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "enzo_utils.h"
#include "vpu_common.h"

#include "vpu_io.h"
#include "vpu_lib.h"

struct encoder_info {
	EncHandle handle;		/* Encoder handle */
	PhysicalAddress phy_bsbuf_addr; /* Physical bitstream buffer */
	u32 virt_bsbuf_addr;		/* Virtual bitstream buffer */
	PhysicalAddress phy_outbuf_addr; /* Physical outbuf buffer */
	u32 virt_outbuf_addr;		/* Virtual outbuf buffer */
	int enc_picwidth;	/* Encoded Picture width */
	int enc_picheight;	/* Encoded Picture height */
	int src_picwidth;        /* Source Picture width */
	int src_picheight;       /* Source Picture height */
	int totalfb;	/* Total number of framebuffers allocated */
	int src_fbid;	/* Index of frame buffer that contains YUV image */
	FrameBuffer *fb; /* frame buffer base given to encoder */
	struct frame_buf **pfbpool; /* allocated fb pointers are stored here */
	ExtBufCfg scratchBuf;
	int ringBufferEnable;
	int mvc_paraset_refresh_en;
	int mvc_extension;
	int linear2TiledEnable;
	int minFrameBufferCount;
	int fill_headers;
	int enc_fps;
	int enc_bit_rate;
	int color_space;
	int gop_size;
	int force_i_frame;
	vpu_mem_desc bs_mem_desc;
	vpu_mem_desc outbuf_desc;
	void *g2d_handle;
};

int vpu_encoder_init(struct encoder_info *enc, struct mediaBuffer *enc_dst);
int vpu_encoder_deinit(struct encoder_info *enc);
int vpu_encoder_encode_frame(struct encoder_info *enc,
			 struct mediaBuffer *vid_src,
			 struct mediaBuffer *enc_dst);

#ifdef __cplusplus
}
#endif

#endif

