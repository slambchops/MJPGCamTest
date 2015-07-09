#ifndef ENZO_CODEC_H
#define ENZO_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "enzo_utils.h"
#include "vpu_decode.h"
#include "vpu_encode.h"
#include "v4l2_camera.h"

/* This structure is used to control and preserve the context
   of an encoder session. Anytime an encoder function is called,
   it must be provided with a valid encoderInstance structure. */
struct encoderInstance {
	int type;	/* Type of encoder; H264AVC, MJPEG, etc */
	int width;	/* Width the encoder is expecting of the
			   input data. This will also be the width
			   of the encoded data */
	int height;	/* Height the encoder is expecting of the
			   input data. This will also be the height
			   of the encoded data */
	int fps;	/* Framerate of the encoded data. Note that 
			   it is the responsibility of the user to 
			   ensure that the frames provided to the 
			   encoder are at the proper frame rate. For
			   instance, if frames are read from a v4l2
			   camera and the driver does not have a
			   framerate limit, then the frames will be
			   captured at some frequency that may be
			   different than what is desired. The encoder
			   will still associate the video with the
			   framerate that is specified here, however. */
	int bitRate;
	int gopSize;
	int forceIFrame;/* If this value is 0, the picture type is determined
			   by the VPU according to the various parameters such
			   as encoded frame number and GOP size. If this value
			   is 1, the frame is encoded as an I-picture
			   regardless of the frame number or GOP size and
			   I-picture period calculation is reset to the initial
			   state. For H.264 mode, the picture is encoded as an
			   Instantaneous Decoding Refresh (IDR) picture. */
	int colorSpace;	/* Color space of the data to be encoded. */
	
	struct encoder_info enc; /* Structure that contains in-depth
				    settings for encoder. It should
				    normally not be modified by the 
				    user. */
};

/* This structure is used to control and preserve the context
   of a decoder session. Anytime an encoder function is called,
   it must be provided with a valid decoderInstance structure. */
struct decoderInstance {
	int type;	/* Type of decoder; H264AVC, MJPEG, etc */
	int width;	/* Width the encoder is expecting of the
			   input data. This will also be the width
			   of the decoded data */
	int height;	/* Height the decoder is expecting of the
			   input data. This will also be the height
			   of the decoded data */
	int fps;	/* Framerate of the decoded data */
	
	struct decoder_info dec; /* Structure that contains in-depth
				    settings for decoder. It should
				    normally not be modified by the 
				    user. */
};

/* This structure is used to control and preserve the context
   of a camera. Anytime a camera function is called, it must be
   provided with a valid cameraInstance structure. */
struct cameraInstance {
	int type;	/* Type of data the camera will provide. For
			   example: raw video, MJPEG, H264, etc. */
	int width;	/* Width of the video frame the camera will
			   provide. */
	int height;	/* Height of the video frame the camera will
			   provide. */
	int fps;	/* Frame rate of the video the camera provides.
			   Note that right now this has no effect. */
	int colorSpace; /* Color space of the data that the camera is
			   providing. If the camera is providing
			   encoded data, this doesn't really need to
			   be set.*/
	char deviceName[12]; /* The /dev/videoX name of the camera.
				This value is a c string. */
	
	struct camera_info cam;	/* Structure that contains in-depth
				   settings for camera. It should
				   normally not be modified by the 
				   user. */
};

/* This function initializes an encoder with the parameters
   defined in the encoderInstance structure. It returns the encode headers
   to the mediaBuffer.

   Return: 0 = success, -1 = failure */
int encoderInit(struct encoderInstance *encInst, struct mediaBuffer *enc_dst);
/* This function deinitializes an encoder session
   defined in the encoderInstance structure.
 
   Return: 0 = success, -1 = failure */
int encoderDeinit(struct encoderInstance *encInst);
/* Encodes one frame of data from a video source. It needs to
   be given an encoderInstance to provide context for the 
   encode session. A video source mediaBuffer and a destination
   mediaBuffer must also be provided. Encoded data will be 
   pointed to in the destination mediaBuffer. 

   Return: 0 = success, -1 = failure */
int encoderEncodeFrame( struct encoderInstance *encInst,
			struct mediaBuffer *vid_src,
			struct mediaBuffer *enc_dst);

/* This function initializes a decoder with the parameters
   defined in the decoderInstance structure. It must be passed
   encoded data that contains headers that will be parsed.

   Return: 0 = success, -1 = failure */
int decoderInit(struct decoderInstance *decInst,
		struct mediaBuffer *enc_src);
/* This function deinitializes a decoder session
   defined in the decoderInstance structure.
 
   Return: 0 = success, -1 = failure */
int decoderDeinit(struct decoderInstance *decInst);
/* 
   Return: 0 = success, -1 = failure */
int decoderDecodeFrame( struct decoderInstance *decInst,
			struct mediaBuffer *enc_src,
			struct mediaBuffer *vid_dst);

/* This function initializes a camera with the parameters
   defined in the cameraInstance structure. 

   Return: 0 = success, -1 = failure */
int cameraInit(struct cameraInstance *camInst);
/* This function deinitializes the camera device
   defined in the cameraInstance structure. 

   Return: 0 = success, -1 = failure */
int cameraDeinit(struct cameraInstance *camInst);
/* This function initializes retrieves a frame of data
   from the camera device associated with a certain
   cameraInstance structure. The output frame will be
   pointed to by the mediaBuffer that is passed. 

   Return: 0 = success, -1 = failure */
int cameraGetFrame(struct cameraInstance *camInst,
		   struct mediaBuffer *cam_src);
/* This function initializes the video processing unit
   that contains the video codecs on Enzo. It must be 
   called before any encode/decode session can be started.

   Return: 0 = success, -1 = failure */
int vpuInit(void);
/* This function deinitializes the video processing unit.

   Return: 0 = success, -1 = failure */
int vpuDeinit(void);

#ifdef __cplusplus
}
#endif

#endif
