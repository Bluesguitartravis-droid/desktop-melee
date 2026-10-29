package com.desktopmelee.android;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ResultReceiver;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private TextView status;
    private boolean launching;
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(20), dp(20), dp(24));
        scroll.addView(root);
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            Insets bars = insets.getSystemWindowInsets();
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        TextView title = new TextView(this);
        title.setText("Desktop Melee / Android overlay test 0.0.1");
        title.setTextSize(22); root.addView(title);
        TextView details = new TextView(this);
        details.setText("Two colored placeholders test transparency and gamepad input. This is NOT Melee gameplay; no disc is needed.\n\nWatch: home-screen touch-through test.\nPlay: the overlay takes input focus; icons underneath are not clickable. Stick / D-pad moves yellow, A jumps, X flashes. Back or B returns to Watch.\n\nUse STOP, the notification, or this app to end the overlay. It also stops when the screen turns off. No root, accessibility service, Internet or storage access.");
        details.setPadding(0, dp(16), 0, dp(16)); root.addView(details);
        status = new TextView(this); root.addView(status);
        add(root, "1. Allow display over other apps", v -> open(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName()))));
        add(root, "2. Allow notification / Stop control", v -> notificationPermission());
        add(root, "Watch demo", v -> start(OverlayService.MODE_WATCH));
        add(root, "Play demo with controller", v -> start(OverlayService.MODE_PLAY));
        add(root, "STOP overlay", v -> {
            stopService(new Intent(this, OverlayService.class));
            status.setText("Overlay stopped.");
        });
        setContentView(scroll);
    }
    private void add(LinearLayout parent, String label, View.OnClickListener action) {
        Button button = new Button(this); button.setText(label); button.setOnClickListener(action); parent.addView(button);
    }
    private void open(Intent intent) {
        stopService(new Intent(this, OverlayService.class));
        try { startActivity(intent); }
        catch (RuntimeException e) { status.setText("Could not open settings: " + e.getClass().getSimpleName()); }
    }
    private void notificationPermission() {
        stopService(new Intent(this, OverlayService.class));
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1);
        } else {
            open(new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName()));
        }
    }
    @Override protected void onResume() {
        super.onResume();
        if (status != null) {
            String error = getSharedPreferences("diagnostics", MODE_PRIVATE).getString("last_error", "");
            status.setText(error.isEmpty() ? "Overlay permission: " + Settings.canDrawOverlays(this)
                + " | Notifications: " + OverlayService.notificationsReady(this) : "Last start error: " + error);
        }
    }
    private void start(String mode) {
        if (launching) return;
        if (!Settings.canDrawOverlays(this)) { status.setText("Allow display over other apps first."); return; }
        if (!OverlayService.notificationsReady(this)) {
            status.setText("Enable app notifications and the Overlay control channel first, so Stop stays available.");
            return;
        }
        launching = true;
        status.setText("Starting overlay...");
        ResultReceiver reply = new ResultReceiver(new Handler(Looper.getMainLooper())) {
            @Override protected void onReceiveResult(int result, Bundle data) {
                launching = false;
                if (isFinishing() || isDestroyed()) return;
                if (result != 1) { status.setText(data == null ? "Overlay start failed." : data.getString("error", "Overlay start failed.")); return; }
                // Launch Home only after the foreground service and overlay exist.
                try {
                    startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                } catch (RuntimeException e) { status.setText("Overlay started. Use Home to view it."); }
            }
        };
        Intent intent = new Intent(this, OverlayService.class).setAction(OverlayService.ACTION_START)
            .putExtra(OverlayService.EXTRA_MODE, mode).putExtra("reply", reply);
        try { startForegroundService(intent); }
        catch (RuntimeException e) { launching = false; status.setText("Start failed: " + e.getClass().getSimpleName()); }
    }
}
