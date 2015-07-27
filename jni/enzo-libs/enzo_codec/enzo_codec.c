#include "enzo_codec.h"
#include "vpu_common.h"

#include <stdio.h>
#include <string.h>

int encoderInit(struct encoderInstance *encInst, struct mediaBuffer *enc_dst)
{
	struct encoder_info *enc = &encInst->enc;
	enc->enc_picwidth = encInst->width;
	enc->enc_picheight = encInst->height;
	enc->src_picwidth = encInst->width;
	enc->src_picheight = encInst->height;
	enc->enc_fps = encInst->fps;
	enc->enc_bit_rate = encInst->bitRate;
	enc->gop_size = encInst->gopSize;
	enc->color_space = encInst->colorSpace;

	if (strcmp(encInst->encoderName, "") == 0)
		strcpy(enc->encoder_name,"Encoder");
	else
		strcpy(enc->encoder_name,encInst->encoderName);

	if (vpu_encoder_init(enc, enc_dst) < 0)
		return -1;
	else
		return 0;
}

int encoderDeinit(struct encoderInstance *encInst)
{
	struct encoder_info *enc = &encInst->enc;
	vpu_encoder_deinit(enc);
	return 0;
}
 
int encoderEncodeFrame( struct encoderInstance *encInst,
			struct mediaBuffer *vid_src,
			struct mediaBuffer *enc_dst)
{
	struct encoder_info *enc = &encInst->enc;
	/* Set the dst media buffer properties to reflect
	   the type of encoding that is occuring. For now
	   it is fixed */
	enc_dst->dataType = H264AVC;
	enc_dst->dataSource = VPU_CODEC;
	enc_dst->colorSpace = NV12;
	enc->force_i_frame = encInst->forceIFrame;
	if (vpu_encoder_encode_frame(enc, vid_src, enc_dst) < 0)
		return -1;
	else {
		return 0;
	}
}

int decoderInit(struct decoderInstance *decInst, struct mediaBuffer *enc_src) {
	struct decoder_info *dec = &decInst->dec;
	dec->format = decInst->type;
	if (strcmp(decInst->decoderName, "") == 0)
		strcpy(dec->decoder_name, "Decoder");
	else
		strcpy(dec->decoder_name, decInst->decoderName);
	if (vpu_decoder_init(dec, enc_src) < 0)
		return -1;
	else
		return 0;
	return 0;
}
int decoderDeinit(struct decoderInstance *decInst){
	struct decoder_info *dec = &decInst->dec;
	vpu_decoder_deinit(dec);
	return 0;
}

int decoderDecodeFrame( struct decoderInstance *decInst,
			struct mediaBuffer *enc_src,
			struct mediaBuffer *vid_dst)
{
	struct decoder_info *dec = &decInst->dec;
	/* Set the dst media buffer properties to reflect
	   the type of encoding that is occuring. For now
	   it is fixed */
	vid_dst->dataType = RAW_VIDEO;
	vid_dst->dataSource = VPU_CODEC;
	return vpu_decoder_decode_frame(dec, enc_src, vid_dst);
}

int cameraInit(struct cameraInstance *camInst)
{
	struct camera_info *cam = &camInst->cam;
	cam->width = camInst->width;
	cam->height = camInst->height;
	cam->fps = camInst->fps;
	cam->type = camInst->type;
	strcpy(cam->dev_name, camInst->deviceName);
	if (v4l2_cameraInit(cam) < 0)
		return -1;
	else
		return 0;
}

int cameraDeinit(struct cameraInstance *camInst)
{
	struct camera_info *cam = &camInst->cam;
	if (v4l2_cameraDeinit(cam) < 0)
		return -1;
	else
		return 0;
}

int cameraGetFrame(struct cameraInstance *camInst,
		   struct mediaBuffer *cam_src)
{
	struct camera_info *cam = &camInst->cam;
	/* Set the dst media buffer properties to reflect
	   the type of encoding that is occuring */
	cam_src->dataType = camInst->type;
	cam_src->dataSource = V4L2_CAM;
	if (camInst->type == RAW_VIDEO)
		cam_src->dataType = RAW_VIDEO;
	else if (camInst->type == MJPEG)
		cam_src->dataType = MJPEG;
	if (v4l2_cameraGetFrame(cam, cam_src) < 0)
		return -1;
	else
		return 0;
}

int vpuInit(void)
{
	int err;
	vpu_versioninfo ver;

	err = vpu_Init(NULL);
	if (err) {
		err_msg("VPU Init Failure.\n");
		return -1;
	}

	err = vpu_GetVersionInfo(&ver);
	if (err) {
		err_msg("Cannot get version info, err:%d\n", err);
		vpu_UnInit();
		return -1;
	}

	info_msg("VPU firmware version: %d.%d.%d_r%d\n",
			ver.fw_major, ver.fw_minor,
			ver.fw_release, ver.fw_code);
	info_msg("VPU library version: %d.%d.%d\n", ver.lib_major,
			ver.lib_minor, ver.lib_release);

	info_msg("VPU: Init framebuffer pool\n");
	framebuf_init();

	info_msg("VPU was successfully initialized\n\n");

	return 0;
}

int vpuDeinit(void)
{
	vpu_UnInit();
	info_msg("VPU was deinitialized\n\n");
	return 0;
}

