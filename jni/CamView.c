#include <jni.h>

#include <sys/time.h>

#include "enzo_codec.h"
#include "enzo_utils.h"
#include "g2d.h"
#include "CamView.h"

#include <android/bitmap.h>
#include <malloc.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define FPS			15

/* These are the control structures for the encoder and camera */
struct decoderInstance *mjpgDec;
struct cameraInstance *usbCam;

/* Control structures for the media buffers, which are used to
   pass data between sources (like a camera or file), and pass them
   to other components (like the VPU, a file, or a buffer) */
struct mediaBuffer *camData, *yuvData;

struct g2d_buf *y420_buf, *y422_buf, *rgb_buf;
struct g2d_surface y420_surf, y422_surf, rgb_surf;
void *g2d_handle;

JNIEXPORT void JNICALL Java_com_example_enzocamtest_CamView_loadNextFrame(JNIEnv* env,
		jobject thiz, jobject bitmap)
{
	AndroidBitmapInfo info;
	int result;

	if((result = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		err_msg("AndroidBitmap_getInfo() failed, error=%d", result);
		return;
	}

	if(info.format != ANDROID_BITMAP_FORMAT_RGB_565) {
		err_msg("Bitmap format is not RGBA_565 !");
		return;
	}

	char *colors;
	if((result = AndroidBitmap_lockPixels(env, bitmap, (void*)&colors)) < 0) {
		err_msg("AndroidBitmap_lockPixels() failed, error=%d", result);
	}

	//info_msg("Getting camera frame...\n");
	result = cameraGetFrame(usbCam, camData);
	if (result < 0) {
		err_msg("Could not get camera frame\n");
	}

	//info_msg("Decoding camera frame...\n");
	result = decoderDecodeFrame(mjpgDec, camData, yuvData);
	if (result < 0) {
		err_msg("Could not decode MJPG frame\n");
	}

	if(g2d_open(&g2d_handle)) {
		err_msg("Encoder: g2d_open fail.\n");
		return;
	}

	int y_size = info.width * info.height;

	y422_buf->buf_paddr = (unsigned char *)yuvData->pBufOut;
	y422_buf->buf_vaddr = (unsigned char *)yuvData->vBufOut;

	g2d_copy(g2d_handle, y420_buf, y422_buf, y_size *3/2);
	g2d_finish(g2d_handle);

	//info_msg("Converting frame to RGB565...\n");
	g2d_blit(g2d_handle, &y420_surf, &rgb_surf);
	g2d_finish(g2d_handle);
	g2d_close(g2d_handle);

	//info_msg("Copy RGB frame to bitmap...\n");
	memcpy(colors, rgb_buf->buf_vaddr, info.width * info.height * 2);

	AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT jint JNICALL Java_com_example_enzocamtest_CamView_startCamera(JNIEnv* env,
		jobject thiz, jstring deviceName, jint width, jint height)
{
	int ret = 0;
	const char* dev_name = (*env)->GetStringUTFChars(env, deviceName, 0);

	/* Initialize all the structures we will be using */
	mjpgDec = (struct decoderInstance *)calloc(1, sizeof(struct decoderInstance));
	usbCam = (struct cameraInstance  *)calloc(1, sizeof(struct cameraInstance));

	camData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));
	yuvData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));

	y422_buf = (struct g2d_buf *)calloc(1, sizeof(struct g2d_buf));

	/* Set properties for H264 AVC decoder */
	mjpgDec->type = MJPEG;

	/* Set properties for USB camera */
	usbCam->type = MJPEG;
	usbCam->width = width;
	usbCam->height = height;
	usbCam->fps = FPS;
	strcpy(usbCam->deviceName, dev_name);

	/* Init the VPU. This must be done before a codec can be used.
	   If this fails, we need to bail. */
	ret = vpuInit();
	if (ret < 0)
		return -1;

	if (cameraInit(usbCam) < 0)
		ret = -1;
	/* In order to init mjpg decoder, it must be supplied with bitstream
	   parse */
	ret = cameraGetFrame(usbCam, camData);
	if (ret < 0) {
		err_msg("Could not get camera frame\n");
		ret = -1;
	}
	if (decoderInit(mjpgDec, camData) < 0) {
		err_msg("Could not init MJPG decoder\n");
		ret = -1;
	}

	y420_buf = g2d_alloc(width * height * 2, 0);
	rgb_buf = g2d_alloc(width * height * 2, 0);

	rgb_surf.planes[0] = rgb_buf->buf_paddr;
	rgb_surf.left = 0;
	rgb_surf.top = 0;
	rgb_surf.right = width;
	rgb_surf.bottom = height;
	rgb_surf.stride = width;
	rgb_surf.width = width;
	rgb_surf.height = height;
	rgb_surf.rot = G2D_ROTATION_0;
	rgb_surf.format = G2D_RGB565;

	y420_surf.planes[0] = y420_buf->buf_paddr;
	y420_surf.planes[1] = y420_surf.planes[0] + width*height;
	y420_surf.planes[2] = y420_surf.planes[1] + width*height/4;
	y420_surf.left = 0;
	y420_surf.top = 0;
	y420_surf.right = width;
	y420_surf.bottom = height;
	y420_surf.stride = width;
	y420_surf.width  = width;
	y420_surf.height = height;
	y420_surf.rot    = G2D_ROTATION_0;
	y420_surf.format = G2D_I420;

	info_msg("Finished setting up JNI codec and camera!\n");

	return ret;
}

JNIEXPORT void JNICALL Java_com_example_enzocamtest_CamView_stopCamera(JNIEnv* env,
		jobject thiz)
{
	cameraDeinit(usbCam);
	decoderDeinit(mjpgDec);
	free(mjpgDec);
	free(usbCam);
	free(camData);
	free(yuvData);
	free(y422_buf);
	g2d_free(rgb_buf);
	g2d_free(y420_buf);
	vpuDeinit();
}

JNIEXPORT jboolean JNICALL Java_com_example_enzocamtest_CamView_cameraAttached(JNIEnv* env,
		jobject thiz)
{
	return 0;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	return JNI_VERSION_1_6;
}
