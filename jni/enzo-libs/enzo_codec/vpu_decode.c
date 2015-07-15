#include "vpu_decode.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/*When AVC data has parameter change, the VPU decode frame function needs
 *to iterate a few hundred times. I'm not sure why this is, but it needs to
 *happen. This loop count is to set a limit on how many times we loop through
 * decode one frame when param change occurs. */
#define AVC_PARAM_LOOP_MAX	1000

/* Function prototypes */
static int decoder_open(struct decoder_info *dec, struct mediaBuffer *enc_src);
static void decoder_close(struct decoder_info *dec);
static int decoder_parse(struct decoder_info *dec);
static int decoder_allocate_framebuffer(struct decoder_info *dec);
static void decoder_free_framebuffer(struct decoder_info *dec);
static int decoder_decode_frame(struct decoder_info *dec, struct mediaBuffer *enc_src,
			 	struct mediaBuffer *vid_dst);
static int dec_fill_bsbuffer(DecHandle handle, struct mediaBuffer *enc_src,
		u32 bs_va_startaddr, u32 bs_va_endaddr,
		u32 bs_pa_startaddr, int defaultsize,
		int *eos, int *fill_end_bs);
static void write_to_dst(struct decoder_info *dec,
			 struct mediaBuffer *vid_dst, int index);
/* End function prototypes */

int vpu_decoder_init(struct decoder_info *dec, struct mediaBuffer *enc_src)
{
	int ret;

	dec->bs_mem_desc.size = STREAM_BUF_SIZE;
	ret = IOGetPhyMem(&dec->bs_mem_desc);
	if (ret) {
		err_msg("Decoder: Unable to obtain physical mem\n");
		return -1;
	}

	if (IOGetVirtMem(&dec->bs_mem_desc) <= 0) {
		err_msg("Decoder: Unable to obtain virtual mem\n");
		IOFreePhyMem(&dec->bs_mem_desc);
		return -1;
	}

	if (dec->format == H264AVC) {
		dec->ps_mem_desc.size = PS_SAVE_SIZE;
		ret = IOGetPhyMem(&dec->ps_mem_desc);
		if (ret) {
			err_msg("Decoder: Unable to obtain"
				"physical ps save mem\n");
			IOFreePhyMem(&dec->bs_mem_desc);
			return -1;
		}
		dec->phy_ps_buf=(PhysicalAddress)(&dec->ps_mem_desc.phy_addr);
	}

	dec->phy_bsbuf_addr = dec->bs_mem_desc.phy_addr;
	dec->virt_bsbuf_addr = dec->bs_mem_desc.virt_uaddr;

	dec->reorderEnable = 1;
	dec->tiled2LinearEnable = 0;

	dec->userData.enable = 0;
	dec->mbInfo.enable = 0;
	dec->mvInfo.enable = 0;
	dec->frameBufStat.enable = 0;

	/* open decoder */
	ret = decoder_open(dec, enc_src);
	if (ret) {
		err_msg("Decoder: Unable to open decoder instance\n");
		IOFreePhyMem(&dec->bs_mem_desc);
		IOFreePhyMem(&dec->ps_mem_desc);
		return -1;
	}

	return 0;
}

int vpu_decoder_deinit(struct decoder_info *dec)
{
	info_msg("Decoder: Closing the decoder\n");
	decoder_free_framebuffer(dec);
	decoder_close(dec);
	return 0;
}
int vpu_decoder_decode_frame(struct decoder_info *dec,
			 struct mediaBuffer *enc_src,
			 struct mediaBuffer *vid_dst)
{
	int ret;
	/* start decoding */
	ret = decoder_decode_frame(dec, enc_src, vid_dst);

	return ret;
}

