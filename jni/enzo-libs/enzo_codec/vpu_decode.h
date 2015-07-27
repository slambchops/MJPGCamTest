#ifndef VPU_DECODE_H
#define VPU_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "enzo_utils.h"
#include "vpu_common.h"

#include "vpu_io.h"
#include "vpu_lib.h"

struct decoder_info {
	DecHandle handle;
	PhysicalAddress phy_bsbuf_addr;
	PhysicalAddress phy_ps_buf;
	PhysicalAddress phy_slice_buf;
	PhysicalAddress phy_vp8_mbparam_buf;

	int format;
	int phy_slicebuf_size;
	int phy_vp8_mbparam_size;
	u32 virt_bsbuf_addr;
	int picwidth;
	int picheight;
	int stride;
	int mjpg_fmt;
	int regfbcount;
	int minfbcount;
	int rot_buf_count;
	int extrafb;
	FrameBuffer *fb;
	struct frame_buf **pfbpool;
	vpu_mem_desc *mvcol_memdesc;
	vpu_mem_desc bs_mem_desc;
	vpu_mem_desc ps_mem_desc;
	vpu_mem_desc slice_mem_desc;
	Rect picCropRect;
	int reorderEnable;
	int tiled2LinearEnable;
	int post_processing;
	int disp_clr_index;
	int color_space;
	int totalfb;

	int decoded_field[32];
	int lastPicWidth;
	int lastPicHeight;

	char decoder_name[12];

	DecReportInfo mbInfo;
	DecReportInfo mvInfo;
	DecReportInfo frameBufStat;
	DecReportInfo userData;
};

int vpu_decoder_init(struct decoder_info *dec, struct mediaBuffer *enc_src);
int vpu_decoder_deinit(struct decoder_info *dec);
int vpu_decoder_decode_frame(struct decoder_info *dec,
			 struct mediaBuffer *enc_src,
			 struct mediaBuffer *vid_dst);

#ifdef __cplusplus
}
#endif

#endif
