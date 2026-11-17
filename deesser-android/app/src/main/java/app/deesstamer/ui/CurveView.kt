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

/**
 * Transfer curve of the de-esser band: input level (x) vs output level (y), -60..0 dB.
 * Shows threshold (Processing), ratio (Intensity × Mix) and knee (Sharpness).
 */
class CurveView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var params: DeEsserParams = DeEsserParams()
        set(v) { field = v; invalidate() }

    private val d = resources.displayMetrics.density
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF2A303B.toInt(); strokeWidth = 1 * d
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
        color = 0xFFF6AD55.toInt(); strokeWidth = 1.2f * d
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF8C96A6.toInt(); textSize = 10 * d
    }
    private val bigTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFECEFF4.toInt(); textSize = 12 * d; isFakeBoldText = true; textAlign = Paint.Align.RIGHT
    }
    private val bgPaint = Paint().apply { color = 0xFF171B22.toInt() }
    private val path = Path()

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = resolveSize((w * 0.8f).toInt().coerceAtMost((220 * d).toInt()), heightMeasureSpec)
        setMeasuredDimension(w, h)
    }

    override fun onDraw(canvas: Canvas) {
        val pad = 8 * d
        val left = pad + 18 * d
        val top = pad
        val right = width - pad
        val bottom = height - pad - 12 * d
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        fun x(db: Float) = left + (db + 60f) / 60f * (right - left)
        fun y(db: Float) = bottom - (db + 60f) / 60f * (bottom - top)

        for (db in -60..0 step 12) {
            canvas.drawLine(x(db.toFloat()), top, x(db.toFloat()), bottom, gridPaint)
            canvas.drawLine(left, y(db.toFloat()), right, y(db.toFloat()), gridPaint)
            canvas.drawText("$db", 2 * d, y(db.toFloat()) + 4 * d, textPaint)
        }
        textPaint.textAlign = Paint.Align.RIGHT
        canvas.drawText("IN dB →", right, height - 4 * d, textPaint)
        textPaint.textAlign = Paint.Align.LEFT
        canvas.drawLine(x(-60f), y(-60f), x(0f), y(0f), unityPaint)

        val t = DspMapping.thresholdDb(params.processing)
        val r = DspMapping.ratio(params.intensity, params.mix)
        val k = DspMapping.kneeDb(params.sharpness)

        canvas.drawLine(x(t), top, x(t), bottom, thresholdPaint)

        path.reset()
        var first = true
        var db = -60f
        while (db <= 0.001f) {
            val out = DspMapping.compressorOutputDb(db, t, r, k)
            if (first) { path.moveTo(x(db), y(out)); first = false } else path.lineTo(x(db), y(out))
            db += 0.5f
        }
        canvas.drawPath(path, curvePaint)

        canvas.drawText("%.1f:1".format(r), right - 4 * d, bottom - 22 * d, bigTextPaint)
        canvas.drawText("TH %.0f dB".format(t), right - 4 * d, bottom - 6 * d, bigTextPaint)
    }
}