static int decoder_open(struct decoder_info *dec, struct mediaBuffer *enc_src)
{
	RetCode ret;
	DecHandle handle;
	DecOpenParam oparam;
	int eos = 0, fill_end_bs = 0, fillsize = 0;

	memset(&handle, 0 , sizeof(DecHandle));
	memset(&oparam, 0 , sizeof(DecOpenParam));

	if (dec->format == MJPEG) {
		oparam.bitstreamFormat = STD_MJPG;
		oparam.chromaInterleave = 0;
		info_msg("Decoder: MJPEG requested\n");
	} else if (dec->format == H264AVC) {
		oparam.bitstreamFormat = STD_AVC;
		oparam.chromaInterleave = 1;
		info_msg("Decoder: H264 AVC requested\n");
	} else {
		err_msg("Decoder: Unsupported format\n");
		return -1;
	}

	oparam.bitstreamBuffer = dec->phy_bsbuf_addr;
	oparam.bitstreamBufferSize = STREAM_BUF_SIZE;
	oparam.pBitStream = (Uint8 *)dec->virt_bsbuf_addr;
	oparam.reorderEnable = 1;
	oparam.mp4DeblkEnable = 0;
	oparam.mjpg_thumbNailDecEnable = 0;
	oparam.mapType = 0;
	oparam.tiled2LinearEnable = 0;
	oparam.bitstreamMode = 1;

	/* These are for H264 AVC */
	oparam.psSaveBuffer = dec->phy_ps_buf;
	oparam.psSaveBufferSize = PS_SAVE_SIZE;

	info_msg("Decoder: Opening the decoder\n");
	ret = vpu_DecOpen(&handle, &oparam);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Decoder: vpu_DecOpen failed, ret:%d\n", ret);
		return -1;
	}

	memcpy(&dec->handle, &handle, sizeof(DecHandle));

	info_msg("Decoder: Parsing input data\n");
	ret = dec_fill_bsbuffer(dec->handle, enc_src,
			dec->virt_bsbuf_addr,
			(dec->virt_bsbuf_addr + STREAM_BUF_SIZE),
			dec->phy_bsbuf_addr, fillsize, &eos, &fill_end_bs);

	if (fill_end_bs)
		err_msg("Decoder: Update 0 before seqinit, \
			 fill_end_bs=%d\n", fill_end_bs);

	if (ret < 0) {
		err_msg("Decoder: dec_fill_bsbuffer failed\n");
		return -1;
	}

	/* parse the bitstream */
	ret = decoder_parse(dec);
	if (ret) {
		err_msg("Decoder: Parse failed\n");
		return -1;
	}

	if (dec->format == H264AVC) {
		dec->slice_mem_desc.size = dec->phy_slicebuf_size;
		ret = IOGetPhyMem(&dec->slice_mem_desc);
		if (ret) {
			err_msg("Decoder: Unable to obtain"
				"physical slice save mem\n");
			return -1;
		}
		dec->phy_slice_buf = dec->slice_mem_desc.phy_addr;
	}

	/* allocate frame buffers */
	info_msg("Decoder: Allocating framebuffer\n");
	ret = decoder_allocate_framebuffer(dec);
	if (ret) {
		err_msg("Decoder: Couldn't allocate framebuffer\n");
		IOFreePhyMem(&dec->slice_mem_desc);
		return -1;
	}

	return 0;
}

static void decoder_close(struct decoder_info *dec)
{
	RetCode ret;

	ret = vpu_DecClose(dec->handle);
	if (ret == RETCODE_FRAME_NOT_COMPLETE) {
		vpu_SWReset(dec->handle, 0);
		ret = vpu_DecClose(dec->handle);
		if (ret != RETCODE_SUCCESS)
			err_msg("Decoder: vpu_DecClose failed\n");
	}

	IOFreePhyMem(&dec->bs_mem_desc);
	if (dec->format == H264AVC) {
		IOFreePhyMem(&dec->slice_mem_desc);
		IOFreePhyMem(&dec->ps_mem_desc);
	}

	return;
}

