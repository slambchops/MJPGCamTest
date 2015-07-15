#include "vpu_encode.h"

#include "g2d.h"

#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

/* Function prototypes */
static int encoder_allocate_framebuffer(struct encoder_info *enc);
static int encoder_get_headers(struct encoder_info *enc, struct mediaBuffer *enc_dst);
static void encoder_close(struct encoder_info *enc);
static void encoder_free_framebuffer(struct encoder_info *enc);
static int encoder_open(struct encoder_info *enc);
static int read_source_frame(struct encoder_info *enc, struct mediaBuffer *vid_src);
/* End function prototypes */

int vpu_encoder_init(struct encoder_info *enc, struct mediaBuffer *enc_dst)
{
	int ret;

	/* get physical contigous bit stream buffer */
	info_msg("Encoder: Allocating physical contigous bit stream buffer\n");
	enc->bs_mem_desc.size = STREAM_BUF_SIZE;
	ret = IOGetPhyMem(&enc->bs_mem_desc);
	if (ret) {
		err_msg("Encoder: Unable to obtain physical memory\n");
		return -1;
	}
	enc->virt_bsbuf_addr = IOGetVirtMem(&enc->bs_mem_desc);
	if (enc->virt_bsbuf_addr <= 0) {
		err_msg("Encoder: Unable to map physical memory\n");
		IOFreePhyMem(&enc->bs_mem_desc);
		return -1;
	}

	info_msg("Encoder: Allocating physical contigous output buffer\n");
	enc->outbuf_desc.size = enc->enc_picwidth*enc->enc_picheight;
	ret = IOGetPhyMem(&enc->outbuf_desc);
	if (ret) {
		err_msg("Encoder: Unable to obtain physical memory\n");
		return -1;
	}
	enc->virt_outbuf_addr = IOGetVirtMem(&enc->outbuf_desc);
	if (enc->virt_outbuf_addr <= 0) {
		err_msg("Encoder: Unable to map physical memory\n");
		IOFreePhyMem(&enc->outbuf_desc);
		return -1;
	}

	enc->phy_bsbuf_addr = enc->bs_mem_desc.phy_addr;
	enc->linear2TiledEnable = 0;

	/* open the encoder */
	info_msg("Encoder: Opening the encoder\n");
	ret = encoder_open(enc);
	if (ret)
		return -1;

	/* Get the headers for the encoded data and copy them to media buffer*/
	info_msg("Encoder: Filling the headers\n");
	ret = encoder_get_headers(enc, enc_dst);
	if (ret)
		return -1;

	/* allocate memory for the frame buffers */
	info_msg("Encoder: Allocating encoder framebuffers\n");
	ret = encoder_allocate_framebuffer(enc);
	if (ret) {
		return -1;
	}

	return 0;
}

int vpu_encoder_deinit(struct encoder_info *enc)
{
	/* free the allocated framebuffers */
	info_msg("Encoder: Freeing encoder framebuffers\n");
	encoder_free_framebuffer(enc);

	/* close the encoder */
	info_msg("Encoder: Closing the encoder\n");
	encoder_close(enc);

	IOFreePhyMem(&enc->bs_mem_desc);
	IOFreePhyMem(&enc->outbuf_desc);

	return 0;
}

