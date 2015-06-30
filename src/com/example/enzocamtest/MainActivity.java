package com.example.enzocamtest;

import android.app.Activity;
import android.os.Bundle;

public class MainActivity extends Activity {
	private CamView mCamView;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		mCamView = new CamView(this);
		setContentView(mCamView);
	}
}
