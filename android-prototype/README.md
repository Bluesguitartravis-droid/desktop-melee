# Desktop Melee Android prototype 0.0.3

**This is not playable Melee yet.** Four colored placeholders test the Android
home-screen overlay, four-slot layout and controller routing on Odin 3 Ultra.
The APK also loads a native C++/JNI bridge that will be used for the engine port.

## Important: one-time reinstall for stable updates

Versions 0.0.1 and 0.0.2 were signed with GitHub runner debug keys. Those keys
were temporary and their private keys no longer exist, so Android cannot accept
0.0.3 as an update over either old installation.

**Uninstall 0.0.1/0.0.2 once, then install 0.0.3.**

Starting with 0.0.3, the workflow reuses one fixed prototype signing key from the
GitHub release `android-prototype-signing-v1`. Future prototype APKs keep the
same application ID (`com.desktopmelee.android`) and signing certificate and
increment versionCode, so they are intended to install directly over 0.0.3 and
later builds.

The signing key is intentionally public and test-only. It is not suitable for a
Play Store or production release; a production package would use a different
application ID and secure private key.

## Test

1. Install 0.0.3 directly in Android, not DroidDeck.
2. Grant **Display over other apps** and notifications.
3. **Watch demo** animates four CPU placeholders while Home stays usable.
4. **Play demo with controller** lets the Odin controller move P1 while three
   CPU placeholders remain active.
5. Back/B or WATCH releases focus; STOP or the notification removes the overlay.

Android 10/API 29 or newer is required. No disc, root, accessibility service,
Internet or storage permission is requested.

## Build verification

CI runs the controller-state tests, builds an unsigned release APK, signs it with
the stable prototype test key, runs Android lint, verifies package/version/SDK
metadata, rejects unexpected network/storage permissions, and verifies that the
APK certificate hash exactly matches the certificate used to sign it.

Version 0.0.3 uses versionCode 3. Every later update must increase versionCode.