int vpu_encoder_encode_frame(struct encoder_info *enc, struct mediaBuffer *vid_src, struct mediaBuffer *enc_dst)
{
	EncHandle handle = enc->handle;
	EncParam enc_param;
	EncOutputInfo outinfo;
	RetCode ret = 0;
	int src_fbid = enc->src_fbid;
	int loop_id;
	unsigned char *vbuf;

	/* Timer related variables */
	struct timeval total_start, total_end;
	int sec, usec;
	double total_time;

	enc_param.encLeftOffset = 0;
	enc_param.encTopOffset = 0;
	if ((enc_param.encLeftOffset + enc->enc_picwidth) > enc->src_picwidth){
		err_msg("Encoder: Configure is failure for width and left offset\n");
		return -1;
	}
	if ((enc_param.encTopOffset + enc->enc_picheight) > enc->src_picheight){
		err_msg("Encoder: Configure is failure for height and top offset\n");
		return -1;
	}

	/* Choose the location of the source frame that will be encoded.
	   If the source came from a VPU codec, then there will a
	   frameBuffer type that can be used, which will have a
	   physically contiguous address that can be supplied directly
	   to the VPU encoder. Otherwise, the source data will need to
	   be mem copied into the preallocated encoder source frame
	   buffer. */
	gettimeofday(&total_start, NULL);
	ret = read_source_frame(enc, vid_src);
	if (ret <= 0) {
		err_msg("Encoder: no data read from video source\n");
		return -1;
	}
	gettimeofday(&total_end, NULL);
	sec = total_end.tv_sec - total_start.tv_sec;
	usec = total_end.tv_usec - total_start.tv_usec;
	if (usec < 0) {
		sec--;
		usec = usec + 1000000;
	}
	total_time = (sec * 1000000) + usec;
	//info_msg("encode csc took %f us\n", total_time);

	enc_param.sourceFrame = &enc->fb[src_fbid];
	enc_param.quantParam = 23;
	enc_param.forceIPicture = enc->force_i_frame;
	enc_param.skipPicture = 0;
	enc_param.enableAutoSkip = 1;

	ret = vpu_EncStartOneFrame(handle, &enc_param);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Encoder: vpu_EncStartOneFrame failed Err code:%d\n", ret);
		return -1;
	}

	loop_id = 0;
	while (vpu_IsBusy()) {
		vpu_WaitForInt(200);
		if (loop_id == 20) {
			err_msg("Encoder: VPU sw reset failed\n");
			ret = vpu_SWReset(handle, 0);
			return -1;
		}
		loop_id ++;
	}

	ret = vpu_EncGetOutputInfo(handle, &outinfo);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Encoder: vpu_EncGetOutputInfo failed Err code: %d\n", ret);
		return -1;
	}

	if (outinfo.skipEncoded)
		warn_msg("Encoder: Skip encoding one Frame!\n");

	/* Now that we have an output frame from encoder, save it to the
	   the mediaBuffer structure so it can easily be accessed */
	vbuf = (unsigned char *)enc->virt_bsbuf_addr + outinfo.bitstreamBuffer
		- enc->phy_bsbuf_addr;

	enc_dst->frameType = outinfo.picType;
	enc_dst->bufOutSize = outinfo.bitstreamSize;
	enc_dst->vBufOut = (unsigned char*)vbuf;
	enc_dst->pBufOut = (unsigned char*)outinfo.bitstreamBuffer;
	return 0;
	
	/* For now, we will always include headers with frame 
	memcpy((unsigned char*)enc->virt_outbuf_addr + temp_size, vbuf,
		outinfo.bitstreamSize);
	temp_size += outinfo.bitstreamSize;
	enc_dst->dataSource = VPU_CODEC;
	enc_dst->bufOutSize = temp_size;
	enc_dst->vBufOut = (unsigned char*)enc->virt_outbuf_addr;
	enc_dst->pBufOut = (unsigned char*)enc->phy_outbuf_addr;

	return 0;*/
}

