/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_example_enzocamtest_CamView */

#include "util.h"

static int DEVICE_DESCRIPTOR = -1;
int* RGB_BUFFER = NULL;
int* Y_BUFFER = NULL;

/*
 * Class:     com_example_enzocamtest_CamView
 * Method:    startCamera
 * Signature: (Ljava/lang/String;II)I
 */
JNIEXPORT jint JNICALL Java_com_example_enzocamtest_CamView_startCamera
  (JNIEnv *, jobject, jstring, jint, jint);

/*
 * Class:     com_example_enzocamtest_CamView
 * Method:    processCamera
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_example_enzocamtest_CamView_processCamera
  (JNIEnv *, jobject);

/*
 * Class:     com_example_enzocamtest_CamView
 * Method:    cameraAttached
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_example_enzocamtest_CamView_cameraAttached
  (JNIEnv *, jobject);

/*
 * Class:     com_example_enzocamtest_CamView
 * Method:    stopCamera
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_example_enzocamtest_CamView_stopCamera
  (JNIEnv *, jobject);

/*
 * Class:     com_example_enzocamtest_CamView
 * Method:    loadNextFrame
 * Signature: (Landroid/graphics/Bitmap;)V
 */
JNIEXPORT void JNICALL Java_com_example_enzocamtest_CamView_loadNextFrame
  (JNIEnv *, jobject, jobject);
