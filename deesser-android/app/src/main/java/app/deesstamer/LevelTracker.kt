package app.deesstamer

import android.media.audiofx.Visualizer
import android.os.SystemClock
import android.util.Log

/**
 * Measures what is playing through Android's Visualizer on the output mix (session 0) and
 * feeds [LevelAnalyzer]. The Visualizer does not use the microphone, but Android requires the
 * RECORD_AUDIO permission to create one. Callbacks arrive on the main thread.
 */
class LevelTracker(
    layout: DspMapping.Layout,
    private val onLive: (FloatArray) -> Unit,
    private val onReferences: (FloatArray) -> Unit,
) {
    val analyzer = LevelAnalyzer(layout)
    private var visualizer: Visualizer? = null
    private val peakRms = Visualizer.MeasurementPeakRms()
    private var lastFrameMs = 0L
    private var lastRefsMs = 0L

    /** Returns null on success, or a short error description. */
    fun start(): String? = try {
        val v = Visualizer(0)
        v.setEnabled(false)
        v.setCaptureSize(Visualizer.getCaptureSizeRange()[1])
        v.setScalingMode(Visualizer.SCALING_MODE_NORMALIZED)
        v.setMeasurementMode(Visualizer.MEASUREMENT_MODE_PEAK_RMS)
        v.setDataCaptureListener(
            object : Visualizer.OnDataCaptureListener {
                override fun onWaveFormDataCapture(vis: Visualizer, wave: ByteArray, samplingRate: Int) {
                    onFrame(vis, wave, samplingRate / 1000f)
                }
                override fun onFftDataCapture(vis: Visualizer, fft: ByteArray, samplingRate: Int) = Unit
            },
            Visualizer.getMaxCaptureRate(), true, false,
        )
        val status = v.setEnabled(true)
        if (status != Visualizer.SUCCESS) {
            v.release()
            "Visualizer enable failed ($status)"
        } else {
            visualizer = v
            null
        }
    } catch (t: Throwable) {
        Log.w(TAG, "Visualizer start failed", t)
        t.javaClass.simpleName + (t.message?.let { ": $it" } ?: "")
    }

    fun stop() {
        visualizer?.let { v ->
            runCatching { v.setEnabled(false) }
            runCatching { v.release() }
        }
        visualizer = null
    }

    private fun onFrame(vis: Visualizer, wave: ByteArray, sampleRateHz: Float) {
        val now = SystemClock.elapsedRealtime()
        val dt = if (lastFrameMs == 0L) 0.05f else ((now - lastFrameMs) / 1000f).coerceIn(0.001f, 0.5f)
        lastFrameMs = now
        val rmsDb = runCatching {
            if (vis.getMeasurementPeakRms(peakRms) == Visualizer.SUCCESS) peakRms.mRms / 100f else Float.NaN
        }.getOrDefault(Float.NaN)
        if (rmsDb.isNaN()) return
        analyzer.process(wave, sampleRateHz, rmsDb, dt)
        onLive(analyzer.liveOvershoot())
        if (now - lastRefsMs >= REFS_INTERVAL_MS) {
            lastRefsMs = now
            onReferences(analyzer.references())
        }
    }

    companion object {
        private const val TAG = "LevelTracker"
        private const val REFS_INTERVAL_MS = 1_000L
    }
}