static int encoder_allocate_framebuffer(struct encoder_info *enc)
{
	EncHandle handle = enc->handle;
	int i, enc_stride, src_stride, src_fbid;
	int totalfb, minfbcount, srcfbcount, extrafbcount;
	RetCode ret;
	FrameBuffer *fb;
	PhysicalAddress subSampBaseA = 0, subSampBaseB = 0;
	struct frame_buf **pfbpool;
	EncExtBufInfo extbufinfo;
	int enc_fbwidth, enc_fbheight, src_fbwidth, src_fbheight;

	minfbcount = enc->minFrameBufferCount;
	info_msg("Encoder: minimum framebuffers %d\n", minfbcount);
	srcfbcount = 1;

	enc_fbwidth = (enc->enc_picwidth + 15) & ~15;
	enc_fbheight = (enc->enc_picheight + 15) & ~15;
	src_fbwidth = (enc->src_picwidth + 15) & ~15;
	src_fbheight = (enc->src_picheight + 15) & ~15;

	/* AVC + MVC */
	extrafbcount = 2 + 2; /* Subsamp [2] + Subsamp MVC [2] */

	enc->totalfb = totalfb = minfbcount + extrafbcount + srcfbcount;

	/* last framebuffer is used as src frame in the test */
	enc->src_fbid = src_fbid = totalfb - 1;

	fb = enc->fb = calloc(totalfb, sizeof(FrameBuffer));
	if (fb == NULL) {
		err_msg("Encoder: Failed to allocate enc->fb\n");
		return -1;
	}

	pfbpool = enc->pfbpool = calloc(totalfb,
					sizeof(struct frame_buf *));
	if (pfbpool == NULL) {
		err_msg("Encoder: Failed to allocate enc->pfbpool\n");
		free(fb);
		return -1;
	}

	/* All buffers are linear */
	for (i = 0; i < minfbcount + extrafbcount; i++) {
		pfbpool[i] = framebuf_alloc(2, enc->color_space,
					    enc_fbwidth, enc_fbheight, 0);
		if (pfbpool[i] == NULL) {
			goto ERROR;
		}
	}

	for (i = 0; i < minfbcount + extrafbcount; i++) {
		fb[i].myIndex = i;
		fb[i].bufY = pfbpool[i]->addrY;
		fb[i].bufCb = pfbpool[i]->addrCb;
		fb[i].bufCr = pfbpool[i]->addrCr;
		fb[i].strideY = pfbpool[i]->strideY;
		fb[i].strideC = pfbpool[i]->strideC;
	}

	subSampBaseA = fb[minfbcount].bufY;
	subSampBaseB = fb[minfbcount + 1].bufY;
	extbufinfo.subSampBaseAMvc = fb[minfbcount + 2].bufY;
	extbufinfo.subSampBaseBMvc = fb[minfbcount + 3].bufY;

	enc_stride = (enc->enc_picwidth + 15) & ~15;
	src_stride = (enc->src_picwidth + 15 ) & ~15;

	extbufinfo.scratchBuf = enc->scratchBuf;
	ret = vpu_EncRegisterFrameBuffer(handle, fb, minfbcount, enc_stride,
					 src_stride, subSampBaseA,
					 subSampBaseB, &extbufinfo);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Encoder: Register frame buffer failed\n");
		goto ERROR;
	}

	/* Allocate a single frame buffer for source frame */
	pfbpool[src_fbid] = framebuf_alloc(2, enc->color_space,
					   src_fbwidth, src_fbheight, 0);
	if (pfbpool[src_fbid] == NULL) {
		err_msg("Encoder: failed to allocate single framebuf\n");
		goto ERROR;
	}

	fb[src_fbid].myIndex = enc->src_fbid;
	fb[src_fbid].bufY = pfbpool[src_fbid]->addrY;
	fb[src_fbid].bufCb = pfbpool[src_fbid]->addrCb;
	fb[src_fbid].bufCr = pfbpool[src_fbid]->addrCr;
	fb[src_fbid].strideY = pfbpool[src_fbid]->strideY;
	fb[src_fbid].strideC = pfbpool[src_fbid]->strideC;

	return 0;

ERROR:
	for (i = 0; i < totalfb; i++) {
		framebuf_free(pfbpool[i]);
	}

	free(fb);
	free(pfbpool);
	return -1;
}

static void encoder_free_framebuffer(struct encoder_info *enc)
{
	int i;

	for (i = 0; i < enc->totalfb; i++) {
		framebuf_free(enc->pfbpool[i]);
	}

	free(enc->fb);
	free(enc->pfbpool);
}