static int decoder_allocate_framebuffer(struct decoder_info *dec)
{
	DecBufInfo bufinfo;
	int i, regfbcount = dec->regfbcount, totalfb, mvCol;
	int vpu_fmt = 0;
	int color_space = 0;
	int stride = 0;
	RetCode ret;
	DecHandle handle = dec->handle;
	FrameBuffer *fb;
	struct frame_buf **pfbpool;
	int delay = -1;

	color_space = dec->color_space;

	totalfb = regfbcount + dec->extrafb;
	info_msg("Decoder: regfb %d, extrafb %d\n", regfbcount, dec->extrafb);

	fb = dec->fb = calloc(totalfb, sizeof(FrameBuffer));
	if (fb == NULL) {
		err_msg("Decoder: Failed to allocate fb\n");
		return -1;
	}

	pfbpool = dec->pfbpool = calloc(totalfb, sizeof(struct frame_buf *));
	if (pfbpool == NULL) {
		err_msg("Decoder: Failed to allocate pfbpool\n");
		free(dec->fb);
		dec->fb = NULL;
		return -1;
	}

	if (dec->format == MJPEG) {
		mvCol = 0;
		vpu_fmt = STD_MJPG;
	} else {
		mvCol = 1;
		vpu_fmt = STD_AVC;
	}

	/* All buffers are linear */
	for (i = 0; i < totalfb; i++) {
		
		pfbpool[i] = framebuf_alloc(vpu_fmt, color_space,
				    dec->stride, dec->picheight, mvCol);
		if (pfbpool[i] == NULL) {
			err_msg("Decoder: framebuf_alloc returned NULL\n");
			goto err;
			}
	}

	for (i = 0; i < totalfb; i++) {
		fb[i].myIndex = i;
		fb[i].bufY = pfbpool[i]->addrY;
		fb[i].bufCb = pfbpool[i]->addrCb;
		fb[i].bufCr = pfbpool[i]->addrCr;
		fb[i].bufMvCol = pfbpool[i]->mvColBuf;
	}

	stride = ((dec->stride + 15) & ~15);

	if (dec->format == H264AVC) {
		bufinfo.avcSliceBufInfo.bufferBase = dec->phy_slice_buf;
		bufinfo.avcSliceBufInfo.bufferSize = dec->phy_slicebuf_size;
	}

	/* User needs to fill max suported macro block value of frame as following*/
	bufinfo.maxDecFrmInfo.maxMbX = dec->stride / 16;
	bufinfo.maxDecFrmInfo.maxMbY = dec->picheight / 16;
	bufinfo.maxDecFrmInfo.maxMbNum = dec->stride * dec->picheight / 256;

	/* For H.264, we can overwrite initial delay calculated from syntax.
	 * delay can be 0,1,... (in unit of frames)
	 * Set to -1 or do not call this command if you don't want to overwrite it.
	 * Take care not to set initial delay lower than reorder depth of the clip,
	 * otherwise, display will be out of order. */
	vpu_DecGiveCommand(handle, DEC_SET_FRAME_DELAY, &delay);

	ret = vpu_DecRegisterFrameBuffer(handle, fb, dec->regfbcount, stride, &bufinfo);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Register frame buffer failed, ret=%d\n", ret);
		goto err;
	}

	dec->totalfb = totalfb;

	return 0;

err:
	for (i = 0; i < totalfb; i++) {
		framebuf_free(pfbpool[i]);
	}

	free(dec->fb);
	free(dec->pfbpool);
	dec->fb = NULL;
	dec->pfbpool = NULL;
	return -1;
}

static void decoder_free_framebuffer(struct decoder_info *dec)
{
	int i;

	for (i = 0; i < dec->totalfb; i++) {
		framebuf_free(dec->pfbpool[i]);
	}

	if (dec->fb) {
		free(dec->fb);
		dec->fb = NULL;
	}
	if (dec->pfbpool) {
		free(dec->pfbpool);
		dec->pfbpool = NULL;
	}

	return;
}

