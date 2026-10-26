package com.ut99.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.RelativeLayout;

import java.io.File;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The game: SDL's activity, in the ":game" process (the engine starts once per process, so the process
 * ends with the game, {@link #onDestroy}). Before SDL starts SDL_main, the game folder chosen in the
 * launcher gets the APK's System files and becomes the engine's base directory (the JNI bridge sets the
 * environment and the working directory). Over SDL's surface lie the touch controls
 * ({@link Ut99TouchOverlayView}): in play they drive the engine through the touch natives below; while a
 * menu is up they hide and pass every touch on to SDL's surface, where it becomes a mouse event.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "UT99";

    /** Which game folder (the launcher's choice): {@link #MODE_INTERNAL} or {@link #MODE_EXTERNAL}. */
    static final String EXTRA_STORAGE_MODE = "com.ut99.android.STORAGE_MODE";
    static final String MODE_INTERNAL = "internal";
    static final String MODE_EXTERNAL = "external";

    /**
     * An engine command line, whitespace-separated, given to the launcher (GameActivity is not exported)
     * and passed on, the desktop's command line: a map URL first, then switches, e.g.
     *
     *   adb shell am start -n com.ut99.android/.MainActivity --es com.ut99.android.ARGS 'DM-Deck16][?Game=Botpack.DeathMatchPlus'
     *
     * The launcher's countdown then starts the game without a touch.
     */
    static final String EXTRA_ARGS = "com.ut99.android.ARGS";

    /**
     * The frame's size as a multiple of the screen's (the launcher's resolution scale), from 0.1 to 1; 0 or absent
     * is the screen's size. The surface's buffer takes that size, SDL reports it as the window, Android scales the
     * buffer to the screen, and touches reach SDL in the buffer's pixels ({@link FrameSurface}).
     */
    static final String EXTRA_RENDER_SCALE = "com.ut99.android.RENDER_SCALE";
    static final float RENDER_SCALE_MIN = 0.1f, RENDER_SCALE_MAX = 1f;

    /**
     * The touch move stick (the launcher's settings), in dp: full tilt at this drag from where the finger is held,
     * 4 to 60, and the dead zone for a resting finger, 0 to 10 and below full tilt. Absent: the defaults.
     */
    static final String EXTRA_MOVE_FULL_TILT_DP = "com.ut99.android.MOVE_FULL_TILT_DP";
    static final String EXTRA_MOVE_DEAD_ZONE_DP = "com.ut99.android.MOVE_DEAD_ZONE_DP";
    static final float MOVE_FULL_TILT_DP = 12f, MOVE_FULL_TILT_MIN_DP = 4f, MOVE_FULL_TILT_MAX_DP = 60f;
    static final float MOVE_DEAD_ZONE_DP = 3f, MOVE_DEAD_ZONE_MAX_DP = 10f;

    static float moveFullTilt(float dp) {
        return Math.max(MOVE_FULL_TILT_MIN_DP, Math.min(MOVE_FULL_TILT_MAX_DP, dp));
    }

    /** The dead zone in its range, and at least 1 dp below full tilt. */
    static float moveDeadZone(float dp, float fullTiltDp) {
        return Math.max(0f, Math.min(Math.min(MOVE_DEAD_ZONE_MAX_DP, fullTiltDp - 1f), dp));
    }

    private AlertDialog exitDialog;

    private static boolean bridgeLoaded = false;

    static {
        // The bridge first, on its own: nativePrepareProcess runs before SDL loads its libraries.
        try {
            System.loadLibrary("ut99_android_bridge");
            bridgeLoaded = true;
        } catch (Throwable t) {
            Log.e(TAG, "could not load ut99_android_bridge", t);
        }
    }

    // android_bridge.cpp.
    private static native boolean nativePrepareProcess(String dataRoot, String homeDir);

    // The touch controls' input, in libUT99.so. Axis 0 is strafe and 1 forward, each -1 to 1 and held until
    // the next call; look takes the drag's pixel deltas, which the engine accumulates.
    static native boolean nativeIsMenuVisible();
    static native void nativeTouchAxis(int axis, float value);
    static native void nativeTouchLook(float dx, float dy);
    static native void nativeTouchButton(int button, boolean down);

    // The button ids of nativeTouchButton.
    private static final int BTN_FIRE = 0, BTN_ALTFIRE = 1, BTN_JUMP = 2, BTN_CROUCH = 3,
            BTN_NEXTWEAPON = 4, BTN_MENU = 5;

    private Ut99TouchOverlayView touchControls;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before super.onCreate, which starts the SDL thread and with it SDL_main.
        File root = resolveGameFolder();
        GameFolder.installSystemFiles(this, root);
        File home = new File(getFilesDir(), "home");
        if (bridgeLoaded) {
            boolean ok = nativePrepareProcess(root.getAbsolutePath(), home.getAbsolutePath());
            Log.i(TAG, "game folder " + root + (ok ? "" : ": the process set-up failed"));
        } else {
            Log.e(TAG, "no bridge: the engine will not find the game folder " + root);
        }
        super.onCreate(savedInstanceState);
        applyRenderScale();

        // A match can go on for long without a touch (a pad, spectating).
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // The whole screen is the game's: the window draws under the system bars, with no bar backgrounds of its
        // own. Without FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS the window of this legacy theme paints a black band over
        // the navigation bar's place (the home line) even while SDL's immersive mode hides the bar.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) getWindow().setNavigationBarContrastEnforced(false);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) getWindow().setDecorFitsSystemWindows(false);

        try {
            touchControls = new Ut99TouchOverlayView(this);
            addContentView(touchControls, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        } catch (Throwable t) {
            Log.e(TAG, "could not add the touch controls", t);
        }
    }

    // A real pad's input hides the touch controls (Ut99TouchOverlayView.onRealPadInput); SDL still gets every event.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (touchControls != null && isPadPress(event)) touchControls.onRealPadInput();
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (touchControls != null && isPadPush(event)) touchControls.onRealPadInput();
        return super.dispatchGenericMotionEvent(event);
    }

    /** A game pad's button going down (a pad's own source, or a gamepad key code from any source). */
    static boolean isPadPress(KeyEvent e) {
        if (e.getAction() != KeyEvent.ACTION_DOWN || e.getRepeatCount() != 0) return false;
        return e.isFromSource(InputDevice.SOURCE_GAMEPAD) || e.isFromSource(InputDevice.SOURCE_JOYSTICK)
                || KeyEvent.isGamepadButton(e.getKeyCode());
    }

    private static final int[] PAD_AXES = {
        MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RX, MotionEvent.AXIS_RY,
        MotionEvent.AXIS_RZ, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_LTRIGGER,
        MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS, MotionEvent.AXIS_BRAKE,
    };

    /** A pad's stick, trigger or hat pushed at least half way (a resting stick's drift stays below). */
    static boolean isPadPush(MotionEvent e) {
        if (!e.isFromSource(InputDevice.SOURCE_JOYSTICK) || e.getActionMasked() != MotionEvent.ACTION_MOVE) return false;
        for (int axis : PAD_AXES) if (Math.abs(e.getAxisValue(axis)) >= 0.5f) return true;
        return false;
    }

    /** SDL's surface, whose pointer events SDL reads in the buffer's pixels at every resolution scale. */
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new FrameSurface(context);
    }

    /** Sizes SDL's surface buffer to the scaled frame before the surface is created; at scale 1 it stays the screen's. */
    private void applyRenderScale() {
        float scale = getIntent() != null ? getIntent().getFloatExtra(EXTRA_RENDER_SCALE, 0f) : 0f;
        if (scale < RENDER_SCALE_MIN || scale >= RENDER_SCALE_MAX || mSurface == null) return;
        int[] screen = landscapeScreen(this);
        int[] frame = frameSize(screen[0], screen[1], scale);
        // SDL adds its surface view with no layout parameters, and a fixed-size SurfaceView then measures to the
        // buffer's size and sits in a corner; filling the parent keeps it on the whole screen, scaled.
        mSurface.setLayoutParams(new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
        mSurface.getHolder().setFixedSize(frame[0], frame[1]);
        Log.i(TAG, "resolution scale " + scale + ": " + frame[0] + "x" + frame[1] + " for the " + screen[0] + "x" + screen[1] + " screen");
    }

    /** The whole screen's size in landscape, the way the game holds it: the long side first. */
    static int[] landscapeScreen(Activity a) {
        Rect b = maximumWindowBounds(a);
        return new int[] { Math.max(b.width(), b.height()), Math.min(b.width(), b.height()) };
    }

    /** The frame for a screen at a scale: each side rounded to an even number, at least 320 x 240. */
    static int[] frameSize(int screenW, int screenH, float scale) {
        if (scale >= RENDER_SCALE_MAX) return new int[] { screenW, screenH };
        int w = Math.max(320, Math.round(screenW * scale / 2f) * 2);
        int h = Math.max(240, Math.round(screenH * scale / 2f) * 2);
        return new int[] { w, h };
    }

    /**
     * The launcher's choice; with none (the game process started again from the recent apps), the
     * internal copy if there is one. An internal choice without an internal copy runs from shared storage.
     */
    private File resolveGameFolder() {
        String mode = getIntent() != null ? getIntent().getStringExtra(EXTRA_STORAGE_MODE) : null;
        File internal = GameFolder.internalRoot(this);
        if (MODE_EXTERNAL.equals(mode)) return GameFolder.externalRoot(this);
        if (GameFolder.isGameFolder(internal)) return internal;
        if (MODE_INTERNAL.equals(mode)) Log.w(TAG, "no internal copy at " + internal + ": running from shared storage");
        return GameFolder.externalRoot(this);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "UT99" };
    }

    @Override
    protected String[] getArguments() {
        // The engine writes its inis, its log and the saves into the game folder, as on a desktop. The
        // command line from adb is the whole command line (a map URL must be the first argument).
        ArrayList<String> args = new ArrayList<String>();
        String extra = getIntent() != null ? getIntent().getStringExtra(EXTRA_ARGS) : null;
        if (extra != null && !extra.trim().isEmpty()) args.addAll(Arrays.asList(extra.trim().split("\\s+")));
        Log.i(TAG, "engine arguments: " + args);
        return args.toArray(new String[0]);
    }

    /**
     * Called by the engine when Back has nothing left to go back to: the usual Android confirmation.
     * Exit quits through SDL, so the engine saves its settings and shuts down.
     */
    public void showExitDialog() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (exitDialog != null && exitDialog.isShowing()) return;
                exitDialog = new AlertDialog.Builder(GameActivity.this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                        .setTitle("Exit game?")
                        .setNegativeButton("Cancel", null)
                        .setPositiveButton("Exit", new android.content.DialogInterface.OnClickListener() {
                            @Override public void onClick(android.content.DialogInterface d, int which) {
                                Log.i(TAG, "exit confirmed");
                                SDLActivity.nativeSendQuit();
                            }
                        })
                        .create();
                exitDialog.show();
                // A pad's confirm button lands on Cancel, as console exit prompts do; Exit is one step away.
                android.widget.Button cancel = exitDialog.getButton(AlertDialog.BUTTON_NEGATIVE);
                if (cancel != null) cancel.requestFocus();
            }
        });
    }

    @Override
    protected void onDestroy() {
        // SDLActivity waits for SDL_main to return (the engine saved its settings and shut down).
        super.onDestroy();
        if (isFinishing()) {
            Log.i(TAG, "the game ended: ending its process");
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }

    /**
     * Full-screen touch controls. The left half is a move stick where the finger lands, the right half
     * drag-look, and round buttons give Fire, Alt Fire, Jump, Crouch, Next Weapon and Menu. A second free
     * touch on the right half fires while the first keeps aiming. While a menu is up the controls hide and
     * every touch goes on to SDL's surface. A real pad's input hides them until the screen is touched: in play
     * that touch only shows them, in a menu it is still the menu's.
     */
    private static final class Ut99TouchOverlayView extends View {
        private final GameActivity activity;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();
        private final SparseArray<TouchRole> roles = new SparseArray<TouchRole>();
        private final Bitmap iconFire, iconAltFire, iconJump, iconCrouch, iconNext, iconMenu;

        private long lastMenuReadMs;
        private boolean menuVisible = false;
        private float leftBaseX, leftBaseY, rightLastX, rightLastY, lx, ly;
        private boolean fireButton, altFire, jump, crouch, next, menu, fire;
        private int rightFireAssistCount = 0;
        // Hidden by a real pad's input until the screen is touched; revealing: the touch that showed them, until it lifts.
        private boolean padHidden = false, revealing = false;

        private enum TouchRole { LEFT_STICK, RIGHT_LOOK, RIGHT_FIRE_ASSIST, FIRE, ALTFIRE, JUMP, CROUCH, NEXT, MENU }

        // The move stick: full tilt this far from the held point (dp, the same on every screen), nothing within the
        // dead zone (a resting finger); past full tilt the held point follows the finger.
        private final float moveFullTiltDp, moveDeadZoneDp;

        Ut99TouchOverlayView(GameActivity activity) {
            super(activity);
            this.activity = activity;
            Intent in = activity.getIntent();
            moveFullTiltDp = moveFullTilt(in != null ? in.getFloatExtra(EXTRA_MOVE_FULL_TILT_DP, MOVE_FULL_TILT_DP) : MOVE_FULL_TILT_DP);
            moveDeadZoneDp = moveDeadZone(in != null ? in.getFloatExtra(EXTRA_MOVE_DEAD_ZONE_DP, MOVE_DEAD_ZONE_DP) : MOVE_DEAD_ZONE_DP,
                    moveFullTiltDp);
            Log.i(TAG, String.format(Locale.US, "touch move stick: full tilt %.1f dp, dead zone %.1f dp", moveFullTiltDp, moveDeadZoneDp));
            setWillNotDraw(false);
            setFocusable(false);
            iconFire = loadIcon("touch_overlay/fire.png");
            iconAltFire = loadIcon("touch_overlay/alternate-fire.png");
            iconJump = loadIcon("touch_overlay/jump.png");
            iconCrouch = loadIcon("touch_overlay/crouch.png");
            iconNext = loadIcon("touch_overlay/next-weapon.png");
            iconMenu = loadIcon("touch_overlay/menu.png");
        }

        // Redraws every 66 ms while the controls can be seen, so they show and hide as a menu closes and opens.
        private final Runnable redrawRunnable = new Runnable() {
            @Override public void run() { invalidate(); postDelayed(this, 66L); }
        };

        @Override public void onVisibilityAggregated(boolean isVisible) {
            super.onVisibilityAggregated(isVisible);
            removeCallbacks(redrawRunnable);
            if (isVisible) postDelayed(redrawRunnable, 66L);
        }

        /** A real pad's button or stick (GameActivity): the controls hide until the screen is touched. */
        void onRealPadInput() {
            if (padHidden) return;
            padHidden = true;
            releaseAll();
            roles.clear();
            invalidate();
        }

        private Bitmap loadIcon(String assetPath) {
            try {
                InputStream in = activity.getAssets().open(assetPath);
                try { return BitmapFactory.decodeStream(in); }
                finally { in.close(); }
            } catch (Throwable t) {
                Log.w(TAG, "touch controls: no icon " + assetPath, t);
                return null;
            }
        }

        private void refreshMenuState(boolean force) {
            long now = SystemClock.uptimeMillis();
            if (force || now - lastMenuReadMs > 120L) {
                lastMenuReadMs = now;
                try { menuVisible = nativeIsMenuVisible(); } catch (Throwable t) { menuVisible = false; }
            }
        }

        private boolean active(boolean force) { refreshMenuState(force); return !menuVisible; }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            refreshMenuState(false);
            if (menuVisible || padHidden || canvas == null) return;
            float w = getWidth(), h = getHeight();
            if (w <= 0 || h <= 0) return;
            float s = Math.min(w, h);
            float pad = Math.max(10f, s * 0.020f);
            float r = Math.max(50f, s * 0.0705f);
            float gap = Math.max(12f, s * 0.018f);

            drawIconButton(canvas, pad + r * 0.72f, pad + r * 0.72f, r * 0.72f, iconMenu);
            float actionY1 = h * 0.42f + r * 2.0f;
            float actionY2 = actionY1 + r * 2.0f + gap;
            float nextY = actionY1 - r * 2.0f - gap;
            drawIconButton(canvas, w - pad - r, nextY, r, iconNext);
            drawIconButton(canvas, w - pad - r, actionY1, r, iconFire);
            drawIconButton(canvas, w - pad - r, actionY2, r, iconAltFire);

            float bottomY = h - pad - r;
            float bottomShiftLeft = r;
            drawIconButton(canvas, w - pad - r - bottomShiftLeft, bottomY, r, iconCrouch);
            drawIconButton(canvas, w - pad - r * 3.05f - gap - bottomShiftLeft, bottomY, r, iconJump);
        }

        private void drawIconButton(Canvas canvas, float cx, float cy, float r, Bitmap icon) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x06202020);
            canvas.drawCircle(cx, cy, r, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(2f, r * 0.055f));
            paint.setColor(0x1AFFFFFF);
            canvas.drawCircle(cx, cy, r, paint);
            if (icon != null) {
                float iconR = r * 0.64f;
                rect.set(cx - iconR, cy - iconR, cx + iconR, cy + iconR);
                paint.setStyle(Paint.Style.FILL);
                paint.setAlpha(40);
                canvas.drawBitmap(icon, null, rect, paint);
                paint.setAlpha(255);
            }
        }

        private boolean hasActiveRightLookPointer() {
            for (int i = 0; i < roles.size(); ++i)
                if (roles.valueAt(i) == TouchRole.RIGHT_LOOK) return true;
            return false;
        }

        private TouchRole resolveRole(float x, float y) {
            TouchRole role = hitRole(x, y);
            // A second free touch on the right half fires while the first pointer keeps aiming.
            if (role == TouchRole.RIGHT_LOOK && x >= getWidth() * 0.5f && hasActiveRightLookPointer())
                return TouchRole.RIGHT_FIRE_ASSIST;
            return role;
        }

        private TouchRole hitRole(float x, float y) {
            float w = getWidth(), h = getHeight(), s = Math.min(w, h);
            float pad = Math.max(10f, s * 0.020f), r = Math.max(50f, s * 0.0705f), gap = Math.max(12f, s * 0.018f);
            if (insideCircle(x, y, pad + r * 0.72f, pad + r * 0.72f, r * 1.10f)) return TouchRole.MENU;
            float actionY1 = h * 0.42f + r * 2.0f, actionY2 = actionY1 + r * 2.0f + gap, nextY = actionY1 - r * 2.0f - gap;
            if (insideCircle(x, y, w - pad - r, nextY, r * 1.35f)) return TouchRole.NEXT;
            if (insideCircle(x, y, w - pad - r, actionY1, r * 1.35f)) return TouchRole.FIRE;
            if (insideCircle(x, y, w - pad - r, actionY2, r * 1.35f)) return TouchRole.ALTFIRE;
            float bottomY = h - pad - r, bottomShiftLeft = r;
            if (insideCircle(x, y, w - pad - r - bottomShiftLeft, bottomY, r * 1.35f)) return TouchRole.CROUCH;
            if (insideCircle(x, y, w - pad - r * 3.05f - gap - bottomShiftLeft, bottomY, r * 1.35f)) return TouchRole.JUMP;
            return x < w * 0.5f ? TouchRole.LEFT_STICK : TouchRole.RIGHT_LOOK;
        }

        private boolean insideCircle(float x, float y, float cx, float cy, float r) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= r * r;
        }

        /** The move stick's axes: the drag's direction, its tilt from the dead zone's edge to full tilt. */
        private void moveStick(float x, float y, boolean down) {
            float density = getResources().getDisplayMetrics().density;
            float full = moveFullTiltDp * density, dead = moveDeadZoneDp * density;
            float dx = x - leftBaseX, dy = y - leftBaseY, len = (float) Math.hypot(dx, dy);
            if (len > full) {
                float k = (len - full) / len;
                leftBaseX += dx * k;
                leftBaseY += dy * k;
                dx -= dx * k;
                dy -= dy * k;
                len = full;
            }
            float m = down && len > dead ? (len - dead) / (full - dead) : 0f;
            lx = m > 0f ? dx / len * m : 0f;
            ly = m > 0f ? dy / len * m : 0f;
            nativeTouchAxis(0, lx);
            nativeTouchAxis(1, ly);
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event == null) return false;
            int action = event.getActionMasked(), index = event.getActionIndex();
            int pointerId = (index >= 0 && index < event.getPointerCount()) ? event.getPointerId(index) : -1;
            TouchRole existingRole = pointerId >= 0 ? roles.get(pointerId) : null;

            if (revealing) {
                if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) revealing = false;
                return true;
            }
            if (padHidden) {
                if (action == MotionEvent.ACTION_DOWN) {
                    padHidden = false;
                    invalidate();
                    if (!active(true)) return false;
                    revealing = true;
                    return true;
                }
                return active(false);
            }

            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                // Each new pointer reads the menu state; in a menu the touch goes on to SDL's surface.
                if (!active(true)) { releaseAll(); roles.clear(); return false; }
                TouchRole role = resolveRole(event.getX(index), event.getY(index));
                roles.put(pointerId, role);
                if (role == TouchRole.LEFT_STICK) { leftBaseX = event.getX(index); leftBaseY = event.getY(index); }
                else if (role == TouchRole.RIGHT_LOOK) { rightLastX = event.getX(index); rightLastY = event.getY(index); }
                if (role == TouchRole.RIGHT_FIRE_ASSIST) addRightFireAssist();
                else updateRole(role, event.getX(index), event.getY(index), true);
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE) {
                if (roles.size() == 0 && !active(true)) return false;
                for (int i = 0; i < event.getPointerCount(); ++i) {
                    TouchRole role = roles.get(event.getPointerId(i));
                    if (role != null && role != TouchRole.RIGHT_FIRE_ASSIST)
                        updateRole(role, event.getX(i), event.getY(i), true);
                }
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL) { releaseAll(); roles.clear(); return true; }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                if (existingRole != null) {
                    if (existingRole == TouchRole.RIGHT_FIRE_ASSIST) removeRightFireAssist();
                    else updateRole(existingRole, event.getX(index), event.getY(index), false);
                    roles.remove(pointerId);
                    return true;
                }
                if (!active(true)) return false;
            }
            return roles.size() > 0;
        }

        private void syncFire() {
            boolean want = fireButton || rightFireAssistCount > 0;
            if (fire != want) { fire = want; nativeTouchButton(BTN_FIRE, want); }
        }
        private void addRightFireAssist() { rightFireAssistCount++; syncFire(); }
        private void removeRightFireAssist() { if (rightFireAssistCount > 0) rightFireAssistCount--; syncFire(); }

        private void updateRole(TouchRole role, float x, float y, boolean down) {
            switch (role) {
                case LEFT_STICK:
                    moveStick(x, y, down);
                    break;
                case RIGHT_LOOK:
                    if (down) {
                        float dx = x - rightLastX, dy = y - rightLastY;
                        rightLastX = x; rightLastY = y;
                        if (dx != 0f || dy != 0f) nativeTouchLook(dx, dy);
                    }
                    break;
                case RIGHT_FIRE_ASSIST: break;
                case FIRE:    setFireButton(down); break;
                case ALTFIRE: if (altFire != down) { altFire = down; nativeTouchButton(BTN_ALTFIRE, down); } break;
                case JUMP:    if (jump != down)    { jump = down;    nativeTouchButton(BTN_JUMP, down); } break;
                case CROUCH:  if (crouch != down)  { crouch = down;  nativeTouchButton(BTN_CROUCH, down); } break;
                case NEXT:    if (next != down)    { next = down;    if (down) nativeTouchButton(BTN_NEXTWEAPON, true); } break;
                case MENU:    if (menu != down)    { menu = down;    nativeTouchButton(BTN_MENU, down); } break;
            }
        }

        private void setFireButton(boolean down) {
            if (fireButton != down) { fireButton = down; syncFire(); }
        }

        private void releaseAll() {
            if (lx != 0f || ly != 0f) { lx = ly = 0f; nativeTouchAxis(0, 0f); nativeTouchAxis(1, 0f); }
            if (fire || fireButton || rightFireAssistCount > 0) {
                fire = false; fireButton = false; rightFireAssistCount = 0; nativeTouchButton(BTN_FIRE, false);
            }
            if (altFire) { altFire = false; nativeTouchButton(BTN_ALTFIRE, false); }
            if (jump)    { jump = false;    nativeTouchButton(BTN_JUMP, false); }
            if (crouch)  { crouch = false;  nativeTouchButton(BTN_CROUCH, false); }
            if (menu)    { menu = false;    nativeTouchButton(BTN_MENU, false); }
            next = false;
        }
    }

    /** The largest window's bounds: WindowMetrics from Android 11, the display's real size before. */
    static Rect maximumWindowBounds(Activity a) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) return a.getWindowManager().getMaximumWindowMetrics().getBounds();
        Point p = new Point();
        a.getWindowManager().getDefaultDisplay().getRealSize(p);
        return new Rect(0, 0, p.x, p.y);
    }
}
