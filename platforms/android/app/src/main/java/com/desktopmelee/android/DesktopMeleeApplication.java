package com.desktopmelee.android;

import android.app.Application;
import android.util.Log;

public final class DesktopMeleeApplication extends Application {
    @Override public void onCreate() {
        super.onCreate();
        OverlayDiagnostics.record(this, "application process started");
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, problem) -> {
            OverlayDiagnostics.record(this, "UNCAUGHT JAVA EXCEPTION (" + thread.getName() + ")\n"
                + Log.getStackTraceString(problem));
            if (previous != null) previous.uncaughtException(thread, problem);
        });
    }
}