static int decoder_parse(struct decoder_info *dec)
{
	DecInitialInfo initinfo;
	DecHandle handle = dec->handle;
	int align, extended_fbcount;
	RetCode ret;
	char *count;

	memset(&initinfo, 0, sizeof(DecInitialInfo));

	/* Parse bitstream and get width/height/framerate etc */
	vpu_DecSetEscSeqInit(handle, 1);
	ret = vpu_DecGetInitialInfo(handle, &initinfo);
	vpu_DecSetEscSeqInit(handle, 0);
	if (ret != RETCODE_SUCCESS) {
		err_msg("Decoder: vpu_DecGetInitialInfo failed, ret:%d, errorcode:%ld\n",
		         ret, initinfo.errorcode);
		return -1;
	}

	if (dec->format == H264AVC) {
		info_msg("Decoder: H.264 Profile: %d Level: %d Interlace: %d\n",
			initinfo.profile, initinfo.level, initinfo.interlace);

		if (initinfo.aspectRateInfo) {
			int aspect_ratio_idc;
			int sar_width, sar_height;

			if ((initinfo.aspectRateInfo >> 16) == 0) {
				aspect_ratio_idc = (initinfo.aspectRateInfo & 0xFF);
				info_msg("Decoder: aspect_ratio_idc: %d\n",
					aspect_ratio_idc);
			} else {
				sar_width = (initinfo.aspectRateInfo >> 16) & 0xFFFF;
				sar_height = (initinfo.aspectRateInfo & 0xFFFF);
				info_msg("Decoder: sar_width: %d, sar_height: %d\n",
					sar_width, sar_height);
			}
		dec->color_space = NV12;
		} else {
			info_msg("Decoder: Aspect Ratio is not present.\n");
		}
	} else if (dec->format == MJPEG) {
		dec->mjpg_fmt = initinfo.mjpg_sourceFormat;
		info_msg("Decoder: MJPG SourceFormat: %d\n",
			initinfo.mjpg_sourceFormat);
		dec->color_space = YUV422P;
	}

	dec->lastPicWidth = initinfo.picWidth;
	dec->lastPicHeight = initinfo.picHeight;

	info_msg("Decoder: width = %d, height = %d, frameRateRes = %lu, frameRateDiv = %lu, count = %u\n",
		initinfo.picWidth, initinfo.picHeight,
		initinfo.frameRateRes, initinfo.frameRateDiv,
		initinfo.minFrameBufferCount);

	/*
	 * We suggest to add two more buffers than minFrameBufferCount:
	 *
	 * vpu_DecClrDispFlag is used to control framebuffer whether can be
	 * used for decoder again. One framebuffer dequeue from IPU is delayed
	 * for performance improvement and one framebuffer is delayed for
	 * display flag clear.
	 *
	 * Performance is better when more buffers are used if IPU performance
	 * is bottleneck.
	 *
	 * Two more buffers may be needed for interlace stream from IPU DVI view
	 */
	dec->minfbcount = initinfo.minFrameBufferCount;
	count = getenv("VPU_EXTENDED_BUFFER_COUNT");
	if (count)
		extended_fbcount = atoi(count);
	else
		extended_fbcount = 2;

	if (initinfo.interlace)
		dec->regfbcount = dec->minfbcount + extended_fbcount + 2;
	else
		dec->regfbcount = dec->minfbcount + extended_fbcount;
	info_msg("Decoder: minfb %d, extfb %d\n", dec->minfbcount, extended_fbcount);

	dec->picwidth = ((initinfo.picWidth + 15) & ~15);
	align = 16;
	dec->picheight = ((initinfo.picHeight + align - 1) & ~(align - 1));

	if ((dec->picwidth == 0) || (dec->picheight == 0))
		return -1;

	/*
	 * Information about H.264 decoder picture cropping rectangle which
	 * presents the offset of top-left point and bottom-right point from
	 * the origin of frame buffer.
	 *
	 * By using these four offset values, host application can easily
	 * detect the position of target output window. When display cropping
	 * is off, the cropping window size will be 0.
	 *
	 * This structure for cropping rectangles is only valid for H.264
	 * decoder case.
	 */

	/* Add non-h264 crop support, assume left=top=0 */
	if ((dec->picwidth > initinfo.picWidth ||
		dec->picheight > initinfo.picHeight) &&
		(!initinfo.picCropRect.left &&
		!initinfo.picCropRect.top &&
		!initinfo.picCropRect.right &&
		!initinfo.picCropRect.bottom)) {
		initinfo.picCropRect.left = 0;
		initinfo.picCropRect.top = 0;
		initinfo.picCropRect.right = initinfo.picWidth;
		initinfo.picCropRect.bottom = initinfo.picHeight;
	}

	info_msg("Decoder: Crop left/top/right/bottom %lu %lu %lu %lu\n",
					initinfo.picCropRect.left,
					initinfo.picCropRect.top,
					initinfo.picCropRect.right,
					initinfo.picCropRect.bottom);

	memcpy(&(dec->picCropRect), &(initinfo.picCropRect),
					sizeof(initinfo.picCropRect));

	/* worstSliceSize is in kilo-byte unit */
	dec->phy_slicebuf_size = initinfo.worstSliceSize * 1024;
	dec->stride = dec->picwidth;

	//if (initinfo.mjpg_sourceFormat == 1) {
	//}

	return 0;
}

/*
 * Fill the bitstream ring buffer
 */
