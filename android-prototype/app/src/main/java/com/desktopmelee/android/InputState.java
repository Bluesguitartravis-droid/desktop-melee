package com.desktopmelee.android;

/** Platform-independent controller state; never reads other apps' input. */
public final class InputState {
    private float stick, hat;
    private boolean left, right;
    public static float filtered(float value, float deadzone) {
        if (!Float.isFinite(value)) return 0;
        return Math.abs(value) <= Math.max(0.12f, deadzone) ? 0 : Math.max(-1, Math.min(1, value));
    }
    public void axes(float x, float hatX, float deadzone) {
        stick = filtered(x, deadzone);
        hat = filtered(hatX, 0.12f);
    }
    public void left(boolean down) { left = down; }
    public void right(boolean down) { right = down; }
    public float horizontal() {
        if (left || right) return (right ? 1 : 0) - (left ? 1 : 0);
        return hat != 0 ? hat : stick;
    }
    public void reset() { stick = hat = 0; left = right = false; }
}
