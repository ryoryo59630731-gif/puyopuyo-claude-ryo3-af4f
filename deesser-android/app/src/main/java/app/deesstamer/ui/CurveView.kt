package app.deesstamer.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View
import app.deesstamer.DspMapping

/**
 * Transfer curve of the selected target. The axes are relative to the band's usual level
 * (0 dB = "as loud as this band normally is"), so the threshold reads as
 * "reacts when the band is N dB louder than usual".
 */
class CurveView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    private var thresholdDb = 10f
    private var ratio = 4f
    private var kneeDb = 6f
    private var enabled = true

    var color: Int = 0xFF4FD1C5.toInt()
        set(v) { field = v; curvePaint.color = v; thresholdPaint.color = v; invalidate() }

    /** Texts drawn in the corner: e.g. "6.0:1" and "+10 dB". */
    var axisLabel: String = ""
        set(v) { field = v; invalidate() }

    fun setCurve(thresholdDb: Float, ratio: Float, kneeDb: Float, enabled: Boolean) {
        this.thresholdDb = thresholdDb
        this.ratio = ratio
        this.kneeDb = kneeDb
        this.enabled = enabled
        invalidate()
    }

    private val d = resources.displayMetrics.density
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF2A303B.toInt(); strokeWidth = 1 * d
    }
    private val zeroPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF3B4252.toInt(); strokeWidth = 1.5f * d
    }
    private val unityPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4C566A.toInt(); strokeWidth = 1.2f * d; style = Paint.Style.STROKE
        pathEffect = DashPathEffect(floatArrayOf(6 * d, 5 * d), 0f)
    }
    private val curvePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4FD1C5.toInt(); strokeWidth = 2.5f * d; style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
    }
    private val thresholdPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4FD1C5.toInt(); strokeWidth = 1.2f * d
        pathEffect = DashPathEffect(floatArrayOf(4 * d, 4 * d), 0f)
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 10 * d
    }
    private val bigTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFECEFF4.toInt(); textSize = 12 * d; isFakeBoldText = true; textAlign = Paint.Align.RIGHT
    }
    private val bgPaint = Paint().apply { color = 0xFF171B22.toInt() }
    private val path = Path()

    private val minDb = -30f
    private val maxDb = 30f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = resolveSize((w * 0.62f).toInt().coerceAtMost((200 * d).toInt()), heightMeasureSpec)
        setMeasuredDimension(w, h)
    }

    override fun onDraw(canvas: Canvas) {
        val pad = 8 * d
        val left = pad + 20 * d
        val top = pad
        val right = width - pad
        val bottom = height - pad - 12 * d
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        fun x(db: Float) = left + (db - minDb) / (maxDb - minDb) * (right - left)
        fun y(db: Float) = bottom - (db - minDb) / (maxDb - minDb) * (bottom - top)

        var g = minDb
        while (g <= maxDb + 0.01f) {
            canvas.drawLine(x(g), top, x(g), bottom, if (g == 0f) zeroPaint else gridPaint)
            canvas.drawLine(left, y(g), right, y(g), if (g == 0f) zeroPaint else gridPaint)
            canvas.drawText("%+.0f".format(g).replace("+0", "0"), 2 * d, y(g) + 4 * d, textPaint)
            g += 10f
        }
        textPaint.textAlign = Paint.Align.RIGHT
        canvas.drawText(axisLabel, right, height - 4 * d, textPaint)
        textPaint.textAlign = Paint.Align.LEFT
        canvas.drawLine(x(minDb), y(minDb), x(maxDb), y(maxDb), unityPaint)

        val alpha = if (enabled) 255 else 80
        curvePaint.alpha = alpha
        thresholdPaint.alpha = alpha
        bigTextPaint.alpha = alpha

        val t = thresholdDb.coerceIn(minDb, maxDb)
        canvas.drawLine(x(t), top, x(t), bottom, thresholdPaint)

        path.reset()
        var db = minDb
        var first = true
        while (db <= maxDb + 0.001f) {
            val out = DspMapping.compressorOutputDb(db, thresholdDb, ratio, kneeDb)
            if (first) { path.moveTo(x(db), y(out)); first = false } else path.lineTo(x(db), y(out))
            db += 0.5f
        }
        canvas.drawPath(path, curvePaint)

        canvas.drawText("%.1f:1".format(ratio), right - 4 * d, bottom - 22 * d, bigTextPaint)
        canvas.drawText("%+.0f dB".format(thresholdDb), right - 4 * d, bottom - 6 * d, bigTextPaint)
    }
}
