#include "v4l2_camera.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/*
 * V4L2 capture device initialization
 */
static int v4l2_init_device(struct camera_info *device)
{
	int ret, i, j;
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer buf;
	struct v4l2_format temp_fmt;
	struct v4l2_capability capability;
	struct v4l2_streamparm param;
	struct buf_info *temp_buffers;

	/* Open the capture device */
	device->fd = open((const char *) device->dev_name, O_RDWR);
	if (device->fd <= 0) {
		err_msg("Cannot open %s device\n", device->dev_name);
		return -1;
	}

	info_msg("%s: Opened device\n", device->name);

	/* Check if the device is capable of streaming */
	if (ioctl(device->fd, VIDIOC_QUERYCAP, &capability) < 0) {
		err_msg("%s: VIDIOC_QUERYCAP failed", device->name);
		goto ERROR;
	}

	if (capability.capabilities & V4L2_CAP_STREAMING) {
		info_msg("%s: Capable of streaming\n", device->name);
	}
	else {
		err_msg("%s: Not capable of streaming\n", device->name);
		goto ERROR;
	}

	memset(&param, 0, sizeof(param));
	param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	param.parm.capture.timeperframe.numerator   = 1;
	param.parm.capture.timeperframe.denominator = device->fps;
	ret = ioctl(device->fd, VIDIOC_S_PARM, &param);
	if (ret < 0) {
		err_msg("%s: Could not set FPS\n", device->name);
		goto ERROR;
	} else
		info_msg("%s: FPS set to %d\n", device->name, device->fps);

	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	temp_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (device->type == RAW_VIDEO) {
		info_msg("%s: YUYV format requested\n", device->name);
		temp_fmt.fmt.pix.width = device->width;
		temp_fmt.fmt.pix.height = device->height;
		temp_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	} else if (device->type == MJPEG) {
		info_msg("%s: MJPEG format requested\n", device->name);
		temp_fmt.fmt.pix.width = device->width;
		temp_fmt.fmt.pix.height = device->height;
		temp_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		temp_fmt.fmt.pix.width = device->width & 0xFFFFFFF8;
		temp_fmt.fmt.pix.height = device->height & 0xFFFFFFF8;
		temp_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		temp_fmt.fmt.pix.priv = 0;
		temp_fmt.fmt.pix.sizeimage = 0;
		temp_fmt.fmt.pix.bytesperline = 0;
	} else {
		info_msg("%s: Invalid format requested\n", device->name);
		goto ERROR;
	}

	info_msg("%s: Width = %d, Height = %d\n",
		 device->name, device->width, device->height);

	ret = ioctl(device->fd, VIDIOC_S_FMT, &temp_fmt);
	if (ret < 0) {
		err_msg("%s: VIDIOC_S_FMT failed", device->name);
		goto ERROR;
	}

	device->fmt = temp_fmt;

	reqbuf.count = device->num_buffers;
	reqbuf.memory = device->memory_mode;
	ret = ioctl(device->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0) {
		err_msg("%s: Cannot allocate memory", device->name);
		goto ERROR;
	}
	device->num_buffers = reqbuf.count;
	info_msg("%s: Number of requested buffers = %u\n", device->name,
		device->num_buffers);

	temp_buffers = (struct buf_info *) malloc(sizeof(struct buf_info) *
		device->num_buffers);
	if (!temp_buffers) {
		err_msg("Cannot allocate memory\n");
		goto ERROR;
	}

	for (i = 0; i < device->num_buffers; i++) {
		buf.type = reqbuf.type;
		buf.index = i;
		buf.memory = reqbuf.memory;
		ret = ioctl(device->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			err_msg("%s: VIDIOC_QUERYCAP failed", device->name);
			device->num_buffers = i;
			goto ERROR1;
			return -1;
		}

		temp_buffers[i].offset = buf.m.offset;
		temp_buffers[i].length = buf.length;

		temp_buffers[i].start = (unsigned char*)mmap(NULL, buf.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, device->fd,
			buf.m.offset);
		if (temp_buffers[i].start == MAP_FAILED) {
			err_msg("Cannot mmap = %d buffer\n", i);
			device->num_buffers = i;
			goto ERROR1;
		}
	}

	device->buffers = temp_buffers;

	return 0;

ERROR1:
	for (j = 0; j < device->num_buffers; j++)
		munmap(temp_buffers[j].start,
			temp_buffers[j].length);

	free(temp_buffers);
ERROR:
	close(device->fd);
	device->fd = -1;

	err_msg("%s: Could not init device\n", device->name);

	return -1;
}

static void v4l2_exit_device(struct camera_info *device)
{
	int i;

	for (i = 0; i < device->num_buffers; i++) {
		munmap(device->buffers[i].start,
			device->buffers[i].length);
	}

	free(device->buffers);
	close(device->fd);
	device->fd = -1;

	return;
}


/*
 * Enable streaming for V4L2 capture device
 */
