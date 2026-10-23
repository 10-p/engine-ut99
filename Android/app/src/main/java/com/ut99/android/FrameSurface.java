package com.ut99.android;

import android.content.Context;
import android.graphics.Matrix;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.SurfaceHolder;

import org.libsdl.app.SDLSurface;

/**
 * SDL's surface view, its pointer events in the pixels of the surface's buffer. SDL takes a touch's or a mouse's
 * position as a position in the buffer, which the resolution scale makes smaller or larger than the view; every
 * pointer event is scaled from the view to the buffer before SDL reads it, so a touch lands where it is drawn.
 */
final class FrameSurface extends SDLSurface {
    private final Matrix viewToBuffer = new Matrix();
    private int bufferWidth, bufferHeight;

    FrameSurface(Context context) {
        super(context);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        bufferWidth = width;
        bufferHeight = height;
        super.surfaceChanged(holder, format, width, height);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        MotionEvent inBuffer = toBuffer(event);
        if (inBuffer == event) return super.dispatchTouchEvent(event);
        try { return super.dispatchTouchEvent(inBuffer); }
        finally { inBuffer.recycle(); }
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        MotionEvent inBuffer = toBuffer(event);
        if (inBuffer == event) return super.dispatchGenericMotionEvent(event);
        try { return super.dispatchGenericMotionEvent(inBuffer); }
        finally { inBuffer.recycle(); }
    }

    /**
     * A pointer event scaled from the view's pixels to the buffer's, as a copy; the event itself while the buffer
     * has the view's size (or no size yet) and for other sources (a pad's sticks are not positions).
     */
    private MotionEvent toBuffer(MotionEvent event) {
        int viewWidth = getWidth(), viewHeight = getHeight();
        if (bufferWidth <= 0 || bufferHeight <= 0 || viewWidth <= 0 || viewHeight <= 0) return event;
        if (bufferWidth == viewWidth && bufferHeight == viewHeight) return event;
        if (!event.isFromSource(InputDevice.SOURCE_CLASS_POINTER)) return event;
        MotionEvent inBuffer = MotionEvent.obtain(event);
        viewToBuffer.setScale((float) bufferWidth / viewWidth, (float) bufferHeight / viewHeight);
        inBuffer.transform(viewToBuffer);
        return inBuffer;
    }
}
