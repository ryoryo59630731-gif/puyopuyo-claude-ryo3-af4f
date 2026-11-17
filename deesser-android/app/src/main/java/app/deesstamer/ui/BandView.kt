package app.deesstamer.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View
import app.deesstamer.DeEsserParams
import app.deesstamer.DspMapping
import app.deesstamer.ListenMode
import kotlin.math.ln

/**
 * Frequency strip (log scale, 50 Hz – 20 kHz) that shades the detection band and, if enabled,
 * draws the static high-shelf curve.
 */
class BandView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var params: DeEsserParams = DeEsserParams()
        set(v) { field = v; invalidate() }

    private val d = resources.displayMetrics.density
    private val bgPaint = Paint().apply { color = 0xFF171B22.toInt() }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF2A303B.toInt(); strokeWidth = d }
    private val bandPaint = Paint().apply { color = 0x554FD1C5 }
    private val mutedPaint = Paint().apply { color = 0xAA0B0D11.toInt() }
    private val edgePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF4FD1C5.toInt(); strokeWidth = 2 * d }
    private val shelfPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFF6AD55.toInt(); strokeWidth = 2 * d; style = Paint.Style.STROKE
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 10 * d; textAlign = Paint.Align.CENTER
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFECEFF4.toInt(); textSize = 11 * d; textAlign = Paint.Align.CENTER; isFakeBoldText = true
    }
    private val path = Path()

    private val fMin = 50f
    private val fMax = 20_000f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, resolveSize((84 * d).toInt(), heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        val left = 8 * d
        val right = width - 8 * d
        val top = 6 * d
        val bottom = height - 16 * d
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        fun x(f: Float) = left + (ln(f / fMin) / ln(fMax / fMin)) * (right - left)

        val marks = listOf(100f to "100", 200f to "", 500f to "500", 1_000f to "1k", 2_000f to "2k",
            5_000f to "5k", 10_000f to "10k", 20_000f to "20k")
        for ((f, label) in marks) {
            canvas.drawLine(x(f), top, x(f), bottom, gridPaint)
            if (label.isNotEmpty()) canvas.drawText(label, x(f).coerceIn(left + 8 * d, right - 10 * d), height - 4 * d, textPaint)
        }

        val (lo, hi) = DspMapping.bandEdges(params)
        if (params.listen == ListenMode.FILTER) {
            canvas.drawRect(left, top, x(lo), bottom, mutedPaint)
            canvas.drawRect(x(hi), top, right, bottom, mutedPaint)
        }
        canvas.drawRect(x(lo), top, x(hi), bottom, bandPaint)
        canvas.drawLine(x(lo), top, x(lo), bottom, edgePaint)
        canvas.drawLine(x(hi), top, x(hi), bottom, edgePaint)
        canvas.drawText(fmtHz(lo) + " – " + fmtHz(hi), (x(lo) + x(hi)) / 2f, top + 14 * d, labelPaint)

        if (params.shelfEnabled) {
            // 0 dB at the top third, -24 dB at the bottom.
            val zeroY = top + (bottom - top) * 0.3f
            fun gy(g: Float) = zeroY + (-g / 24f) * (bottom - zeroY)
            val fx = x(params.shelfFreqHz)
            path.reset()
            path.moveTo(left, zeroY)
            path.lineTo(fx - 10 * d, zeroY)
            path.cubicTo(fx, zeroY, fx, gy(params.shelfGainDb), fx + 10 * d, gy(params.shelfGainDb))
            path.lineTo(right, gy(params.shelfGainDb))
            canvas.drawPath(path, shelfPaint)
        }
    }

    companion object {
        fun fmtHz(f: Float): String =
            if (f >= 1000f) {
                val k = f / 1000f
                if (k >= 10f || k == k.toInt().toFloat()) "%.0fk".format(k) else "%.1fk".format(k)
            } else "%.0f".format(f)
    }
}