static int v4l2_stream_on(struct camera_info *device)
{
	int a, i, ret;

	for (i = 0; i < device->num_buffers; ++i) {
		struct v4l2_buffer buf;

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		ret = ioctl(device->fd, VIDIOC_QBUF, &buf);
		if (ret < 0) {
			err_msg("%s: VIDIOC_QBUF failed", device->name);
			device->num_buffers = i;
			return -1;
		}
	}

	device->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	device->buf.index = 0;
	device->buf.memory = device->memory_mode;

	a = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(device->fd, VIDIOC_STREAMON, &a);
	if (ret < 0) {
		err_msg("%s: VIDIOC_STREAMON failed", device->name);
		return -1;
	}
	info_msg("%s: Stream on\n", device->name);

	return 0;
}

/*
 * Disable streaming for V4L2 capture device
 */
static int v4l2_stream_off(struct camera_info *device)
{
	int a, ret;

	a = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(device->fd, VIDIOC_STREAMOFF, &a);
	if (ret < 0) {
		err_msg("%s: VIDIOC_STREAMOFF failed", device->name);
		return -1;
	}
	info_msg("%s: Stream off\n", device->name);

	return 0;
}

/*
 * Queue V4L2 buffer
 */
static int v4l2_queue_buffer(struct camera_info *device)
{
	int ret;
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(device->fd, &fds);

	/* Timeout. */
	tv.tv_sec = 10;
	tv.tv_usec = 0;

	ret = select(device->fd + 1, &fds, NULL, NULL, &tv);

	if (-1 == ret) {
		if (EINTR == errno) {
			err_msg("%s: QBBUF select failed\n", device->name);
			return -1;
		}
	}

	if (0 == ret) {
		err_msg("%s: QBUF select timeout\n", device->name);
		return -1;
	}

	/* Queue buffer for the v4l2 capture device */
	ret = ioctl(device->fd, VIDIOC_QBUF,
	&device->buf);
	if (ret < 0) {
		err_msg("%s: VIDIOC_QBUF failed\n", device->name);
		return -1;
	}

	return 0;
}

/*
 * DeQueue V4L2 buffer
 */
static int v4l2_dequeue_buffer(struct camera_info *device)
{
	int ret;
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(device->fd, &fds);

	/* Timeout. */
	tv.tv_sec = 10;
	tv.tv_usec = 0;

	ret = select(device->fd + 1, &fds, NULL, NULL, &tv);

	if (-1 == ret) {
		if (EINTR == errno) {
			err_msg("%s: DQBUF select failed\n", device->name);
			return -1;
		}
	}

	if (0 == ret) {
		err_msg("%s: DQBUF select timeout\n", device->name);
		return -1;
	}

	/* Dequeue buffer for the v4l2 capture device */
	ret = ioctl(device->fd, VIDIOC_DQBUF,
	&device->buf);
	if (ret < 0) {
		err_msg("%s: VIDIOC_DQBUF failed", device->name);
		return -1;
	}

	return 0;
}

/*
 * Initializes camera for streaming
 */
int v4l2_cameraInit(struct camera_info *camera)
{
	/* Declare properties for camera */
	camera->memory_mode = V4L2_MEMORY_MMAP;
	camera->num_buffers = 3;
	strcpy(camera->name,"USB Cam");
	camera->buffers = NULL;
		
	/* Initialize the v4l2 capture devices */
	if (v4l2_init_device(camera) < 0)
		return -1;

	/* Enable streaming for the v4l2 capture devices */
	if (v4l2_stream_on(camera) < 0)
		goto Error;

	/* Request a capture buffer from the driver that can be copied
	 * to framebuffer */
	v4l2_dequeue_buffer(camera);

	info_msg("%s: Init done successfully\n\n", camera->name);

	return 0;

Error:
	v4l2_cameraDeinit(camera);
	return -1;
}

/*
 * Closes down the camera
 */
int v4l2_cameraDeinit(struct camera_info *camera)
{
	/* If the fd is bad, then init must have not completed
	   and the cleanup would have occured in the init process */

	if (camera->fd > 0) {
		v4l2_stream_off(camera);
		v4l2_exit_device(camera);
	}

	info_msg("%s: camera was deinitialized\n\n", camera->name);

	return 0;
}

/*
 * Capture v4l2 frame
 */
int v4l2_cameraGetFrame(struct camera_info *camera, struct mediaBuffer *cam_src)
{
	unsigned int index;

	/* Give the buffer back to the driver so it can be filled again */
	v4l2_queue_buffer(camera);

	/* Request a capture buffer from the driver that can be copied
	 * to framebuffer */
	v4l2_dequeue_buffer(camera);

	index = camera->buf.index;

	cam_src->bufOutSize = camera->buf.bytesused;
	cam_src->vBufOut = camera->buffers[index].start;
	cam_src->pBufOut = NULL;
	cam_src->height = camera->height;
	cam_src->width = camera->width;
	cam_src->imageHeight = camera->height;
	cam_src->imageWidth = camera->width;

	if (camera->type == RAW_VIDEO)
		cam_src->colorSpace = YUYV;

	return 0;
}
