package com.desktopmelee.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

/** Four-slot diagnostic arena. Native bridge is loaded separately; no Melee engine/assets yet. */
public final class ArenaView extends View implements InputManager.InputDeviceListener {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final InputState input = new InputState();
    private final InputManager devices;
    private final boolean watch;
    private final Runnable release;
    private boolean running;
    private float x = .24f, height, velocity, elapsed;
    private final float[] cpuX = {.43f, .63f, .82f};
    private long previous, flashUntil;
    private int deviceId = -1;
    private String lastInput = "Waiting for gamepad input";
    private final Runnable loop = new Runnable() {
        @Override public void run() {
            if (!running) return;
            long now = SystemClock.uptimeMillis();
            float dt = Math.min(.05f, Math.max(0, (now - previous) / 1000f)); previous = now; elapsed += dt;
            if (watch) x = .35f + (float)Math.sin(elapsed * 1.7f) * .24f;
            else x = Math.max(.04f, Math.min(.96f, x + input.horizontal() * .65f * dt));
            for (int i = 0; i < cpuX.length; ++i) {
                float center = .42f + i * .20f;
                cpuX[i] = center + (float)Math.sin(elapsed * (1.05f + i * .17f) + i) * .075f;
            }
            if (height > 0 || velocity > 0) { velocity -= 1200 * dt; height = Math.max(0, height + velocity * dt); }
            invalidate(); postOnAnimation(this);
        }
    };
    ArenaView(Context context, boolean watch, Runnable release) {
        super(context); this.watch = watch; this.release = release; devices = context.getSystemService(InputManager.class);
        setFocusable(!watch); setFocusableInTouchMode(!watch);
        setContentDescription("Controller overlay test with four colored placeholders, not Melee");
    }
    private float dp(float n) { return n * getResources().getDisplayMetrics().density; }
    public void begin() { if (running) return; running = true; previous = SystemClock.uptimeMillis(); devices.registerInputDeviceListener(this, handler); postOnAnimation(loop); }
    public void end() { running = false; removeCallbacks(loop); devices.unregisterInputDeviceListener(this); input.reset(); }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), floor = getHeight() - dp(48), radius = dp(18);
        paint.setColor(Color.argb(200, 35, 190, 225)); canvas.drawRect(0, floor, w, floor + dp(3), paint);
        paint.setColor(SystemClock.uptimeMillis() < flashUntil ? Color.WHITE : Color.rgb(246, 198, 60));
        canvas.drawCircle(w * x, floor - radius - dp(height), radius, paint);
        int[] cpuColors = {Color.rgb(205,74,122), Color.rgb(92,184,92), Color.rgb(155,104,210)};
        for (int i=0;i<cpuX.length;++i) {
            paint.setColor(cpuColors[i]);
            canvas.drawCircle(w * cpuX[i], floor - radius, radius, paint);
        }
        paint.setColor(Color.argb(180, 15, 20, 30)); canvas.drawRect(dp(10), dp(68), Math.min(w - dp(10), dp(620)), dp(170), paint);
        paint.setColor(Color.WHITE); paint.setTextSize(dp(14));
        canvas.drawText(watch ? "WATCH: placeholder animation / no input capture" : "PLAY: stick or D-pad / A jump / X flash / B or Back = Watch", dp(18), dp(91), paint);
        canvas.drawText("4 SLOTS + NATIVE C++ BRIDGE - Melee engine not integrated yet", dp(18), dp(116), paint);
        canvas.drawText(watch ? "Stop from the app or notification" : "Focus: " + hasWindowFocus() + " | " + lastInput, dp(18), dp(145), paint);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (!watch && (event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.isFromSource(InputDevice.SOURCE_GAMEPAD)) && event.getAction() == MotionEvent.ACTION_MOVE) {
            float flat = .12f;
            InputDevice device = event.getDevice();
            if (device != null) { InputDevice.MotionRange range = device.getMotionRange(MotionEvent.AXIS_X, event.getSource()); if (range != null) flat = Math.max(flat, range.getFlat()); }
            input.axes(event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_HAT_X), flat);
            deviceId = event.getDeviceId(); lastInput = "device " + deviceId + " axis " + Math.round(input.horizontal() * 100) + "%";
            return true;
        }
        return super.onGenericMotionEvent(event);
    }
    @Override public boolean onKeyDown(int key, KeyEvent event) {
        if (watch) return false;
        if (key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_BUTTON_B) { if (event.getRepeatCount() == 0) post(release); return true; }
        if (key == KeyEvent.KEYCODE_DPAD_LEFT) input.left(true);
        else if (key == KeyEvent.KEYCODE_DPAD_RIGHT) input.right(true);
        else if (key == KeyEvent.KEYCODE_BUTTON_A) { if (event.getRepeatCount() == 0 && height == 0) velocity = 440; }
        else if (key == KeyEvent.KEYCODE_BUTTON_X) flashUntil = SystemClock.uptimeMillis() + 180;
        else return super.onKeyDown(key, event);
        deviceId = event.getDeviceId(); lastInput = "device " + deviceId + " / " + KeyEvent.keyCodeToString(key); return true;
    }
    @Override public boolean onKeyUp(int key, KeyEvent event) {
        if (watch) return false;
        if (key == KeyEvent.KEYCODE_DPAD_LEFT) { input.left(false); return true; }
        if (key == KeyEvent.KEYCODE_DPAD_RIGHT) { input.right(false); return true; }
        if (key == KeyEvent.KEYCODE_BUTTON_A || key == KeyEvent.KEYCODE_BUTTON_X || key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BACK) return true;
        return super.onKeyUp(key, event);
    }
    @Override public boolean onTouchEvent(MotionEvent event) { if (!watch) { requestFocus(); if (event.getAction() == MotionEvent.ACTION_UP) performClick(); return true; } return false; }
    @Override public boolean performClick() { super.performClick(); return true; }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (!focus) input.reset(); }
    @Override public void onInputDeviceAdded(int id) { }
    @Override public void onInputDeviceChanged(int id) { if (id == deviceId) input.reset(); }
    @Override public void onInputDeviceRemoved(int id) { if (id == deviceId) { input.reset(); lastInput = "Controller disconnected"; deviceId = -1; } }
}