static int encoder_open(struct encoder_info *enc)
{
	EncHandle handle;
	EncOpenParam encop;
	EncInitialInfo initinfo;
	RetCode ret;

	memset(&handle, 0 , sizeof(EncHandle));
	memset(&encop, 0 , sizeof(EncOpenParam));
	memset(&initinfo, 0 , sizeof(EncInitialInfo));

	/* Fill up parameters for encoding */
	encop.bitstreamBuffer = enc->phy_bsbuf_addr;
	encop.bitstreamBufferSize = STREAM_BUF_SIZE;
	encop.bitstreamFormat = 2;
	encop.mapType = 0;
	encop.linear2TiledEnable = 0;

	if (enc->src_picwidth < 0 || enc->src_picheight < 0) {
		err_msg("Encoder: Source picture size not set\n");
		return -1;
	}

	if (enc->enc_picwidth < 0 || enc->enc_picheight < 0) {
		err_msg("Encoder: Encoded picture size not set\n");
		return -1;
	}

	encop.picWidth = enc->enc_picwidth;
	encop.picHeight = enc->enc_picheight;

	/*Note: Frame rate cannot be less than 15fps per H.263 spec */
	encop.frameRateInfo = enc->enc_fps;
	info_msg("Encoder: frame rate is %d\n", (int)encop.frameRateInfo);
	encop.bitRate = enc->enc_bit_rate;
	info_msg("Encoder: bit rate is %d kbps\n", (int)encop.bitRate);
	encop.gopSize = enc->gop_size;
	info_msg("Encoder: GOP size is %d\n", (int)encop.gopSize);
	encop.slicemode.sliceMode = 0;	/* 0: 1 slice per picture; 1: Multiple slices per picture */
	encop.slicemode.sliceSizeMode = 0; /* 0: silceSize defined by bits; 1: sliceSize defined by MB number*/
	encop.slicemode.sliceSize = 4000;  /* Size of a slice in bits or MB numbers */

	encop.initialDelay = 0;
	encop.vbvBufferSize = 0;        /* 0 = ignore 8 */
	encop.intraRefresh = 0;
	encop.sliceReport = 0;
	encop.mbReport = 0;
	encop.mbQpReport = 0;
	encop.rcIntraQp = -1;
	encop.userQpMax = 0;
	encop.userQpMin = 0;
	encop.userQpMinEnable = 0;
	encop.userQpMaxEnable = 0;

	encop.IntraCostWeight = 0;
	encop.MEUseZeroPmv  = 0;
	/* (3: 16x16, 2:32x16, 1:64x32, 0:128x64, H.263(Short Header : always 3) */
	encop.MESearchRange = 3;

	encop.userGamma = (Uint32)(0.75*32768);         /*  (0*32768 <= gamma <= 1*32768) */
	encop.RcIntervalMode= 1;        /* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
	encop.MbInterval = 0;
	encop.avcIntra16x16OnlyModeEnable = 0;

	encop.ringBufferEnable = enc->ringBufferEnable = 0;
	encop.dynamicAllocEnable = 0;
	if (enc->color_space == NV12)
		encop.chromaInterleave = 1;
	else
		encop.chromaInterleave = 0;

	//AVC specific
	encop.EncStdParam.avcParam.avc_constrainedIntraPredFlag = 0;
	encop.EncStdParam.avcParam.avc_disableDeblk = 0;
	encop.EncStdParam.avcParam.avc_deblkFilterOffsetAlpha = 6;
	encop.EncStdParam.avcParam.avc_deblkFilterOffsetBeta = 0;
	encop.EncStdParam.avcParam.avc_chromaQpOffset = 10;
	encop.EncStdParam.avcParam.avc_audEnable = 0;
	//imx6 specific avc params
	encop.EncStdParam.avcParam.interview_en = 0;
	encop.EncStdParam.avcParam.paraset_refresh_en = enc->mvc_paraset_refresh_en = 0;
	encop.EncStdParam.avcParam.prefix_nal_en = 0;
	encop.EncStdParam.avcParam.mvc_extension = 0;
	enc->mvc_extension = 0;
	encop.EncStdParam.avcParam.avc_frameCroppingFlag = 0;
	encop.EncStdParam.avcParam.avc_frameCropLeft = 0;
	encop.EncStdParam.avcParam.avc_frameCropRight = 0;
	encop.EncStdParam.avcParam.avc_frameCropTop = 0;
	encop.EncStdParam.avcParam.avc_frameCropBottom = 0;

	ret = vpu_EncOpen(&handle, &encop);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Encoder: Encoder open failed %d\n", ret);
		return -1;
	}

	ret = vpu_EncGetInitialInfo(handle, &initinfo);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Encoder: Encoder GetInitialInfo failed\n");
		return -1;
	}

	enc->minFrameBufferCount = initinfo.minFrameBufferCount;

	memcpy(&enc->handle, &handle, sizeof(EncHandle));

	return 0;
}

static void encoder_close(struct encoder_info *enc)
{
	RetCode ret;

	/* free the temp buffer used to store avc headers */
	ret = vpu_EncClose(enc->handle);
	if (ret == RETCODE_FRAME_NOT_COMPLETE) {
		vpu_SWReset(enc->handle, 0);
		vpu_EncClose(enc->handle);
	}
}

