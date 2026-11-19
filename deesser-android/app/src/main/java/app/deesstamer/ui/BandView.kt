package app.deesstamer.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View
import app.deesstamer.DeEsserParams
import app.deesstamer.DspMapping
import app.deesstamer.Focus
import app.deesstamer.ListenMode
import kotlin.math.ln

/** Colours used for each target throughout the UI. */
object FocusColors {
    fun of(f: Focus): Int = when (f) {
        Focus.GUITAR -> 0xFFB794F4.toInt()
        Focus.VOCAL -> 0xFF4FD1C5.toInt()
        Focus.HIHAT -> 0xFFF6AD55.toInt()
    }
}

/**
 * Frequency strip (log scale, 1–20 kHz) showing the analysis bands, which target controls each
 * band, the selected target's range, live band levels (auto level) and the static high shelf.
 */
class BandView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var params: DeEsserParams = DeEsserParams()
        set(v) { field = v; invalidate() }

    var labels: Map<Focus, String> = emptyMap()
        set(v) { field = v; invalidate() }

    /**
     * Live "above usual level" per MBC band in dB (NaN = no data), from auto level.
     * Drawn as bars; a bar turns bright when it passes that band's threshold.
     */
    var live: FloatArray? = null
        set(v) { field = v; invalidate() }

    private val d = resources.displayMetrics.density
    private val bgPaint = Paint().apply { color = 0xFF171B22.toInt() }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF2A303B.toInt(); strokeWidth = d }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val outlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 2 * d }
    private val mutedPaint = Paint().apply { color = 0xB00B0D11.toInt() }
    private val shelfPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFE2E8F0.toInt(); strokeWidth = 1.5f * d; style = Paint.Style.STROKE
        pathEffect = DashPathEffect(floatArrayOf(5 * d, 4 * d), 0f)
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 10 * d; textAlign = Paint.Align.CENTER
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textSize = 11 * d; textAlign = Paint.Align.CENTER; isFakeBoldText = true
    }
    private val path = Path()
    private val barPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thrPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFECEFF4.toInt(); strokeWidth = 1.5f * d }

    private val fMin = 1_000f
    private val fMax = 20_000f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, resolveSize((96 * d).toInt(), heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        val left = 8 * d
        val right = width - 8 * d
        val top = 22 * d
        val bottom = height - 16 * d
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        fun x(f: Float) = left + (ln(f / fMin) / ln(fMax / fMin)) * (right - left)

        for ((f, label) in listOf(1_000f to "1k", 2_000f to "2k", 5_000f to "5k", 10_000f to "10k", 20_000f to "20k")) {
            canvas.drawLine(x(f), top, x(f), bottom, gridPaint)
            canvas.drawText(label, x(f).coerceIn(left + 8 * d, right - 10 * d), height - 4 * d, textPaint)
        }

        val layout = DspMapping.layout(params)
        val settings = DspMapping.bandSettings(params)
        val liveValues = live?.takeIf { it.size == layout.mbcBands }
        val gap = (if (layout.analysisBands > 12) 1f else 1.5f) * d
        // Live bars: -12 dB (bottom) … +18 dB (top) relative to the band's usual level.
        fun ly(db: Float) = bottom - ((db + 12f) / 30f).coerceIn(0f, 1f) * (bottom - top)
        for (i in layout.analysisRange) {
            val l = x(layout.bandLow(i)) + gap / 2
            val r = x(layout.bandHigh(i)) - gap / 2
            val owner = settings[i].owner
            fillPaint.color = if (owner != null) FocusColors.of(owner) else 0xFF2E3440.toInt()
            fillPaint.alpha = if (owner != null) (if (liveValues != null) 55 else 110) else 255
            canvas.drawRect(l, top, r, bottom, fillPaint)
            val v = liveValues?.get(i)
            if (v != null && !v.isNaN()) {
                val margin = if (owner != null) {
                    DspMapping.marginDb(params.focus(owner).processing) + DspMapping.thresholdShiftDb(params, layout, i)
                } else Float.NaN
                val over = !margin.isNaN() && v >= margin
                barPaint.color = if (over && owner != null) FocusColors.of(owner) else 0xFF8C96A6.toInt()
                barPaint.alpha = if (over) 255 else 150
                canvas.drawRect(l + gap, ly(v), r - gap, bottom, barPaint)
                if (!margin.isNaN()) canvas.drawLine(l, ly(margin), r, ly(margin), thrPaint)
            }
        }

        // Selected target: outline its bands and label it.
        val sel = params.selected
        val covered = DspMapping.coveredBands(params.focus(sel), layout)
        val selColor = FocusColors.of(sel)
        val l = x(layout.bandLow(covered.first()))
        val r = x(layout.bandHigh(covered.last()))
        if (params.listen == ListenMode.FILTER) {
            canvas.drawRect(left, top, l, bottom, mutedPaint)
            canvas.drawRect(r, top, right, bottom, mutedPaint)
        }

        outlinePaint.color = selColor
        canvas.drawRect(l, top, r, bottom, outlinePaint)
        labelPaint.color = selColor
        val text = (labels[sel] ?: sel.name) + "  " +
            fmtHz(layout.bandLow(covered.first())) + "–" + fmtHz(layout.bandHigh(covered.last()))
        canvas.drawText(text, ((l + r) / 2f).coerceIn(left + 70 * d, right - 70 * d), top - 7 * d, labelPaint)

        if (params.shelfEnabled) {
            val zeroY = top + (bottom - top) * 0.25f
            fun gy(g: Float) = zeroY + (-g / 18f) * (bottom - zeroY)
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
