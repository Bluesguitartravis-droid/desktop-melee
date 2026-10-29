package com.desktopmelee.android;

import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.widget.TextView;
import org.json.*;
import java.io.*;
import java.net.*;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.regex.*;

public final class AndroidUpdater implements AutoCloseable {
    private static final String API =
        "https://api.github.com/repos/Bluesguitartravis-droid/desktop-melee/releases?per_page=30";
    private static final long CHECK_INTERVAL_MS = 6L * 60L * 60L * 1000L;
    private static final String APK_MIME = "application/vnd.android.package-archive";
    private static final Pattern VERSION = Pattern.compile("(\\d+)\\.(\\d+)\\.(\\d+)");

    private final Activity activity;
    private final SharedPreferences prefs;
    private final TextView status;
    private final DownloadManager downloads;
    private boolean registered;

    public AndroidUpdater(Activity activity, SharedPreferences prefs, TextView status) {
        this.activity = activity;
        this.prefs = prefs;
        this.status = status;
        this.downloads = (DownloadManager) activity.getSystemService(Context.DOWNLOAD_SERVICE);
        IntentFilter filter = new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE);
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            activity.registerReceiver(receiver, filter);
        }
        registered = true;
    }

    public void checkIfDue() {
        if (!prefs.getBoolean("autoUpdate", true)) return;
        long last = prefs.getLong("lastUpdateCheck", 0L);
        if (System.currentTimeMillis() - last >= CHECK_INTERVAL_MS) check(false);
    }

    public void checkNow() { check(true); }

    private void check(boolean userInitiated) {
        setStatus("Checking GitHub for Android updates...");
        new Thread(() -> {
            try {
                Release release = findUpdate();
                prefs.edit().putLong("lastUpdateCheck", System.currentTimeMillis()).apply();
                activity.runOnUiThread(() -> {
                    if (release == null) {
                        setStatus("Desktop Melee Android is up to date.");
                        if (userInitiated) {
                            Toast.makeText(activity, "No newer Android Full release found.", Toast.LENGTH_SHORT).show();
                        }
                    } else {
                        setStatus("Update available: " + release.tag);
                        offer(release);
                    }
                });
            } catch (Exception e) {
                activity.runOnUiThread(() -> setStatus("Update check failed: " + e.getMessage()));
            }
        }, "DesktopMeleeUpdater").start();
    }

    private Release findUpdate() throws Exception {
        JSONArray releases = new JSONArray(fetchText(API));
        int[] current = parseVersion(activity.getPackageManager()
            .getPackageInfo(activity.getPackageName(), 0).versionName);
        Release best = null;
        for (int i = 0; i < releases.length(); ++i) {
            JSONObject release = releases.getJSONObject(i);
            if (release.optBoolean("draft", false)) continue;
            String tag = release.optString("tag_name", "");
            if (!tag.startsWith("android-full-v")) continue;
            int[] candidate = parseVersion(tag);
            if (compare(candidate, current) <= 0) continue;
            JSONArray assets = release.optJSONArray("assets");
            if (assets == null) continue;
            for (int j = 0; j < assets.length(); ++j) {
                JSONObject asset = assets.getJSONObject(j);
                String name = asset.optString("name", "");
                String digest = asset.optString("digest", "");
                String url = asset.optString("browser_download_url", "");
                if (!name.endsWith(".apk") || !name.startsWith("Desktop-Melee-Android-Full-")) continue;
                if (!digest.startsWith("sha256:") || url.isEmpty()) continue;
                Release candidateRelease = new Release(tag, url, digest.substring(7).toLowerCase(Locale.ROOT), candidate);
                if (best == null || compare(candidate, best.version) > 0) best = candidateRelease;
            }
        }
        return best;
    }

    private void offer(Release release) {
        new AlertDialog.Builder(activity)
            .setTitle("Desktop Melee update available")
            .setMessage(release.tag + "\n\nDownload, verify, and install this update?")
            .setNegativeButton("Later", null)
            .setPositiveButton("Update", (d, which) -> download(release))
            .show();
    }

    private void download(Release release) {
        try {
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(release.url));
            request.setTitle("Desktop Melee " + release.tag);
            request.setDescription("Downloading verified Android update");
            request.setMimeType(APK_MIME);
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setAllowedOverMetered(true);
            request.setAllowedOverRoaming(false);
            request.setDestinationInExternalFilesDir(activity, Environment.DIRECTORY_DOWNLOADS,
                "Desktop-Melee-update.apk");
            long id = downloads.enqueue(request);
            prefs.edit()
                .putLong("updateDownloadId", id)
                .putString("updateDigest", release.sha256)
                .putString("updateTag", release.tag)
                .apply();
            setStatus("Downloading " + release.tag + "...");
        } catch (Exception e) {
            setStatus("Update download failed: " + e.getMessage());
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (!DownloadManager.ACTION_DOWNLOAD_COMPLETE.equals(intent.getAction())) return;
            long id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L);
            if (id != prefs.getLong("updateDownloadId", -2L)) return;
            verifyAndInstall(id);
        }
    };

    private void verifyAndInstall(long id) {
        setStatus("Verifying downloaded update...");
        new Thread(() -> {
            try {
                Uri uri = downloads.getUriForDownloadedFile(id);
                if (uri == null) throw new IOException("download did not complete");
                String expected = prefs.getString("updateDigest", "");
                String actual = sha256(uri);
                if (!actual.equalsIgnoreCase(expected)) {
                    throw new SecurityException("SHA-256 mismatch; update refused");
                }
                prefs.edit().putBoolean("pendingVerifiedInstall", true).apply();
                activity.runOnUiThread(() -> {
                    setStatus("Update verified. Opening Android installer...");
                    installPending();
                });
            } catch (Exception e) {
                activity.runOnUiThread(() -> setStatus("Update verification failed: " + e.getMessage()));
            }
        }, "DesktopMeleeUpdateVerify").start();
    }

    public void onResume() {
        if (prefs.getBoolean("pendingVerifiedInstall", false)) installPending();
    }

    private void installPending() {
        long id = prefs.getLong("updateDownloadId", -1L);
        Uri uri = id >= 0 ? downloads.getUriForDownloadedFile(id) : null;
        if (uri == null) {
            prefs.edit().putBoolean("pendingVerifiedInstall", false).apply();
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
            !activity.getPackageManager().canRequestPackageInstalls()) {
            Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(settings);
            setStatus("Allow installs from Desktop Melee, then return here.");
            return;
        }
        Intent install = new Intent(Intent.ACTION_INSTALL_PACKAGE);
        install.setData(uri);
        install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        install.putExtra(Intent.EXTRA_NOT_UNKNOWN_SOURCE, true);
        activity.startActivity(install);
        prefs.edit().putBoolean("pendingVerifiedInstall", false).apply();
    }

    private String fetchText(String urlText) throws Exception {
        URL url = new URL(urlText);
        HttpURLConnection c = (HttpURLConnection) url.openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(15000);
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("User-Agent", "Desktop-Melee-Android-Updater");
        int code = c.getResponseCode();
        if (code != 200) throw new IOException("GitHub HTTP " + code);
        try (InputStream in = c.getInputStream();
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) >= 0) out.write(buf, 0, n);
            return out.toString("UTF-8");
        } finally {
            c.disconnect();
        }
    }

    private String sha256(Uri uri) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException("cannot read downloaded APK");
            byte[] buf = new byte[1024 * 64];
            int n;
            while ((n = in.read(buf)) >= 0) md.update(buf, 0, n);
        }
        StringBuilder out = new StringBuilder(64);
        for (byte b : md.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return out.toString();
    }

    private static int[] parseVersion(String value) {
        Matcher m = VERSION.matcher(value == null ? "" : value);
        if (!m.find()) return new int[]{0,0,0};
        return new int[]{Integer.parseInt(m.group(1)), Integer.parseInt(m.group(2)), Integer.parseInt(m.group(3))};
    }

    private static int compare(int[] a, int[] b) {
        for (int i=0;i<3;++i) {
            int c = Integer.compare(a[i], b[i]);
            if (c != 0) return c;
        }
        return 0;
    }

    private void setStatus(String value) {
        if (status != null) activity.runOnUiThread(() -> status.setText(value));
    }

    @Override public void close() {
        if (registered) {
            try { activity.unregisterReceiver(receiver); } catch (Exception ignored) {}
            registered = false;
        }
    }

    private static final class Release {
        final String tag, url, sha256;
        final int[] version;
        Release(String tag, String url, String sha256, int[] version) {
            this.tag = tag; this.url = url; this.sha256 = sha256; this.version = version;
        }
    }
}