static int encoder_get_headers(struct encoder_info *enc, struct mediaBuffer *enc_dst)
{
	EncHeaderParam enchdr_param;
	unsigned char *vbuf;
	int temp_size = 0;

	memset(&enchdr_param, 0, sizeof(EncHeaderParam));

	/* Must put encode header before encoding */
	enchdr_param.headerType = SPS_RBSP;
	vpu_EncGiveCommand(enc->handle, ENC_PUT_AVC_HEADER, &enchdr_param);
	/*Need to get the virtual address of the physical address from the
	  vpu bitstream buffer */
	vbuf = (unsigned char *)enc->virt_bsbuf_addr + 
		enchdr_param.buf - enc->phy_bsbuf_addr;
	/*Now copy the header buffer to the global abv header buffer, which
	  will then be pointed to the buffer that will be passed back */
	memcpy((unsigned char *)enc->virt_outbuf_addr, vbuf,
		enchdr_param.size);
	temp_size = enchdr_param.size;

	enchdr_param.headerType = PPS_RBSP;
	vpu_EncGiveCommand(enc->handle, ENC_PUT_AVC_HEADER, &enchdr_param);
	/*Get virtual address as done before. Then copy the rest of the 
	  headers into the global buffer to be passed back up completion */
	vbuf = (unsigned char *)enc->virt_bsbuf_addr + 
		enchdr_param.buf - enc->phy_bsbuf_addr;
	memcpy((unsigned char *)enc->virt_outbuf_addr + temp_size,
		vbuf, enchdr_param.size);
		temp_size += enchdr_param.size;

	enc_dst->dataSource = VPU_CODEC;
	enc_dst->bufOutSize = temp_size;
	enc_dst->height = enc->enc_picheight;
	enc_dst->width = enc->enc_picwidth;
	enc_dst->imageHeight = enc->enc_picheight;
	enc_dst->imageWidth = enc->enc_picwidth;
	enc_dst->vBufOut = (unsigned char*)enc->virt_outbuf_addr;
	enc_dst->pBufOut = (unsigned char*)enc->phy_outbuf_addr;

	return 0;
}

