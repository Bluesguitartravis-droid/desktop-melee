package dev.melee;

import org.libsdl.app.SDLActivity;

public class MeleeActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        /* libpng is linked statically since the aurora sync to 77326d45
         * (extern/aurora/extern/CMakeLists.txt, _USE_SHARED is off on
         * Android), so a clean build ships no libpng16.so and loading it
         * failed before the activity had a window. */
        return new String[] {
            "melee"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    public org.libsdl.app.SDLSurface createSDLSurface(android.content.Context context) {
        return new dev.encounter.aurora.AuroraSurface(context);
    }

    private static final int EXPORT_LOGS_REQUEST = 7124;

    public void exportLogs() {
        runOnUiThread(() -> {
            android.content.Intent intent = new android.content.Intent(
                android.content.Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(android.content.Intent.EXTRA_TITLE, "melee-pc-diagnostics.txt");
            try {
                startActivityForResult(intent, EXPORT_LOGS_REQUEST);
            } catch (android.content.ActivityNotFoundException e) {
                android.widget.Toast.makeText(this, "No document picker is available.",
                    android.widget.Toast.LENGTH_LONG).show();
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != EXPORT_LOGS_REQUEST || resultCode != RESULT_OK || data == null ||
                data.getData() == null) {
            return;
        }
        final android.net.Uri destination = data.getData();
        new Thread(() -> {
            String result = "Diagnostics saved. Attach this file to your issue.";
            Process capture = null;
            try (java.io.OutputStream output = getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) {
                    throw new java.io.IOException("The selected document could not be opened.");
                }
                String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
                String header = "Melee PC " + version + "\nDevice: " + android.os.Build.MANUFACTURER +
                    " " + android.os.Build.MODEL + "\nAndroid: " + android.os.Build.VERSION.RELEASE +
                    " (API " + android.os.Build.VERSION.SDK_INT + ")\nABI: " +
                    java.util.Arrays.toString(android.os.Build.SUPPORTED_ABIS) +
                    "\nRecent app logs (including previous launches still in the log buffer):\n\n";
                output.write(header.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                // Android permits reading this app's own UID without READ_LOGS.
                // Keep previous-process messages, and exclude other apps' logs.
                capture = new ProcessBuilder("logcat", "-d", "-v", "threadtime",
                    "--uid=" + android.os.Process.myUid(), "Aurora:V", "Melee:V", "melee:V",
                    "AndroidRuntime:V", "libc:V", "*:S").redirectErrorStream(true).start();
                try (java.io.InputStream input = capture.getInputStream()) {
                    byte[] buffer = new byte[8192];
                    int remaining = 4 * 1024 * 1024;
                    int count;
                    while (remaining > 0 && (count = input.read(buffer, 0,
                            Math.min(buffer.length, remaining))) != -1) {
                        output.write(buffer, 0, count);
                        remaining -= count;
                    }
                    if (remaining == 0) {
                        capture.destroy();
                    } else if (capture.waitFor() != 0) {
                        throw new java.io.IOException("Android could not read the app log buffer.");
                    }
                }
            } catch (Exception e) {
                result = "Could not export diagnostics: " + e.getMessage();
            } finally {
                if (capture != null) {
                    capture.destroy();
                }
            }
            final String message = result;
            runOnUiThread(() -> android.widget.Toast.makeText(this, message,
                android.widget.Toast.LENGTH_LONG).show());
        }, "melee-log-export").start();
    }

    private TouchOverlayView mTouchOverlay;
    private boolean mFullscreen = true;
    private boolean mDesktopOverlay;
    private boolean mDesktopWatch;
    private android.view.WindowManager mOverlayWindowManager;
    private android.view.View mOverlayRoot;
    private boolean mSystemOverlayAttached;
    private boolean mLauncherActive = true;
    private android.view.WindowManager.LayoutParams mOverlayParams;
    private android.view.View mStopControl;
    private boolean mOverlayHiddenForPicker;

    public boolean isDesktopOverlayEnabled() { return mDesktopOverlay; }
    public boolean isDesktopWatchMode() { return mDesktopWatch; }

    private void attachSystemOverlay() {
        if (!mDesktopOverlay || mSystemOverlayAttached || isFinishing()) {
            return;
        }
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.M ||
                !android.provider.Settings.canDrawOverlays(this)) {
            android.widget.Toast.makeText(this,
                "Desktop Melee overlay permission is not available; staying in foreground mode.",
                android.widget.Toast.LENGTH_LONG).show();
            mDesktopOverlay = false;
            return;
        }
        if (mLayout == null) {
            mDesktopOverlay = false;
            return;
        }
        try {
            android.view.ViewParent parent = mLayout.getParent();
            if (parent instanceof android.view.ViewGroup) {
                ((android.view.ViewGroup) parent).removeView(mLayout);
            }
            mLayout.setBackgroundColor(mLauncherActive ? android.graphics.Color.BLACK : android.graphics.Color.TRANSPARENT);
            mOverlayWindowManager =
                (android.view.WindowManager) getSystemService(android.content.Context.WINDOW_SERVICE);

            int flags = android.view.WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN |
                android.view.WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS |
                android.view.WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED |
                android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
            if (mDesktopWatch && !mLauncherActive) {
                flags |= android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                    android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            }

            android.view.WindowManager.LayoutParams lp =
                new android.view.WindowManager.LayoutParams(
                    android.view.WindowManager.LayoutParams.MATCH_PARENT,
                    android.view.WindowManager.LayoutParams.MATCH_PARENT,
                    android.view.WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                    flags,
                    android.graphics.PixelFormat.TRANSLUCENT);
            lp.gravity = android.view.Gravity.TOP | android.view.Gravity.START;
            lp.softInputMode = android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING;
            if (mDesktopWatch && !mLauncherActive) {
                float safeAlpha = 0.70f;
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                    android.hardware.input.InputManager input =
                        (android.hardware.input.InputManager) getSystemService(
                            android.content.Context.INPUT_SERVICE);
                    if (input != null) {
                        safeAlpha = Math.min(safeAlpha,
                            input.getMaximumObscuringOpacityForTouch() * 0.90f);
                    }
                }
                lp.alpha = safeAlpha;
            } else {
                lp.alpha = 1.0f;
            }
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                lp.layoutInDisplayCutoutMode =
                    android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            }
            mOverlayWindowManager.addView(mLayout, lp);
            mOverlayRoot = mLayout;
            mOverlayParams = lp;
            mSystemOverlayAttached = true;
            addEmergencyStopControl();
            com.desktopmelee.android.OverlayDiagnostics.record(this, "system overlay attached; launcher=" + mLauncherActive);
            if (!mDesktopWatch || mLauncherActive) {
                mLayout.setFocusableInTouchMode(true);
                mLayout.requestFocus();
                mLayout.postDelayed(() -> {
                    if (mSystemOverlayAttached && mOverlayRoot != null) {
                        mOverlayRoot.requestFocus();
                    }
                }, 350L);
            }

            // Keep controller/HID input alive after the Activity itself moves behind Home.
            try {
                org.libsdl.app.SDLActivity.nativeSetenv(
                    "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
            } catch (Throwable ignored) {}

            if (!mLauncherActive) sendMatchBehindHome();
        } catch (Throwable t) {
            android.util.Log.e("DesktopMelee", "Could not attach TYPE_APPLICATION_OVERLAY", t);
            com.desktopmelee.android.OverlayDiagnostics.record(this, "OVERLAY ATTACH FAILED: " + android.util.Log.getStackTraceString(t));
            detachSystemOverlay();
            mSystemOverlayAttached = false;
            mOverlayRoot = null;
            mOverlayWindowManager = null;
            mDesktopOverlay = false;
            try {
                setContentView(mLayout);
            } catch (Throwable ignored) {}
            android.widget.Toast.makeText(this,
                "System overlay failed; continuing in foreground mode.",
                android.widget.Toast.LENGTH_LONG).show();
        }
    }

    // This is a separate Android overlay window above the game surface. It still
    // receives taps when Watch mode makes the game overlay non-touchable.
    private void addEmergencyStopControl() {
        android.widget.Button button = new android.widget.Button(this);
        button.setText("STOP OVERLAY");
        button.setAllCaps(false);
        button.setTextSize(11f);
        button.setTextColor(android.graphics.Color.WHITE);
        button.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
            android.graphics.Color.rgb(165, 30, 30)));
        button.setOnClickListener(v -> stopOverlayNow("STOP OVERLAY button"));
        float density = getResources().getDisplayMetrics().density;
        android.view.WindowManager.LayoutParams params =
            new android.view.WindowManager.LayoutParams(
                Math.round(130 * density), Math.round(48 * density),
                android.view.WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                    android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                    android.view.WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                android.graphics.PixelFormat.TRANSLUCENT);
        params.gravity = android.view.Gravity.TOP | android.view.Gravity.RIGHT;
        params.x = Math.round(6 * density);
        params.y = Math.round(12 * density);
        mOverlayWindowManager.addView(button, params);
        mStopControl = button;
    }

    private void stopOverlayNow(String reason) {
        com.desktopmelee.android.OverlayDiagnostics.record(this, "STOP: " + reason);
        // Remove both independent windows first, even if the SDL renderer is hung.
        detachSystemOverlay();
        finishAndRemoveTask();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    public void onBackPressed() {
        if (mSystemOverlayAttached) stopOverlayNow("Back key");
        else super.onBackPressed();
    }

    private void updateOverlayState() {
        if (!mSystemOverlayAttached || mOverlayParams == null ||
                mOverlayWindowManager == null) return;
        mLayout.setBackgroundColor(mLauncherActive ? android.graphics.Color.BLACK :
            android.graphics.Color.TRANSPARENT);
        int baseFlags = mOverlayParams.flags &
            ~(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
              android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        if (mOverlayHiddenForPicker || (mDesktopWatch && !mLauncherActive)) {
            baseFlags |= android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        }
        mOverlayParams.flags = baseFlags;
        mOverlayParams.alpha = mOverlayHiddenForPicker ? 0f :
            (mDesktopWatch && !mLauncherActive ? 0.70f : 1f);
        if (mStopControl != null)
            mStopControl.setVisibility(mOverlayHiddenForPicker ?
                android.view.View.GONE : android.view.View.VISIBLE);
        try {
            mOverlayWindowManager.updateViewLayout(mOverlayRoot, mOverlayParams);
            if (!mOverlayHiddenForPicker && (mLauncherActive || !mDesktopWatch)) {
                mLayout.setFocusableInTouchMode(true);
                mLayout.requestFocus();
            }
        } catch (Throwable t) {
            com.desktopmelee.android.OverlayDiagnostics.record(this,
                "Overlay window update failed: " + android.util.Log.getStackTraceString(t));
        }
    }

    private void sendMatchBehindHome() {
        if (mLayout == null) return;
        mLayout.postDelayed(() -> {
            if (mSystemOverlayAttached && !mLauncherActive && !isFinishing()) {
                com.desktopmelee.android.OverlayDiagnostics.record(this,
                    "Match started: moving task behind Home");
                moveTaskToBack(true);
            }
        }, 400L);
    }

    private void detachSystemOverlay() {
        if (!mSystemOverlayAttached || mOverlayWindowManager == null || mOverlayRoot == null) {
            return;
        }
        if (mStopControl != null) {
            try { mOverlayWindowManager.removeViewImmediate(mStopControl); }
            catch (Throwable ignored) {}
            mStopControl = null;
        }
        try {
            mOverlayWindowManager.removeViewImmediate(mOverlayRoot);
        } catch (Throwable ignored) {}
        mSystemOverlayAttached = false;
        mOverlayRoot = null;
        mOverlayWindowManager = null;
        mOverlayParams = null;
    }

    private void applyDesktopWindowMode() {
        android.view.Window window = getWindow();
        if (window == null) return;
        if (mDesktopOverlay) {
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(mLauncherActive ? android.graphics.Color.BLACK : android.graphics.Color.TRANSPARENT));
            window.setDimAmount(0.0f);
            window.addFlags(android.view.WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
            window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            if (mDesktopWatch && !mLauncherActive) {
                window.addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                    android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            } else {
                window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                    android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            }
        } else {
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.BLACK));
            window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                android.view.WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        }
    }

    public void setFullscreen(final boolean fullscreen) {
        runOnUiThread(() -> {
            mFullscreen = fullscreen;
            applyImmersiveMode();
        });
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        android.content.Intent intent = getIntent();
        mDesktopOverlay = intent != null && intent.getBooleanExtra(
            com.desktopmelee.android.DesktopMeleeActivity.EXTRA_OVERLAY, false);
        mDesktopWatch = intent != null && intent.getBooleanExtra(
            com.desktopmelee.android.DesktopMeleeActivity.EXTRA_WATCH, false);
        int orientationMode = intent != null ? intent.getIntExtra(
            com.desktopmelee.android.DesktopMeleeActivity.EXTRA_ORIENTATION, 0) : 0;
        if (orientationMode == 1) {
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        } else if (orientationMode == 2) {
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
        } else {
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
        }
        super.onCreate(savedInstanceState);
        applyDesktopWindowMode();
        if (mBrokenLibraries) {
            return;
        }
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        applyImmersiveMode();

        mTouchOverlay = new TouchOverlayView(this);
        if (mLayout != null) {
            mLayout.addView(mTouchOverlay, new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            ));
        }

        if (mDesktopOverlay && mLayout != null) {
            mLayout.post(this::attachSystemOverlay);
        }

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.N) {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(android.content.Context.POWER_SERVICE);
            if (pm != null && pm.isSustainedPerformanceModeSupported()) {
                getWindow().setSustainedPerformanceMode(true);
            }
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = 60.0f;
            getWindow().setAttributes(lp);
        }
        /* Only pre-Android 13 can be granted READ/WRITE_EXTERNAL_STORAGE at
         * all; from API 33 they are not grantable and the request resolves
         * instantly, but the dialog still pauses and recreates the activity
         * while the graphics device is coming up, which killed the process at
         * startup on a Pixel 8. The disc is read from the app's own external
         * files dir, which needs no permission on any version. */
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M
                && android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 100);
            }
        }
    }

    /* Called from native (src/pc/android_compat.cpp) around pc_launcher_run:
     * the launcher needs every touch while it is on screen. */
    public void setLauncherActive(final boolean active) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mTouchOverlay != null) {
                    mTouchOverlay.setLauncherActive(active);
                }
            }
        });
    }

    public static native void nativeDisconnect();

    @Override
    protected void onStop() {
        if (isFinishing()) {
            try {
                nativeDisconnect();
            } catch (Throwable ignored) {}
        }
        super.onStop();
        if (mSystemOverlayAttached && !isFinishing()) {
            try {
                resumeNativeThread();
                org.libsdl.app.SDLActivity.nativeFocusChanged(true);
            } catch (Throwable t) {
                android.util.Log.w("DesktopMelee", "Could not resume SDL behind overlay", t);
            }
            try {
                if (mHIDDeviceManager != null) {
                    mHIDDeviceManager.setFrozen(false);
                }
            } catch (Throwable ignored) {}
        }
    }

    @Override
    protected void onDestroy() {
        detachSystemOverlay();
        try {
            nativeDisconnect();
        } catch (Throwable ignored) {}
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mSystemOverlayAttached) {
            try {
                if (mHIDDeviceManager != null) {
                    mHIDDeviceManager.setFrozen(false);
                }
                org.libsdl.app.SDLActivity.nativeFocusChanged(true);
            } catch (Throwable ignored) {}
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyDesktopWindowMode();
        applyImmersiveMode();
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = 60.0f;
            getWindow().setAttributes(lp);
        }
        if (mTouchOverlay != null) {
            mTouchOverlay.updateControllerState();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        // Insets/IMMERSIVE_STICKY handle swipes; SDL's delayed hide ignores our saved choice.
    }

    private void applyImmersiveMode() {
        if (mBrokenLibraries) {
            return;
        }
        android.view.Window window = getWindow();
        if (window == null) {
            return;
        }
        window.setFlags(mFullscreen ? android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN : 0,
            android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(!mFullscreen);
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                if (mFullscreen)
                    controller.hide(android.view.WindowInsets.Type.systemBars());
                else
                    controller.show(android.view.WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                    mFullscreen ? android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        : android.view.WindowInsetsController.BEHAVIOR_DEFAULT);
            }
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                android.view.WindowManager.LayoutParams lp = window.getAttributes();
                lp.layoutInDisplayCutoutMode =
                    mFullscreen ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                        : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
                window.setAttributes(lp);
            }
        } else {
            int flags = android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
            window.getDecorView().setSystemUiVisibility(mFullscreen ? flags
                : android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    protected String[] getArguments() {
        android.content.Intent intent = getIntent();
        if (intent != null) {
            String[] args = intent.getStringArrayExtra("args");
            if (args != null && args.length > 0) {
                return args;
            }
            String disc = intent.getStringExtra("disc");
            if (disc != null && !disc.isEmpty()) {
                return new String[] { "--dvd", disc };
            }
            android.net.Uri data = intent.getData();
            if (data != null) {
                try {
                    getContentResolver().takePersistableUriPermission(
                        data, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (Exception ignored) {
                }
                return new String[] { "--dvd", data.toString() };
            }
        }
        return new String[0];
    }

    public int[] getDesktopArenaGeometry() {
        android.graphics.Rect bounds;
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
        } else {
            android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
            getWindowManager().getDefaultDisplay().getRealMetrics(dm);
            bounds = new android.graphics.Rect(0, 0, dm.widthPixels, dm.heightPixels);
        }
        final int width = Math.max(1, bounds.width());
        final int height = Math.max(1, bounds.height());
        final boolean landscape = width >= height;
        android.content.SharedPreferences p =
            getSharedPreferences(com.desktopmelee.android.DesktopMeleeActivity.PREFS, MODE_PRIVATE);
        int marginPct = p.getInt(landscape ? "landscapeMargin" : "portraitMargin", landscape ? 3 : 6);
        int floorPct = p.getInt(landscape ? "landscapeFloor" : "portraitFloor", landscape ? 88 : 82);
        marginPct = Math.max(0, Math.min(25, marginPct));
        floorPct = Math.max(50, Math.min(98, floorPct));
        int margin = Math.round(width * (marginPct / 100.0f));
        int floor = Math.round(height * (floorPct / 100.0f));
        floor = Math.max(1, Math.min(height - 1, floor));
        int right = Math.max(margin + 1, width - margin);
        return new int[] {0, 0, width, height, margin, floor, right, height};
    }


}
