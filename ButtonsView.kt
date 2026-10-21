package com.ndsemu.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View

/** On-screen DS controls with multi-touch (e.g. D-pad + A at the same time). */
class ButtonsView(context: Context, attrs: AttributeSet? = null) : View(context, attrs) {

    private class Btn(val label: String, val key: Int) {
        val rect = RectF()
        val hit = RectF()
    }

    private val buttons = listOf(
        Btn("A", NativeCore.KEY_A), Btn("B", NativeCore.KEY_B),
        Btn("X", NativeCore.KEY_X), Btn("Y", NativeCore.KEY_Y),
        Btn("▲", NativeCore.KEY_UP), Btn("▼", NativeCore.KEY_DOWN),
        Btn("◀", NativeCore.KEY_LEFT), Btn("▶", NativeCore.KEY_RIGHT),
        Btn("L", NativeCore.KEY_L), Btn("R", NativeCore.KEY_R),
        Btn("SELECT", NativeCore.KEY_SELECT), Btn("START", NativeCore.KEY_START),
    )

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.rgb(48, 48, 56) }
    private val fillDown = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.rgb(110, 110, 140) }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
    }

    private var mask = 0

    /** Receives the combined key bitmask whenever it changes. */
    var onKeys: ((Int) -> Unit)? = null

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val W = w.toFloat()
        val H = h.toFloat()
        val s = minOf(W * 0.13f, H * 0.24f)  // base button size
        fun place(label: String, cx: Float, cy: Float, bw: Float = s, bh: Float = s) {
            val b = buttons.first { it.label == label }
            b.rect.set(cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2)
            b.hit.set(b.rect)
            b.hit.inset(-s * 0.15f, -s * 0.15f)
        }
        val dx = W * 0.22f
        val dy = H * 0.58f
        place("▲", dx, dy - s)
        place("▼", dx, dy + s)
        place("◀", dx - s, dy)
        place("▶", dx + s, dy)
        val ax = W * 0.78f
        val ay = H * 0.55f
        place("X", ax, ay - s)
        place("B", ax, ay + s)
        place("Y", ax - s, ay)
        place("A", ax + s, ay)
        place("L", W * 0.2f, H * 0.1f, W * 0.3f, H * 0.14f)
        place("R", W * 0.8f, H * 0.1f, W * 0.3f, H * 0.14f)
        place("SELECT", W * 0.40f, H * 0.88f, W * 0.18f, H * 0.12f)
        place("START", W * 0.60f, H * 0.88f, W * 0.18f, H * 0.12f)
        text.textSize = s * 0.38f
    }

    override fun onDraw(canvas: Canvas) {
        for (b in buttons) {
            val down = (mask and b.key) != 0
            canvas.drawRoundRect(b.rect, 18f, 18f, if (down) fillDown else fill)
            val small = b.label.length > 1 && b.label[0].isLetter()
            val oldSize = text.textSize
            if (small) text.textSize = oldSize * 0.6f
            canvas.drawText(b.label, b.rect.centerX(), b.rect.centerY() + text.textSize * 0.35f, text)
            text.textSize = oldSize
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        var m = 0
        val ending = e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL
        if (!ending) {
            for (i in 0 until e.pointerCount) {
                if (e.actionMasked == MotionEvent.ACTION_POINTER_UP && i == e.actionIndex) continue
                for (b in buttons) if (b.hit.contains(e.getX(i), e.getY(i))) m = m or b.key
            }
        }
        if (m != mask) {
            mask = m
            onKeys?.invoke(m)
            invalidate()
        }
        return true
    }
}
