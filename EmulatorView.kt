package com.ndsemu.app

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.nio.ByteBuffer

/** Draws both DS screens stacked (256x384) and turns touches on the lower screen into stylus input. */
class EmulatorView(context: Context, attrs: AttributeSet? = null) :
    SurfaceView(context, attrs), SurfaceHolder.Callback {

    private val bitmap = Bitmap.createBitmap(SCREEN_W, SCREEN_H * 2, Bitmap.Config.ARGB_8888)
    private val paint = Paint().apply { isFilterBitmap = false }
    private val dst = RectF()

    @Volatile
    private var surfaceReady = false

    var onTouch: ((down: Boolean, x: Int, y: Int) -> Unit)? = null

    init {
        holder.addCallback(this)
    }

    /** Called from the emulation thread with the core's framebuffer. */
    fun present(pixels: ByteBuffer) {
        if (!surfaceReady) return
        pixels.rewind()
        bitmap.copyPixelsFromBuffer(pixels)
        val canvas = holder.lockCanvas() ?: return
        try {
            canvas.drawColor(Color.BLACK)
            layoutRect(canvas.width.toFloat(), canvas.height.toFloat(), dst)
            canvas.drawBitmap(bitmap, null, dst, paint)
        } finally {
            holder.unlockCanvasAndPost(canvas)
        }
    }

    /** Largest centered rect with the 256:384 aspect ratio. */
    private fun layoutRect(w: Float, h: Float, out: RectF) {
        val scale = minOf(w / SCREEN_W, h / (SCREEN_H * 2))
        val rw = SCREEN_W * scale
        val rh = SCREEN_H * 2 * scale
        val left = (w - rw) / 2f
        val top = (h - rh) / 2f
        out.set(left, top, left + rw, top + rh)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                val r = RectF()
                layoutRect(width.toFloat(), height.toFloat(), r)
                val scale = r.width() / SCREEN_W
                val sx = ((e.x - r.left) / scale).toInt()
                val sy = ((e.y - r.top) / scale).toInt() - SCREEN_H  // 0 = top of the bottom screen
                if (sx in 0 until SCREEN_W && sy in 0 until SCREEN_H) onTouch?.invoke(true, sx, sy)
                else onTouch?.invoke(false, 0, 0)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> onTouch?.invoke(false, 0, 0)
        }
        return true
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
    }

    companion object {
        const val SCREEN_W = 256
        const val SCREEN_H = 192
    }
}
