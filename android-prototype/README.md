# Desktop Melee Android: overlay/input test 0.0.2

**This is not playable Melee.** Yellow and pink circles are diagnostic placeholders. There is no Melee engine, Nintendo content or disc loading in this APK. The purpose is to test Android home-screen visibility and controller routing on Odin 3 Ultra, before attempting the native engine port.

## Install and test

Download the debug APK from the Android prerelease. Install directly in Android, not DroidDeck. Android 10/API 29 or newer is required. The prototype is Java-only with no native libraries, including on ARM64 devices.

1. Open **Desktop Melee Overlay Test**. Grant **Display over other apps** to this app.
2. Allow notifications and leave the **Overlay control** notification channel enabled. This prototype deliberately requires a visible Stop notification.
3. Tap **Watch demo**. It starts an animated, non-focusable overlay and opens Home. Check that the two circles appear, icons remain tappable and pages swipe normally.
4. Open the app again and tap **Play demo with controller**. Use the left stick or D-pad to move the yellow circle, A to jump and X to flash white. The HUD shows window focus and the last recognized input.
5. **Back/B or WATCH releases controller focus into Watch mode. STOP removes the overlay.** The notification and app also provide Stop. Turning the screen off or removing the app task ends the service.

Play mode intentionally takes window focus and captures touches across its window. The home screen remains visible but its icons are not usable until Watch mode. Actual controller labels, focus delivery and touch passthrough must be tested on Odin firmware. No root, accessibility service, screen capture, storage access or Internet permission is requested. Other overlays and apps' security policies can still block touch-through; do not disable Android security checks.

The service starts only from an explicit user action; there is no boot receiver or automatic restart. Startup errors are shown in the app. Controller state resets on focus loss/disconnect. This is a one-controller routing test, not two-player support, AI combat or geometry detection.

## Build and verification

JDK 17, Gradle 8.9, Android SDK/platform 35, Build Tools 35.0.0, Android Gradle Plugin 8.7.3. Open this directory in Android Studio or run `gradle --no-daemon :app:assembleDebug :app:lintDebug` with that toolchain and ANDROID_HOME configured.

The **Android overlay prototype APK** workflow builds on the dedicated `android-overlay-prototype` branch. It runs 13 platform-independent input checks, compiles the APK, runs Android lint, checks the APK signature, records manifest metadata, and publishes a debug-only prerelease with SHA-256 hashes and matching source. Windows and Linux release code remains unchanged.

**Debug signing is temporary:** the runner generates a test key. Later test builds may require uninstalling this app before installation. No production signing keys are stored in the repository. Do not use this prototype package for persistent saved games. The APK has no built-in updater.

A successful build is not a hardware gameplay test. No actual Melee engine integration, performance claim or Odin validation is implied.

## Android references

- https://developer.android.com/build/releases/agp-8-7-0-release-notes
- https://developer.android.com/develop/ui/views/notifications/notification-permission
- https://developer.android.com/reference/android/view/WindowManager.LayoutParams
- https://developer.android.com/about/versions/14/changes/fgs-types-required
- https://developer.android.com/about/versions/15/behavior-changes-15

## 0.0.2 milestone

- Four placeholder fighter slots are visible at once.
- Watch mode animates all four without taking launcher input focus.
- Play mode gives the Odin controller to P1 while three CPU placeholders remain active.
- The APK loads `libdesktopmelee_native.so` and calls a JNI status function from Java. This proves the Android app can host our own native C++ code before we begin moving the actual game/render path across.
