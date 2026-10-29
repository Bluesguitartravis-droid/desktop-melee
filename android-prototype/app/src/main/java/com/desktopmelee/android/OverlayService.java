package com.desktopmelee.android;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ResultReceiver;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

public final class OverlayService extends Service {
    public static final String ACTION_START = "com.desktopmelee.android.START";
    public static final String ACTION_STOP = "com.desktopmelee.android.STOP";
    public static final String ACTION_WATCH = "com.desktopmelee.android.WATCH";
    public static final String EXTRA_MODE = "mode", MODE_WATCH = "watch", MODE_PLAY = "play";
    private static final String CHANNEL = "overlay";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private WindowManager manager;
    private FrameLayout root;
    private ArenaView arena;
    private boolean registered;
    private final BroadcastReceiver screenOff = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { if (Intent.ACTION_SCREEN_OFF.equals(i.getAction())) stopSelf(); }
    };
    public static boolean notificationsReady(Context c) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 33 && c.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) return false;
        NotificationChannel channel = nm.getNotificationChannel(CHANNEL);
        return nm.areNotificationsEnabled() && (channel == null || channel.getImportance() != NotificationManager.IMPORTANCE_NONE);
    }
    @Override public void onCreate() {
        super.onCreate();
        manager = getSystemService(WindowManager.class);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(screenOff, new IntentFilter(Intent.ACTION_SCREEN_OFF), Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(screenOff, new IntentFilter(Intent.ACTION_SCREEN_OFF));
        registered = true;
    }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) { stopSelf(); return START_NOT_STICKY; }
        if (!ACTION_START.equals(intent.getAction()) && !ACTION_WATCH.equals(intent.getAction())) { stopSelf(); return START_NOT_STICKY; }
        ResultReceiver reply = intent.getParcelableExtra("reply");
        boolean watch = ACTION_WATCH.equals(intent.getAction()) || MODE_WATCH.equals(intent.getStringExtra(EXTRA_MODE));
        try {
            if (!Settings.canDrawOverlays(this)) throw new SecurityException("Display over other apps permission is missing");
            if (!notificationsReady(this)) throw new SecurityException("Enable app notifications and the Overlay control channel");
            NotificationManager nm = getSystemService(NotificationManager.class);
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Overlay control", NotificationManager.IMPORTANCE_LOW));
            Notification notification = notification(watch);
            if (Build.VERSION.SDK_INT >= 34) startForeground(101, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else startForeground(101, notification);
            showOverlay(watch);
            getSharedPreferences("diagnostics", MODE_PRIVATE).edit().remove("last_error").apply();
            if (reply != null) reply.send(1, Bundle.EMPTY);
        } catch (RuntimeException e) {
            String error = e.getClass().getSimpleName() + ": " + e.getMessage();
            android.util.Log.e("DesktopMelee", "Overlay failed", e);
            getSharedPreferences("diagnostics", MODE_PRIVATE).edit().putString("last_error", error).apply();
            if (reply != null) { Bundle data = new Bundle(); data.putString("error", error); reply.send(0, data); }
            stopSelf();
        }
        return START_NOT_STICKY;
    }
    private Notification notification(boolean watch) {
        int immutable = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;
        PendingIntent stop = PendingIntent.getService(this, 1, new Intent(this, OverlayService.class).setAction(ACTION_STOP), immutable);
        PendingIntent observe = PendingIntent.getService(this, 2, new Intent(this, OverlayService.class).setAction(ACTION_WATCH), immutable);
        PendingIntent settings = PendingIntent.getActivity(this, 3, new Intent(this, MainActivity.class), immutable);
        return new Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Desktop Melee / overlay TEST")
            .setContentText(watch ? "Placeholder animation. Tap Stop to end." : "Controller test active. Home-screen touch is captured.")
            .setContentIntent(settings).setOngoing(true).setOnlyAlertOnce(true)
            .addAction(new Notification.Action.Builder(null, "Stop", stop).build())
            .addAction(new Notification.Action.Builder(null, "Watch", observe).build()).build();
    }
    private void watchMode() { onStartCommand(new Intent(this, OverlayService.class).setAction(ACTION_WATCH), 0, 0); }
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private void showOverlay(boolean watch) {
        removeOverlay();
        root = new FrameLayout(this);
        arena = new ArenaView(this, watch, this::watchMode);
        root.addView(arena, new FrameLayout.LayoutParams(-1, -1));
        if (!watch) {
            LinearLayout controls = new LinearLayout(this);
            Button observe = new Button(this); observe.setText("WATCH / release controls"); observe.setFocusable(false);
            observe.setOnClickListener(v -> watchMode()); controls.addView(observe);
            Button stop = new Button(this); stop.setText("STOP"); stop.setFocusable(false);
            stop.setOnClickListener(v -> stopSelf()); controls.addView(stop);
            FrameLayout.LayoutParams top = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.END);
            top.topMargin = dp(12); top.rightMargin = dp(12); root.addView(controls, top);
        }
        int flags = WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        if (watch) flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(-1, -1, WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, flags, PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING;
        float safeAlpha = 0.70f;
        if (Build.VERSION.SDK_INT >= 31) safeAlpha = Math.min(safeAlpha, getSystemService(InputManager.class).getMaximumObscuringOpacityForTouch() * 0.90f);
        params.alpha = watch ? safeAlpha : 1f;
        manager.addView(root, params);
        arena.begin();
        if (!watch) { ArenaView current = arena; current.requestFocus(); handler.postDelayed(() -> { if (arena == current) current.requestFocus(); }, 350); }
    }
    private void removeOverlay() {
        handler.removeCallbacksAndMessages(null);
        if (arena != null) { arena.end(); arena = null; }
        if (root != null) {
            if (root.isAttachedToWindow()) {
                try { manager.removeViewImmediate(root); }
                catch (IllegalArgumentException e) { android.util.Log.w("DesktopMelee", "Overlay already detached", e); }
            }
            root = null;
        }
    }
    @Override public void onTaskRemoved(Intent rootIntent) { stopSelf(); }
    @Override public void onDestroy() {
        removeOverlay();
        if (registered) { unregisterReceiver(screenOff); registered = false; }
        stopForeground(STOP_FOREGROUND_REMOVE); super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