static int dec_fill_bsbuffer(DecHandle handle, struct mediaBuffer *enc_src,
		u32 bs_va_startaddr, u32 bs_va_endaddr,
		u32 bs_pa_startaddr, int defaultsize,
		int *eos, int *fill_end_bs)
{
	RetCode ret;
	PhysicalAddress pa_read_ptr, pa_write_ptr;
	u32 target_addr, space;
	int size;
	int nread = 0, room = 0;
	*eos = 0;


	ret = vpu_DecGetBitstreamBuffer(handle, &pa_read_ptr, &pa_write_ptr,
					&space);

	/* Make sure that we only write the actual size of a camera frame
	   to the VPU */
	if (enc_src->dataSource == V4L2_CAM ||
	    enc_src->dataSource == VPU_CODEC ||
	    enc_src->dataSource == BUFFER)
	{
		defaultsize = enc_src->bufOutSize;
	}

	if (ret != RETCODE_SUCCESS) {
		err_msg("Decoder: vpu_DecGetBitstreamBuffer failed\n");
		return -1;
	}

	/* Decoder bitstream buffer is full */
	if (space <= 0) {
		//warn_msg("Decoder: space %lu <= 0\n", space);
		return 0;
	}

	if (defaultsize > 0) {
		if ((int)space < defaultsize) {
			//warn_msg("Decoder: space %lu < default size\n", space);
			return 0;
		}

		size = defaultsize;
	} else {
		size = ((space >> 9) << 9);
	}

	if (size == 0) {
		warn_msg("Decoder: size == 0, space %lu\n", space);
		return 0;
	}

	/* Fill the bitstream buffer */
	target_addr = bs_va_startaddr + (pa_write_ptr - bs_pa_startaddr);
	if ((target_addr + size) > bs_va_endaddr) {
		/* In this case we have less room available in the bit stream
		   buffer than the amount we need to write (we have reached
		   the end of the ring buffer */
		room = bs_va_endaddr - target_addr;
		if (enc_src->dataSource == FILE_SRC) {
			nread = freadn(enc_src->fd, (void *)target_addr, room);
			if (nread <= 0) {
				/* EOF or error */
				//err_msg("Decoder: EOF or error\n");
				if (nread < 0) {
					if (nread == -EAGAIN)
						return 0;

					err_msg("Decoder: nread %d < 0\n", nread);
					return -1;
				}

				*eos = 1;
			} else {
				/* unable to fill the requested size, so back off! */
				if (nread != room) {
					goto update;
				}

				/* read the remaining */
				space = nread;
				nread = freadn(enc_src->fd, (void *)bs_va_startaddr,
					       (size - room));
				if (nread <= 0) {
					/* EOF or error */
					if (nread < 0) {
						if (nread == -EAGAIN)
							return 0;

						err_msg("Decoder: nread %d < 0\n", nread);
						return -1;
					}

					*eos = 1;
				}

				nread += space;
			}
		} else 	if (enc_src->dataSource == V4L2_CAM ||
			    enc_src->dataSource == VPU_CODEC ||
			    enc_src->dataSource == BUFFER)
			{
			/* A point has been reached where the remaining space
			   in the ring buffer is not enough to fit a full 
			   camera frame */

			/* First write whatever can fit into the remaining room
			   in the bitstream buffer */
			memcpy((char *)target_addr, enc_src->vBufOut, room);

			/* Now write the remaining camera buffer to the 
			   beginning of the ring buffer */
			memcpy((char *)bs_va_startaddr, enc_src->vBufOut + room,
				size - room);
			nread = size;	
		} else {
			err_msg("Decoder: unsupported data source for decode\n");
			return -1;
		}
	} else {
		/* This is the case when the amount of data we want to write
		   into the bit stream buffer is less than the available
		   space. */
		if (enc_src->dataSource == FILE_SRC) {
			nread = freadn(enc_src->fd, (void *)target_addr, size);
		}
		else if (enc_src->dataSource == V4L2_CAM ||
			 enc_src->dataSource == VPU_CODEC ||
			 enc_src->dataSource == BUFFER)
		{
			memcpy((char *)target_addr, enc_src->vBufOut, size);
			nread = size;
		}
		else {
			err_msg("Decoder: unsupported data source for decode\n");
		}
		if (nread <= 0) {
			/* EOF or error */
			//err_msg("Decoder: EOF or error\n");
			if (nread < 0) {
				if (nread == -EAGAIN)
					return 0;

				err_msg("Decoder: nread %d < 0\n", nread);
				return -1;
			}

			*eos = 1;
		}
	}


update:
	if (*eos == 0) {
		ret = vpu_DecUpdateBitstreamBuffer(handle, nread);
		if (ret != RETCODE_SUCCESS) {
			err_msg("Decoder: vpu_DecUpdateBitstreamBuffer failed\n");
			return -1;
		}
		*fill_end_bs = 0;
	} else {
		if (!*fill_end_bs) {
			ret = vpu_DecUpdateBitstreamBuffer(handle,
					STREAM_END_SIZE);
			if (ret != RETCODE_SUCCESS) {
				err_msg("Decoder: vpu_DecUpdateBitstreamBuffer failed"
								"\n");
				return -1;
			}
			*fill_end_bs = 1;
		}

	}

	return nread;
}

