package app.deesstamer.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import app.deesstamer.DeEsserParams
import app.deesstamer.DspMapping
import kotlin.math.abs
import kotlin.math.ln

/**
 * Per-band threshold offsets as bars around a 0 dB line (±12 dB), one per analysis band of
 * the current layout, coloured by the target that owns the band (grey = not processed).
 *
 * Only a tap does something here (it selects a band); values are changed with the buttons
 * next to it. Dragging is left to the ScrollView, so scrolling past this view can never change
 * a value by accident.
 */
class BandOffsetView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var params: DeEsserParams = DeEsserParams()
        set(v) { field = v; invalidate() }

    /** Selected analysis band (1-based, as in [DspMapping.Layout.analysisRange]). */
    var selected: Int = 1
        set(v) { field = v; invalidate() }

    var onSelect: ((Int) -> Unit)? = null

    private val d = resources.displayMetrics.density
    private val bgPaint = Paint().apply { color = 0xFF171B22.toInt() }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF2A303B.toInt(); strokeWidth = d }
    private val zeroPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF8C96A6.toInt(); strokeWidth = d }
    private val barPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val slotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF232833.toInt() }
    private val selPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 2 * d; color = 0xFFECEFF4.toInt()
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 10 * d; textAlign = Paint.Align.CENTER
    }
    private val scalePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 9 * d; textAlign = Paint.Align.LEFT
    }

    private val fMin = DspMapping.MIN_FREQ_HZ * 0.95f
    private val fMax = DspMapping.MAX_FREQ_HZ * 1.05f
    private val range = DspMapping.THRESHOLD_OFFSET_RANGE_DB

    private fun left() = 30 * d
    private fun right() = width - 6 * d
    private fun x(f: Float) = left() + (ln(f / fMin) / ln(fMax / fMin)) * (right() - left())

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent) = true
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            bandAt(e.x)?.let { onSelect?.invoke(it) }
            return true
        }
    })

    override fun onTouchEvent(event: MotionEvent): Boolean =
        isEnabled && (gestures.onTouchEvent(event) || super.onTouchEvent(event))

    private fun bandAt(px: Float): Int? {
        val layout = DspMapping.layout(params)
        return layout.analysisRange.minByOrNull { i ->
            val c = (x(layout.bandLow(i)) + x(layout.bandHigh(i))) / 2
            abs(c - px)
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, resolveSize((120 * d).toInt(), heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        val top = 8 * d
        val bottom = height - 16 * d
        val zeroY = (top + bottom) / 2
        fun y(db: Float) = zeroY - (db / range).coerceIn(-1f, 1f) * (bottom - top) / 2
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        // ±12 / ±6 dB guides and the 0 dB line.
        for (db in listOf(-12f, -6f, 6f, 12f)) {
            canvas.drawLine(left(), y(db), right(), y(db), gridPaint)
        }
        canvas.drawLine(left(), zeroY, right(), zeroY, zeroPaint)
        for ((db, s) in listOf(12f to "+12", 0f to "0", -12f to "-12")) {
            canvas.drawText(s, 2 * d, y(db) + 3 * d, scalePaint)
        }
        for ((f, label) in listOf(2_000f to "2k", 5_000f to "5k", 10_000f to "10k", 16_000f to "16k")) {
            canvas.drawText(label, x(f), height - 4 * d, textPaint)
        }

        val layout = DspMapping.layout(params)
        val settings = DspMapping.bandSettings(params)
        val gap = (if (layout.analysisBands > 12) 1.5f else 2.5f) * d
        val dim = !params.bandOffsetsEnabled
        for (i in layout.analysisRange) {
            val l = x(layout.bandLow(i)) + gap / 2
            val r = x(layout.bandHigh(i)) - gap / 2
            canvas.drawRect(l, top, r, bottom, slotPaint)
            // Read the stored value even when switched off, so it can be seen and edited.
            val v = DspMapping.fineBandsOf(layout, i).map { params.bandOffsetsDb.getOrElse(it) { 0f } }.average().toFloat()
            val owner = settings[i].owner
            barPaint.color = if (owner != null) FocusColors.of(owner) else 0xFF5A6272.toInt()
            barPaint.alpha = if (dim) 90 else 255
            if (abs(v) < 0.05f) {
                canvas.drawRect(l, zeroY - 1.5f * d, r, zeroY + 1.5f * d, barPaint)
            } else {
                canvas.drawRect(l, minOf(y(v), zeroY), r, maxOf(y(v), zeroY), barPaint)
            }
            if (i == selected) canvas.drawRect(l - gap / 2, top - 2 * d, r + gap / 2, bottom + 2 * d, selPaint)
        }
    }
}
