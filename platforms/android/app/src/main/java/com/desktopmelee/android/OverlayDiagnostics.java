package com.desktopmelee.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.os.Process;
import android.provider.Settings;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

public final class OverlayDiagnostics {
    private static final String PREFS = "desktop_melee_overlay_diagnostics";
    private OverlayDiagnostics() {}

    public static synchronized void record(Context context, String event) {
        String stamp = new SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US)
            .format(new Date());
        String line = stamp + " [" + Process.myPid() + "] " + event + "\n";
        Log.i("DesktopMelee", event);
        try {
            SharedPreferences p = context.getApplicationContext()
                .getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            String history = p.getString("breadcrumbs", "") + line;
            if (history.length() > 15000) history = history.substring(history.length() - 15000);
            p.edit().putString("breadcrumbs", history).commit();
        } catch (Throwable t) {
            Log.e("DesktopMelee", "Unable to save event", t);
        }
    }

    public static String report(Context context) {
        StringBuilder out = new StringBuilder("Desktop Melee - Android overlay diagnostics\n");
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            out.append("Version: ").append(info.versionName)
                .append(" / code ").append(info.versionCode).append('\n');
        } catch (Exception e) {
            out.append("Version unknown: ").append(e).append('\n');
        }
        out.append("Device: ").append(Build.MANUFACTURER).append(' ')
            .append(Build.MODEL).append('\n')
            .append("Android: ").append(Build.VERSION.RELEASE).append(" API ")
            .append(Build.VERSION.SDK_INT).append('\n')
            .append("ABI: ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n')
            .append("Overlay permission: ")
            .append(Build.VERSION.SDK_INT >= 23 && Settings.canDrawOverlays(context))
            .append('\n');
        SharedPreferences config = context.getSharedPreferences(
            DesktopMeleeActivity.PREFS, Context.MODE_PRIVATE);
        out.append("Overlay selected: ").append(config.getBoolean("homeOverlay", false))
            .append("; Watch mode: ").append(config.getInt("watch", 0) == 1).append('\n');
        out.append("\n=== PERSISTENT EVENTS (survive restart) ===\n");
        out.append(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString("breadcrumbs", "(no events)"));
        out.append("\n=== RECENT APP LOGCAT ===\n");
        java.lang.Process p = null;
        try {
            p = new ProcessBuilder("logcat", "-d", "-v", "threadtime",
                "--uid=" + Process.myUid(), "-t", "150", "*:V")
                .redirectErrorStream(true).start();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = p.getInputStream()) {
                byte[] buf = new byte[4096];
                int n;
                while ((n = input.read(buf)) != -1) {
                    bytes.write(buf, 0, n);
                    if (bytes.size() > 90000) break;
                }
            }
            if (!p.waitFor(4, TimeUnit.SECONDS)) p.destroyForcibly();
            String logs = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
            if (logs.length() > 18000) logs = logs.substring(logs.length() - 18000);
            out.append(logs);
        } catch (Exception e) {
            out.append("Logcat unavailable: ").append(e).append('\n');
        } finally {
            if (p != null) p.destroy();
        }
        out.append("\nEND - Check for personal file paths before sharing.\n");
        return out.toString();
    }
}