static int decoder_decode_frame(struct decoder_info *dec, struct mediaBuffer *enc_src,
			 struct mediaBuffer *vid_dst)
{
	DecHandle handle = dec->handle;
	DecOutputInfo outinfo;
	DecParam decparam;
	int rot_en = 0, rot_stride, fwidth, fheight;
	int rot_angle = 0;
	int dering_en = 0;
	FrameBuffer *fb = dec->fb;
	int err = 0, eos = 0, fill_end_bs = 0, decodefinish = 0;
	RetCode ret;
	int loop_id;
	u32 img_size;
	double frame_id = 0;
	int decIndex = 0;
	int rotid = 0, mirror;
	int totalNumofErrMbs = 0;
	int disp_clr_index = -1, actual_display_index = -1;
	int is_waited_int = 0;
	int tiled2LinearEnable = 0;
	char *delay_ms, *endptr;
	int return_code = 0;
	int param_change_loop = 0;

	memset(&outinfo, 0, sizeof(DecOutputInfo));
	memset(&decparam, 0, sizeof(DecParam));

	/*
	 * For mx6x MJPG decoding with streaming mode
	 * bitstream buffer filling cannot be done when JPU is in decoding,
	 * there are three places can do this:
	 * 1. before vpu_DecStartOneFrame;
	 * 2. in the case of RETCODE_JPEG_BIT_EMPTY returned in DecStartOneFrame() func;
	 * 3. after vpu_DecGetOutputInfo.
	 */

	err = dec_fill_bsbuffer(handle, enc_src,
		    dec->virt_bsbuf_addr,
		    (dec->virt_bsbuf_addr + STREAM_BUF_SIZE),
		    dec->phy_bsbuf_addr, STREAM_FILL_SIZE,
		    &eos, &fill_end_bs);
	if (err < 0) {
		err_msg("Decoder: dec_fill_bsbuffer failed\n");
		return DEC_ERROR;
	}

