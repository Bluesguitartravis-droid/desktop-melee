package com.desktopmelee.android;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.view.View;
import android.widget.*;

public final class DesktopMeleeActivity extends Activity {
    public static final String PREFS = "desktop_melee";
    public static final String EXTRA_ORIENTATION = "desktopOrientation";
    public static final String EXTRA_OVERLAY = "desktopOverlay";
    public static final String EXTRA_WATCH = "desktopWatch";

    private SharedPreferences prefs;
    private RadioGroup orientation;
    private SeekBar landscapeMargin, landscapeFloor, portraitMargin, portraitFloor;
    private Spinner players, mode, cpu, stocks;
    private CheckBox homeOverlay;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        root.setPadding(pad,pad,pad,pad);
        scroll.addView(root);

        TextView title = text("Desktop Melee", 28);
        root.addView(title);
        root.addView(text("Playable Android build - choose the combat area before launching.", 16));

        root.addView(text("Screen orientation", 18));
        orientation = new RadioGroup(this);
        addRadio("Auto - follow device", 0);
        addRadio("Landscape", 1);
        addRadio("Portrait", 2);
        int savedOrientation = prefs.getInt("orientation",0);
        orientation.check(100 + savedOrientation);
        root.addView(orientation);

        landscapeMargin = slider(root, "Landscape horizontal margin", "landscapeMargin", 0, 20, 3);
        landscapeFloor = slider(root, "Landscape floor height", "landscapeFloor", 60, 96, 88);
        portraitMargin = slider(root, "Portrait horizontal margin", "portraitMargin", 0, 25, 6);
        portraitFloor = slider(root, "Portrait floor height", "portraitFloor", 55, 96, 82);

        root.addView(text("Presentation", 18));
        homeOverlay = new CheckBox(this);
        homeOverlay.setText("Home-screen overlay (transparent)");
        homeOverlay.setChecked(prefs.getBoolean("homeOverlay", true));
        root.addView(homeOverlay);
        root.addView(text("Turn this off to use normal foreground rendering if your Android/Vulkan compositor has trouble with transparency.", 13));

        root.addView(text("Match", 18));
        players = spinner(root, "Fighters", new String[]{"2","3","4"}, prefs.getInt("players",2)-2);
        mode = spinner(root, "Mode", new String[]{"Player 1 vs CPUs","Watch CPUs"}, prefs.getInt("watch",0));
        cpu = spinner(root, "CPU level", new String[]{"1","2","3","4","5","6","7","8","9"}, prefs.getInt("cpu",5)-1);
        stocks = spinner(root, "Stocks", new String[]{"1","2","3","4","5","6","7","8","9"}, Math.max(0,prefs.getInt("stocks",4)-1));

        Button start = new Button(this);
        start.setText("Start Desktop Melee");
        start.setOnClickListener(v -> launch());
        root.addView(start);

        TextView note = text("The next screen is the full native Melee launcher. Choose your own local USA 1.02 disc image there. Rotate only between matches; arena geometry is sampled when a match starts.", 14);
        root.addView(note);

        setContentView(scroll);
    }

    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int sp) {
        TextView t = new TextView(this); t.setText(value); t.setTextSize(sp); t.setPadding(0,dp(8),0,dp(8)); return t;
    }
    private void addRadio(String label, int value) {
        RadioButton b = new RadioButton(this); b.setId(100+value); b.setText(label); orientation.addView(b);
    }
    private SeekBar slider(LinearLayout root, String label, String key, int min, int max, int fallback) {
        TextView caption = text("",16);
        root.addView(caption);
        SeekBar bar = new SeekBar(this);
        bar.setMax(max-min);
        int initial = Math.max(min,Math.min(max,prefs.getInt(key,fallback)));
        bar.setProgress(initial-min);
        Runnable update = () -> caption.setText(label + ": " + (bar.getProgress()+min) + "%");
        update.run();
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar b,int p,boolean fromUser){ update.run(); }
            public void onStartTrackingTouch(SeekBar b){}
            public void onStopTrackingTouch(SeekBar b){}
        });
        root.addView(bar);
        return bar;
    }
    private Spinner spinner(LinearLayout root, String label, String[] values, int selected) {
        root.addView(text(label,16));
        Spinner s = new Spinner(this);
        s.setAdapter(new ArrayAdapter<String>(this, android.R.layout.simple_spinner_dropdown_item, values));
        s.setSelection(Math.max(0,Math.min(values.length-1,selected)));
        root.addView(s);
        return s;
    }
    private void launch() {
        int orientationMode = Math.max(0, orientation.getCheckedRadioButtonId()-100);
        int fighterCount = players.getSelectedItemPosition()+2;
        int watch = mode.getSelectedItemPosition();
        int cpuLevel = cpu.getSelectedItemPosition()+1;
        int stockCount = stocks.getSelectedItemPosition()+1;
        prefs.edit()
            .putInt("orientation",orientationMode)
            .putInt("landscapeMargin",landscapeMargin.getProgress())
            .putInt("landscapeFloor",landscapeFloor.getProgress()+60)
            .putInt("portraitMargin",portraitMargin.getProgress())
            .putInt("portraitFloor",portraitFloor.getProgress()+55)
            .putInt("players",fighterCount).putInt("watch",watch)
            .putInt("cpu",cpuLevel).putInt("stocks",stockCount)
            .putBoolean("homeOverlay", homeOverlay.isChecked()).apply();

        Intent i = new Intent(this, dev.melee.MeleeActivity.class);
        i.putExtra(EXTRA_ORIENTATION, orientationMode);
        i.putExtra(EXTRA_OVERLAY, homeOverlay.isChecked());
        i.putExtra(EXTRA_WATCH, watch == 1);
        java.util.ArrayList<String> args = new java.util.ArrayList<>();
        args.add("--desktop");
        if (homeOverlay.isChecked()) args.add("--android-transparent");
        args.add("--desktop-players"); args.add(Integer.toString(fighterCount));
        args.add("--desktop-watch"); args.add(Integer.toString(watch));
        args.add("--desktop-cpu"); args.add(Integer.toString(cpuLevel));
        args.add("--desktop-stocks"); args.add(Integer.toString(stockCount));
        i.putExtra("args", args.toArray(new String[0]));
        /*
         * Finish the opaque setup activity after starting the translucent game
         * activity. With the setup task gone, Android can composite the Home
         * screen underneath the game surface.
         */
        i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(i);
        finish();
        overridePendingTransition(0, 0);
        return;
    }
}