static int read_source_frame(struct encoder_info *enc, struct mediaBuffer *vid_src)
{
	unsigned char *vdst_y, *vdst_u, *vdst_v;
	unsigned char *pdst_y, *pdst_u, *pdst_v;
	unsigned char *vsrc_y, *vsrc_u, *vsrc_v;
	unsigned char *psrc_y, *psrc_u, *psrc_v;
	unsigned long *vdst_u_l, *vdst_v_l, *vsrc_u_l, *vsrc_v_l;
	struct frame_buf *pfb = enc->pfbpool[enc->src_fbid];
	FrameBuffer *fb = enc->fb;
	int src_fbid = enc->src_fbid;
	int format = vid_src->colorSpace;
	int chromaInterleave = 0;
	int img_size, y_size, c_size;
	int i, j, c_count;
	int ret = 0;
	/* Boolean used to help unpack packed formats */
	bool chroma;
	/* g2d buffers that will be used for zero copies */
	struct g2d_buf s_buf, d_buf;
	void *g2d_handle;

	if (enc->color_space == NV12)
		chromaInterleave = 1;

	if (enc->src_picwidth != pfb->strideY) {
		err_msg("Encoder: Make sure src pic width is a multiple of 16\n");
		return -1;
	}

	if ((format != YUV420P) && (format != YUYV) &&
	    (format != YUV422P) && (format != NV12)) {
		err_msg("Encoder: Video data is not in a valid color space\n");
		return -1;
	}

	y_size = enc->src_picwidth * enc->src_picheight;

	if ((format == YUV422P) || (format == YUYV)) {
		c_size = y_size / 2;
	} else
		c_size = y_size / 4;

	img_size = y_size + c_size * 2;

	vdst_y = (unsigned char*)
		 (pfb->addrY + pfb->desc.virt_uaddr - pfb->desc.phy_addr);
	vdst_u = (unsigned char*)
		 (pfb->addrCb + pfb->desc.virt_uaddr - pfb->desc.phy_addr);
	vdst_v = (unsigned char*)
		 (pfb->addrCr + pfb->desc.virt_uaddr - pfb->desc.phy_addr);

	pdst_y = (unsigned char*)fb[src_fbid].bufY;
	pdst_u = (unsigned char*)fb[src_fbid].bufCb;
	pdst_v = (unsigned char*)fb[src_fbid].bufCr;
	
	vsrc_y = vid_src->vBufOut;
	vsrc_u = vid_src->vBufOut + y_size;
	vsrc_v = vsrc_u + c_size;

	psrc_y = vid_src->pBufOut;
	psrc_u = vid_src->pBufOut + y_size;
	psrc_v = psrc_u + c_size;

	/* Read from YUV420 file source */
	if (vid_src->dataSource == FILE_SRC) {
		if (img_size == pfb->desc.size) {
			ret = freadn(vid_src->fd, (void *)vdst_y, img_size);
		} else {
			ret = freadn(vid_src->fd, (void *)vdst_y, y_size);
			if (chromaInterleave == 0) {
				ret = freadn(vid_src->fd, (void *)vdst_u, c_size);
				ret = freadn(vid_src->fd, (void *)vdst_v, c_size);
			} else {
				ret = freadn(vid_src->fd, (void *)vdst_u, c_size * 2);
			}
		}
		return ret;
	}

	/* If the source is from another VPU process (like decoding), then
	   the output buffer of that process can be given directly to the
	   vpu encoder input to avoid memcpy */
	if (vid_src->dataSource == VPU_CODEC) {
		if (format == YUV422P) {
			if(g2d_open(&g2d_handle)) {
				err_msg("Encoder: g2d_open fail.\n");
				return -1;
			}

			/* Use g2d to dma the Y component to the VPU input buffer */
			s_buf.buf_paddr = (int)psrc_y;
			s_buf.buf_vaddr = vsrc_y;
			d_buf.buf_paddr = (int)pdst_y;
			d_buf.buf_vaddr = vdst_y;
			g2d_copy(g2d_handle, &d_buf, &s_buf, y_size);
			g2d_finish(g2d_handle);
			g2d_close(g2d_handle);

			/* Now copy the U and V components over and decimate the
			   extra samples */
			j =0;
			i = 0;
			vdst_u_l = (unsigned long *)vdst_u;
			vdst_v_l = (unsigned long *)vdst_v;
			vsrc_u_l = (unsigned long *)vsrc_u;
			vsrc_v_l = (unsigned long *)vsrc_v;
			while (i < c_size/2) {
				*vdst_u_l = *vsrc_u_l;
				vdst_u_l++;
				vsrc_u_l++;
				*vdst_v_l = *vsrc_v_l;
				vdst_v_l++;
				vsrc_v_l++;
				i += 4;
				j += 4;
				if (j == enc->src_picwidth) {
					j = 0;
					vsrc_u_l += enc->src_picwidth/4;
					vsrc_v_l += enc->src_picwidth/4;
				}
			}
			return img_size;
		} else if (format == NV12) {
			if(g2d_open(&g2d_handle)) {
				err_msg("Encoder: g2d_open fail.\n");
				return -1;
			}

			/* Use g2d to dma the whole buffer to the VPU input buffer */
			s_buf.buf_paddr = (int)psrc_y;
			s_buf.buf_vaddr = vsrc_y;
			d_buf.buf_paddr = (int)pdst_y;
			d_buf.buf_vaddr = vdst_y;
			g2d_copy(g2d_handle, &d_buf, &s_buf, img_size);
			g2d_finish(g2d_handle);
			g2d_close(g2d_handle);
			return img_size;
		}
	}

	/* If we have reached this point, it means there were no special
	   cases encountered. The source data will be copied into the 
	   input buffer of the vpu encoder. */	
	if (format == YUYV) {	
		i = 0;
		c_count = 0;
		chroma = true; 
		/* Unpack YUYV and put into YUV420p format */
		while (i < vid_src->bufOutSize) {
			/* Y */
			*vdst_y = *vsrc_y;
			vdst_y++;
			vsrc_y++;
			/* U */
			if (chroma) {
				*vdst_u = *vsrc_y;
				vdst_u++;
			}
			vsrc_y++;
			/* Y */
			*vdst_y = *vsrc_y;
			vdst_y++;
			vsrc_y++;
			/* V */
			if (chroma) {
				*vdst_v = *vsrc_y;
				vdst_v++;
			}
			vsrc_y++;
			c_count++;
			if (c_count == enc->src_picwidth) {
				chroma = !chroma;
				c_count = 0;
			}
			i += 4;
		}
		return vid_src->bufOutSize;
	} else {
		err_msg("Encoder: Unsupported format for VPU input\n");
		return -1;
	}
}