	while (param_change_loop < AVC_PARAM_LOOP_MAX) {

		param_change_loop = false;
		disp_clr_index = dec->disp_clr_index;

		if (dec->format == MJPEG)
			rotid = 0;

		decparam.dispReorderBuf = 0;

		decparam.skipframeMode = 0;
		decparam.skipframeNum = 0;
		/*
		 * once iframeSearchEnable is enabled, prescanEnable, prescanMode
		 * and skipframeMode options are ignored.
		 */
		decparam.iframeSearchEnable = 0;

		fwidth = ((dec->picwidth + 15) & ~15);
		fheight = ((dec->picheight + 15) & ~15);

		if (rot_en || dering_en || tiled2LinearEnable || (dec->format == MJPEG)) {
			/*
			 * VPU is setting the rotation angle by counter-clockwise.
			 * We convert it to clockwise, which is consistent with V4L2
			 * rotation angle strategy.
			 */
			if (rot_en) {
				if (rot_angle == 90 || rot_angle == 270)
					rot_angle = 360 - rot_angle;
			} else
				rot_angle = 0;

			vpu_DecGiveCommand(handle, SET_ROTATION_ANGLE,
						&rot_angle);

			mirror = 0;
			vpu_DecGiveCommand(handle, SET_MIRROR_DIRECTION,
						&mirror);

			if (rot_en)
				rot_stride = (rot_angle == 90 || rot_angle == 270) ?
						fheight : fwidth;
			else
				rot_stride = fwidth;
			vpu_DecGiveCommand(handle, SET_ROTATOR_STRIDE, &rot_stride);
		}

		img_size = dec->picwidth * dec->picheight * 3 / 2;

		if (rot_en || dering_en || tiled2LinearEnable || (dec->format == MJPEG)) {
			vpu_DecGiveCommand(handle, SET_ROTATOR_OUTPUT,
						(void *)&fb[rotid]);
			if (frame_id == 0) {
				if (rot_en) {
					vpu_DecGiveCommand(handle,
							ENABLE_ROTATION, 0);
					vpu_DecGiveCommand(handle,
							ENABLE_MIRRORING,0);
				}
				if (dering_en) {
					vpu_DecGiveCommand(handle,
							ENABLE_DERING, 0);
				}
			}
		}

		ret = vpu_DecStartOneFrame(handle, &decparam);
		if (ret == RETCODE_JPEG_EOS) {
			info_msg("Decoder: JPEG bitstream is end\n");
			return DEC_ERROR;
		} else if (ret == RETCODE_JPEG_BIT_EMPTY) {
			err = dec_fill_bsbuffer(handle, enc_src,
				    dec->virt_bsbuf_addr,
				    (dec->virt_bsbuf_addr + STREAM_BUF_SIZE),
				    dec->phy_bsbuf_addr, STREAM_FILL_SIZE,
				    &eos, &fill_end_bs);
			if (err < 0) {
				err_msg("Decoder: dec_fill_bsbuffer failed\n");
				return DEC_ERROR;
			}
		}

		if (ret != RETCODE_SUCCESS) {
			err_msg("Decoder: DecStartOneFrame failed, ret=%d\n", ret);
			return DEC_ERROR;
		}

		is_waited_int = 0;
		loop_id = 0;
		while (vpu_IsBusy()) {
			if (dec->format != MJPEG) {
				//Avoid doing this for now. Fill buffer beginning of sequence instead.
				/*err = dec_fill_bsbuffer(handle, enc_src,
					    dec->virt_bsbuf_addr,
					    (dec->virt_bsbuf_addr + STREAM_BUF_SIZE),
					    dec->phy_bsbuf_addr, STREAM_FILL_SIZE,
					    &eos, &fill_end_bs);
				if (err < 0) {
					err_msg("Decoder: dec_fill_bsbuffer failed\n");
					return -1;
				}*/
			}
			/*
			 * Suppose vpu is hang if one frame cannot be decoded in 5s,
			 * then do vpu software reset.
			 * Please take care of this for network case since vpu
			 * interrupt also cannot be received if no enough data.
			 */
			if (loop_id == 50) {
				err = vpu_SWReset(handle, 0);
				return DEC_ERROR;
			}

			vpu_WaitForInt(100);
			is_waited_int = 1;
			loop_id ++;
		}

		if (!is_waited_int)
			vpu_WaitForInt(100);

		ret = vpu_DecGetOutputInfo(handle, &outinfo);

		/* In 8 instances test, we found some instance(s) may not get a chance to be scheduled
		 * until timeout, so we yield schedule each frame explicitly.
		 * This may be kernel dependant and may be removed on customer platform */
		usleep(0);

		if ((dec->format == MJPEG) &&
		    (outinfo.indexFrameDisplay == 0)) {
			outinfo.indexFrameDisplay = rotid;
		}

		if (ret != RETCODE_SUCCESS) {
			err_msg("Decoder: vpu_DecGetOutputInfo failed Err code is %d\n"
				"\tframe_id = %d\n", ret, (int)frame_id);
			return DEC_ERROR;
		}

		if (outinfo.decodingSuccess == 0) {
			//warn_msg("Decoder: Incomplete finish of decoding process.\n");
			if ((outinfo.indexFrameDecoded >= 0) && (outinfo.numOfErrMBs)) {
				if (enc_src->dataType == MJPEG)
					info_msg("Decoder: Error Mb info:0x%x,\n",
						outinfo.numOfErrMBs);
			}
		}

		/*if (outinfo.decodingSuccess & 0x10) {
			warn_msg("Decoder: vpu needs more bitstream in rollback mode\n");

			// Don't think buffer should fill here since frames are being taken
			//   one at a time
			err = dec_fill_bsbuffer(handle,  enc_src, dec->virt_bsbuf_addr,
					(dec->virt_bsbuf_addr + STREAM_BUF_SIZE),
					dec->phy_bsbuf_addr, 0, &eos, &fill_end_bs);
			if (err < 0) {
				err_msg("Decoder: dec_fill_bsbuffer failed\n");
				return DEC_ERROR;
			}
		}*/

		if (outinfo.notSufficientPsBuffer) {
			err_msg("Decoder: PS Buffer overflow\n");
			return DEC_ERROR;
		}

		if (outinfo.notSufficientSliceBuffer) {
			err_msg("Decoder: Slice Buffer overflow\n");
			return DEC_ERROR;
		}

		if (outinfo.indexFrameDisplay == -1)
			return DEC_ERROR;
		else if ((outinfo.indexFrameDisplay > dec->regfbcount) &&
			 (outinfo.prescanresult != 0) && !cpu_is_mx6x())
			decodefinish = 1;

		if (decodefinish && (!(rot_en || dering_en || tiled2LinearEnable)))
			return DEC_NEW_FRAME;

		if(outinfo.indexFrameDecoded >= 0) {
			/* We MUST be careful of sequence param change (resolution change, etc)
			 * Different frame buffer number or resolution may require vpu_DecClose
			 * and vpu_DecOpen again to reallocate sufficient resources.
			 * If you already allocate enough frame buffers of max resolution
			 * in the beginning, you may not need vpu_DecClose, etc. But sequence
			 * headers must be ahead of their pictures to signal param change.
			 */
			if ((outinfo.decPicWidth != dec->lastPicWidth)
					||(outinfo.decPicHeight != dec->lastPicHeight)) {
				warn_msg("Decoder: resolution changed from %dx%d to %dx%d\n",
						dec->lastPicWidth, dec->lastPicHeight,
						outinfo.decPicWidth, outinfo.decPicHeight);
				dec->lastPicWidth = outinfo.decPicWidth;
				dec->lastPicHeight = outinfo.decPicHeight;
			}

			if (outinfo.numOfErrMBs) {
				totalNumofErrMbs += outinfo.numOfErrMBs;
				info_msg("Decoder: Num of Error Mbs : %d\n",
						outinfo.numOfErrMBs);
			}
		}

		if(outinfo.indexFrameDecoded >= 0)
			decIndex++;

		/* BIT don't have picture to be displayed */
		if ((outinfo.indexFrameDisplay == -3) ||
				(outinfo.indexFrameDisplay == -2)) {
			//err_msg("Decoder: VPU doesn't have picture to be displayed.\n"
			//	"\toutinfo.indexFrameDisplay = %d\n",
			//			outinfo.indexFrameDisplay);

			if (enc_src->dataType != MJPEG && disp_clr_index >= 0) {
				err = vpu_DecClrDispFlag(handle, disp_clr_index);
				if (err)
					err_msg("Decoder: vpu_DecClrDispFlag failed Error code"
							" %d\n", err);
			}
			disp_clr_index = outinfo.indexFrameDisplay;
			return_code = DEC_NO_NEW_FRAME;
			param_change_loop++;
			continue;
		}

		if (rot_en || dering_en || tiled2LinearEnable || (dec->format == MJPEG)) {
			/* delay one more frame for PP */
			if ((dec->format != MJPEG) && (disp_clr_index < 0)) {
				disp_clr_index = outinfo.indexFrameDisplay;
			}
			actual_display_index = rotid;
		}
		else
			actual_display_index = outinfo.indexFrameDisplay;

		if (outinfo.indexFrameDisplay >= 0) {
			write_to_dst(dec, vid_dst, actual_display_index);
		} else {
			//warn_msg("Decoder: no new frame to output\n");
			return_code = DEC_NO_NEW_FRAME;
		}

		if (dec->format != MJPEG && disp_clr_index >= 0) {
			err = vpu_DecClrDispFlag(handle,disp_clr_index);
			if (err)
				err_msg("Decoder: vpu_DecClrDispFlag failed Error code"
					" %d\n", err);
		}
		dec->disp_clr_index = outinfo.indexFrameDisplay;

		delay_ms = getenv("VPU_DECODER_DELAY_MS");
		if (delay_ms && strtol(delay_ms, &endptr, 10))
			usleep(strtol(delay_ms,&endptr, 10) * 1000);

		if (totalNumofErrMbs) {
			info_msg("Decoder: Total Num of Error MBs : %d\n",
				totalNumofErrMbs);
		}

		/* If we get here, we don't need to loop. Looping will only
		 * when the parameters of the encoded data have changed. For
		 * some reason, the VPU decode loop needs to iterate a few
		 * hundred times before it resumes normal operation after a
		 * parameter change occurs. */
		break;
	}

