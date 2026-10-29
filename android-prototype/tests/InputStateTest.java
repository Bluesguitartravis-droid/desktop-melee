import com.desktopmelee.android.InputState;

public final class InputStateTest {
    private static int checks;
    private static void equal(float got, float expected) {
        checks++; if (Math.abs(got - expected) > .001f || !Float.isFinite(got)) throw new AssertionError(got + " != " + expected);
    }
    public static void main(String[] args) {
        InputState state = new InputState();
        equal(state.horizontal(), 0);
        state.axes(.05f, 0, .12f); equal(state.horizontal(), 0);
        state.axes(.7f, 0, .12f); equal(state.horizontal(), .7f);
        state.axes(.7f, -1, .12f); equal(state.horizontal(), -1);
        state.right(true); equal(state.horizontal(), 1);
        state.left(true); equal(state.horizontal(), 0);
        state.right(false); equal(state.horizontal(), -1);
        state.reset(); equal(state.horizontal(), 0);
        state.axes(Float.NaN, Float.POSITIVE_INFINITY, .12f); equal(state.horizontal(), 0);
        state.axes(5, 0, .12f); equal(state.horizontal(), 1);
        state.axes(-5, 0, .12f); equal(state.horizontal(), -1);
        state.axes(.2f, 0, .25f); equal(state.horizontal(), 0);
        state.left(true); state.reset(); equal(state.horizontal(), 0);
        System.out.println("Input state: " + checks + " checks passed");
    }
}
