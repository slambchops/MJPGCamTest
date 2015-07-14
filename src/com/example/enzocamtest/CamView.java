package com.example.enzocamtest;

import java.io.File;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class CamView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
	private static String TAG = "EnzoCam";
	private static String deviceName = "/dev/video0";

	private Bitmap mLocalCamBitmap;
	private Bitmap mRemoteCamBitmap;
	private Canvas mCanvas;
	private SurfaceHolder mHolder;
	private Rect mLocalCamViewWindow;
	private Rect mRemoteCamViewWindow;
    private int mCamWidth = 1280;
    private int mCamHeight = 720;
    private int mPreviewWidth = 320;
    private int mPreviewHeight = 240;
    
    private boolean mRunning = true;
    
    private native int startCamera(String deviceName, int width, int height);
    private native void processCamera();
    private native boolean cameraAttached();
    private native void stopCamera();
    private native void loadNextFrame(Bitmap bitmap);

    static {
        System.loadLibrary("camview");
    }

    public CamView(Context context) {
        super(context);
        mHolder = getHolder();
        mHolder.addCallback(this);
        
        mLocalCamBitmap = Bitmap.createBitmap(mCamWidth, mCamHeight, Bitmap.Config.RGB_565);
        mRemoteCamBitmap = Bitmap.createBitmap(mCamWidth, mCamHeight, Bitmap.Config.RGB_565);
        mCanvas = new Canvas(mLocalCamBitmap);
        
        connect(deviceName, mCamWidth, mCamHeight);
    }
    
    public CamView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        mHolder = getHolder();
        mHolder.addCallback(this);
        
        mLocalCamBitmap = Bitmap.createBitmap(mCamWidth, mCamHeight, Bitmap.Config.RGB_565);
        mRemoteCamBitmap = Bitmap.createBitmap(mCamWidth, mCamHeight, Bitmap.Config.RGB_565);
        mCanvas = new Canvas(mLocalCamBitmap);
        
        connect(deviceName, mCamWidth, mCamHeight);
    }
    
    @Override
    public void run() {
    	
    	Log.d(TAG, "Started running loop!");
    	
    	while(mRunning) {
    	
	    	//loadNextFrame(mLocalCamBitmap);
	    	loadNextFrame(mRemoteCamBitmap);
	    	
	        mCanvas = mHolder.lockCanvas();
	        if(mCanvas != null) {
	        	mCanvas.drawBitmap(mLocalCamBitmap, null, mLocalCamViewWindow, null);
	        	mCanvas.drawBitmap(mRemoteCamBitmap, null, mRemoteCamViewWindow, null);
	            mHolder.unlockCanvasAndPost(mCanvas);
	        }
    	}
    	
    	Log.d(TAG, "Exiting running loop!");
    	
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int winWidth,
            int winHeight) {
        Log.d(TAG, "Surface changed!");
        Log.d(TAG, "Window Width = " + winWidth);
        Log.d(TAG, "Window Height = " + winHeight);
        
        int bottom, left, right, top;
        
        bottom = mPreviewHeight + 50;
        left = winWidth - mPreviewWidth - 50;
        right = winWidth - 50;
        top = 50;
        
        Log.d(TAG, "Preview:");
        Log.d(TAG, "left = " + left);
        Log.d(TAG, "top = " + top);
        Log.d(TAG, "right = " + right);
        Log.d(TAG, "bottom = " + bottom);
        
        mLocalCamViewWindow = new Rect(left, top, right, bottom);
        
        bottom = winHeight - 50;
        left = 50;
        right = winWidth - mPreviewWidth - 2*50;
        top = 50;
        
        Log.d(TAG, "Remote:");
        Log.d(TAG, "left = " + left);
        Log.d(TAG, "top = " + top);
        Log.d(TAG, "right = " + right);
        Log.d(TAG, "bottom = " + bottom);
        
        mRemoteCamViewWindow = new Rect(left, top, right, bottom);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    	Log.d(TAG, "Surface created!");
    	mRunning = true;
    	(new Thread(this)).start();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
    	mRunning = false;
    	stopCamera();
    	Log.d(TAG, "Camera closed!");
    }
    
    private void connect(String deviceName, int width, int height) {
        boolean deviceReady = true;

        File deviceFile = new File(deviceName);
        if(deviceFile.exists()) {
            if(!deviceFile.canRead()) {
                Log.d(TAG, "Insufficient permissions on " + deviceName +
                        " -- does the app have the CAMERA permission?");
                deviceReady = false;
            }
        } else {
            Log.w(TAG, deviceName + " does not exist");
            deviceReady = false;
        }

        if(deviceReady) {
            Log.i(TAG, "Preparing camera with device name " + deviceName);
            startCamera(deviceName, width, height);
        }
    }
}