	return return_code;
}

/*
 * This function is to store the framebuffer into file.
 * It will handle the cases of chromaInterleave, or cropping,
 * or both.
 */
static void write_to_dst(struct decoder_info *dec,
			 struct mediaBuffer *vid_dst, int index)
{
	int height = (dec->picheight + 15) & ~15 ;
	int stride = dec->stride;
	int img_size;
	u8 *buf;
	struct frame_buf *pfb = NULL;

	pfb = dec->pfbpool[index];
	buf = (u8 *)(pfb->addrY + pfb->desc.virt_uaddr - pfb->desc.phy_addr);

	if (dec->color_space == YUV422P)
		img_size = stride * height * 2;
	else
		img_size = stride * height * 3 / 2;


	if (vid_dst->dataSource == FILE_SRC) {
		fwriten(vid_dst->fd, buf, img_size);
	}
	else {
		if (dec->format == MJPEG)
			vid_dst->colorSpace = YUV422P;
		else if (dec->format == H264AVC)
			vid_dst->colorSpace = NV12;
		vid_dst->dataSource = VPU_CODEC;
		vid_dst->bufOutSize = img_size;
		vid_dst->height = dec->picheight;
		vid_dst->width = dec->picwidth;
		vid_dst->imageHeight = dec->lastPicHeight;
		vid_dst->imageWidth = dec->lastPicWidth;
		vid_dst->vBufOut = (unsigned char *)buf;
		vid_dst->pBufOut = (unsigned char *)pfb->addrY;
	}

	return;
}
