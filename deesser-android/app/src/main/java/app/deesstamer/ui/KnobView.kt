package app.deesstamer.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Bundle
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * Rotary knob in the style of audio plug-ins.
 * Drag up/down (or left/right) to turn, double-tap to reset to the default value.
 */
class KnobView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var label: String = ""
        set(v) { field = v; contentDescription = v; invalidate() }

    var min = 0f
    var max = 100f
    var defaultValue = 50f

    /** Optional non-linear mapping, e.g. logarithmic for frequencies. 0..1 <-> value. */
    var toValue: (Float) -> Float = { t -> min + (max - min) * t }
    var toPosition: (Float) -> Float = { v -> (v - min) / (max - min) }

    var formatter: (Float) -> String = { v -> "%.0f".format(v) }
    var onValueChange: ((Float) -> Unit)? = null

    var accentColor: Int = 0xFF4FD1C5.toInt()
        set(v) { field = v; valuePaint.color = v; pointerPaint.color = v; invalidate() }

    private var position = 0.5f

    var value: Float
        get() = toValue(position)
        set(v) {
            position = toPosition(v).coerceIn(0f, 1f)
            invalidate()
        }

    private val density = resources.displayMetrics.density
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 5 * density; strokeCap = Paint.Cap.ROUND
        color = 0xFF2E3440.toInt()
    }
    private val valuePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 5 * density; strokeCap = Paint.Cap.ROUND
        color = accentColor
    }
    private val bodyPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF232833.toInt() }
    private val rimPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 1.5f * density; color = 0xFF3B4252.toInt()
    }
    private val pointerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 3 * density; strokeCap = Paint.Cap.ROUND
        color = accentColor
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFB8C0CC.toInt(); textSize = 12 * density; textAlign = Paint.Align.CENTER
        letterSpacing = 0.05f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFECEFF4.toInt(); textSize = 13 * density; textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private val arcRect = RectF()

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent) = true
        override fun onDoubleTap(e: MotionEvent): Boolean {
            setFromUser(toPosition(defaultValue))
            return true
        }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            // Up or right increases. Full range = 220 dp of travel.
            val delta = (dy - dx) / (220 * density)
            setFromUser(position + delta)
            return true
        }
    })

    init {
        isFocusable = true
        isClickable = true
    }

    private fun setFromUser(p: Float) {
        val np = p.coerceIn(0f, 1f)
        if (np == position) return
        position = np
        invalidate()
        onValueChange?.invoke(value)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = resolveSize((84 * density).toInt(), widthMeasureSpec)
        val h = resolveSize((112 * density).toInt(), heightMeasureSpec)
        setMeasuredDimension(w, h)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled) return false
        if (event.actionMasked == MotionEvent.ACTION_DOWN) parent?.requestDisallowInterceptTouchEvent(true)
        if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
            parent?.requestDisallowInterceptTouchEvent(false)
        }
        return gestures.onTouchEvent(event) || super.onTouchEvent(event)
    }

    override fun onDraw(canvas: Canvas) {
        val textBlock = 34 * density
        val size = min(width.toFloat(), height - textBlock) - 10 * density
        val cx = width / 2f
        val cy = 5 * density + size / 2f
        val r = size / 2f
        arcRect.set(cx - r, cy - r, cx + r, cy + r)

        val start = 135f
        val sweep = 270f
        val a = if (isEnabled) 255 else 90
        trackPaint.alpha = a; valuePaint.alpha = a; pointerPaint.alpha = a; textPaint.alpha = a

        canvas.drawArc(arcRect, start, sweep, false, trackPaint)
        canvas.drawArc(arcRect, start, sweep * position, false, valuePaint)

        val bodyR = r - 9 * density
        canvas.drawCircle(cx, cy, bodyR, bodyPaint)
        canvas.drawCircle(cx, cy, bodyR, rimPaint)

        val ang = Math.toRadians((start + sweep * position).toDouble())
        val inner = bodyR * 0.35f
        val outer = bodyR * 0.85f
        canvas.drawLine(
            cx + (cos(ang) * inner).toFloat(), cy + (sin(ang) * inner).toFloat(),
            cx + (cos(ang) * outer).toFloat(), cy + (sin(ang) * outer).toFloat(),
            pointerPaint,
        )

        val textTop = cy + r + 4 * density
        canvas.drawText(formatter(value), cx, textTop + 13 * density, textPaint)
        canvas.drawText(label, cx, textTop + 28 * density, labelPaint)
    }

    // ---- Accessibility: expose as an adjustable range ----

    @Suppress("DEPRECATION") // RangeInfo.obtain is only used below API 33.
    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.className = "android.widget.SeekBar"
        val type = AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_PERCENT
        info.rangeInfo = if (android.os.Build.VERSION.SDK_INT >= 33) {
            AccessibilityNodeInfo.RangeInfo(type, 0f, 100f, position * 100f)
        } else {
            AccessibilityNodeInfo.RangeInfo.obtain(type, 0f, 100f, position * 100f)
        }
        if (android.os.Build.VERSION.SDK_INT >= 30) info.stateDescription = formatter(value)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD)
    }

    override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean {
        when (action) {
            AccessibilityNodeInfo.ACTION_SCROLL_FORWARD -> { setFromUser(position + 0.05f); return true }
            AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD -> { setFromUser(position - 0.05f); return true }
        }
        return super.performAccessibilityAction(action, arguments)
    }
}
