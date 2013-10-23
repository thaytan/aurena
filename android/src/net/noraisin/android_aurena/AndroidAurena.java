package net.noraisin.android_aurena;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.TimeZone;

import com.gstreamer.GStreamer;

import android.app.Activity;
import android.content.Context;
import android.util.Log;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import android.net.nsd.NsdServiceInfo;
import android.net.nsd.NsdManager;

public class AndroidAurena extends Activity implements SurfaceHolder.Callback {
    private static native boolean classInit();
    private native void nativeInit();
    private native void nativeFinalize();
    private native void nativePlay(String server);
    private native void nativePause();
    private native void nativeSurfaceInit(Object surface);
    private native void nativeSurfaceFinalize();
    private long native_custom_data;

    private int position;
    private int duration;
    private PowerManager.WakeLock wake_lock;

    /* mDNS members */
    NsdManager mNsdManager;
    NsdManager.ResolveListener mResolveListener;
    NsdManager.DiscoveryListener mDiscoveryListener;
    NsdServiceInfo mService;

    private static final String SERVICE_TYPE = "_aurena._tcp.";
    private static final String TAG = "GStreamer";

    /* Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        mNsdManager = (NsdManager) getSystemService(Context.NSD_SERVICE);
        initializeDiscoveryListener();
        initializeResolveListener();

        try {
        GStreamer.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish(); 
            return;
        }

        setContentView(R.layout.main);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wake_lock = pm.newWakeLock(PowerManager.FULL_WAKE_LOCK, "Android Aurena");
        wake_lock.setReferenceCounted(false);

        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);
	
        nativeInit();

        wake_lock.acquire();
    }

    protected void onDestroy() {
        nativeFinalize();
        if (wake_lock.isHeld())
            wake_lock.release();
        super.onDestroy();
    }

    /* Called from native code */
    private void setMessage(final String message) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_message);
        runOnUiThread (new Runnable() {
          public void run() {
            /* Disable until we have got rid of non-fatal errors */
            /*tv.setText(message);*/
          }
        });
    }
    
    /* Called from native code */
    private void onGStreamerInitialized () {
        mNsdManager.discoverServices(
                SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, mDiscoveryListener);
    }

    /* The text widget acts as an slave for the seek bar, so it reflects what the seek bar shows, whether
     * it is an actual pipeline position or the position the user is currently dragging to.
     */
    private void updateTimeWidget () {
        final TextView tv = (TextView) this.findViewById(R.id.textview_time);

        SimpleDateFormat df = new SimpleDateFormat("HH:mm:ss");
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        final String message = df.format(new Date (position)) + " / " + df.format(new Date (duration));
        tv.setText(message);        
    }

    /* Called from native code */
    private void setCurrentPosition(final int position, final int duration) {
        this.position = position;
        this.duration = duration;

        runOnUiThread (new Runnable() {
          public void run() {
            updateTimeWidget();
          }
        });
    }

    /* Called from native code */
    private void setCurrentState (int state) {
        Log.d (TAG, "State has changed to " + state);
        switch (state) {
        case 1:
            setMessage ("NULL");
            break;
        case 2:
            setMessage ("READY");
            break;
        case 3:
            setMessage ("PAUSED");
            break;
        case 4:
            setMessage ("PLAYING");
            break;
        }
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("android-aurena");
        classInit();
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        Log.d(TAG, "Surface changed to format " + format + " width "
                + width + " height " + height);
        nativeSurfaceInit (holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d(TAG, "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d(TAG, "Surface destroyed");
        nativeSurfaceFinalize ();
    }

    // mDNS Discovery
    public void initializeDiscoveryListener() {
        mDiscoveryListener = new NsdManager.DiscoveryListener() {

          @Override
          public void onDiscoveryStarted(String regType) {
              Log.d(TAG, "Service discovery started");
          }

          @Override
          public void onServiceFound(NsdServiceInfo service) {
              Log.d(TAG, "Service discovery success" + service);
              if (!service.getServiceType().equals(SERVICE_TYPE)) {
                  Log.d(TAG, "Unknown Service Type: " + service.getServiceType());
              } else {
                  mNsdManager.resolveService(service, mResolveListener);
              }
          }

          @Override
          public void onServiceLost(NsdServiceInfo service) {
              Log.e(TAG, "service lost" + service);
              if (mService == service) {
                  mService = null;
              }
          }

          @Override
          public void onDiscoveryStopped(String serviceType) {
              Log.i(TAG, "Discovery stopped: " + serviceType);
          }

          @Override
          public void onStartDiscoveryFailed(String serviceType, int errorCode) {
              Log.e(TAG, "Discovery failed: Error code:" + errorCode);
              mNsdManager.stopServiceDiscovery(this);
          }

          @Override
          public void onStopDiscoveryFailed(String serviceType, int errorCode) {
              Log.e(TAG, "Discovery failed: Error code:" + errorCode);
              mNsdManager.stopServiceDiscovery(this);
          }
        };
    }

    public void initializeResolveListener() {
        mResolveListener = new NsdManager.ResolveListener() {

          @Override
          public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
              Log.e(TAG, "Resolve failed" + errorCode);
          }

          @Override
          public void onServiceResolved(NsdServiceInfo serviceInfo) {
              Log.e(TAG, "Resolve Succeeded. " + serviceInfo);

              mService = serviceInfo;
              String server = mService.getHost().getHostAddress() + ":" + mService.getPort();

              Log.d(TAG, "Connecting to server: " + server);
              nativePause ();
              nativePlay (server);
          }
        };
    }
}
