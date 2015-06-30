#ifndef CAMERA_H
#define CAMERA_H

#include "enzo_utils.h"

#include <linux/videodev2.h>

struct buf_info {
	unsigned int length;
	unsigned char *start;
	size_t offset;
};

/*
 * V4L2 capture device structure declaration
 */
struct camera_info {
	int type;
	int fd;
	enum v4l2_memory memory_mode;
	int num_buffers;
	int width;
	int height;
	int fps;
	char dev_name[12];
	char name[10];

	struct v4l2_buffer buf;
	struct v4l2_format fmt;
	struct buf_info *buffers;
};

int v4l2_cameraDeinit(struct camera_info *camera);
int v4l2_cameraGetFrame(struct camera_info *camera, struct mediaBuffer *cam_src);
int v4l2_cameraInit(struct camera_info *camera);


#endif // CAMERA_H
