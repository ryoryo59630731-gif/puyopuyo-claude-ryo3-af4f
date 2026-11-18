package app.deesstamer

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sin

/**
 * Learns the "usual level" of every analysis band from the music that is actually playing,
 * so thresholds follow the song and the volume (auto level tracking, as in Sonnox SuprEsser,
 * applied per band in the spirit of soothe2's "react to what stands out").
 *
 * Input per frame: an 8-bit waveform snapshot of the output (Android Visualizer format,
 * unsigned, 128 = silence, normalized so only the spectral *shape* is meaningful) and the
 * broadband RMS level in dBFS measured separately at full precision.
 * Band level = broadband RMS + (band power / total power), both in dB.
 *
 * Pure Kotlin so it can be unit tested.
 */
class LevelAnalyzer(layout: DspMapping.Layout) {

    var layout: DspMapping.Layout = layout
        set(v) {
            if (v.resolution != field.resolution) { field = v; reset() }
        }

    /** Long-term broadband level (dBFS). */
    var longRmsDb = DspMapping.ASSUMED_RMS_DB
        private set

    private var longRel = FloatArray(0)
    private var shortRel = FloatArray(0)
    private var lastRmsDb = DspMapping.ASSUMED_RMS_DB

    /** Seconds of non-silent audio analysed since the last reset. */
    var learnedSeconds = 0f
        private set

    private var re = FloatArray(0)
    private var im = FloatArray(0)

    init { reset() }

    fun reset() {
        val l = layout
        longRel = FloatArray(l.mbcBands) { i -> if (i in l.analysisRange) model(i) else 0f }
        shortRel = longRel.copyOf()
        longRmsDb = DspMapping.ASSUMED_RMS_DB
        lastRmsDb = longRmsDb
        learnedSeconds = 0f
    }

    private fun model(i: Int) = DspMapping.modelRelativeDb(layout.bandCenter(i), layout.bandsPerOctave)

    /**
     * @param wave unsigned 8-bit samples (size must be a power of two)
     * @param sampleRateHz sample rate of the captured audio
     * @param rmsDb broadband RMS of the same moment, dBFS
     * @param dtSec time since the previous frame
     */
    fun process(wave: ByteArray, sampleRateHz: Float, rmsDb: Float, dtSec: Float) {
        val n = wave.size
        if (n < 64 || n and (n - 1) != 0 || sampleRateHz <= 0f) return
        lastRmsDb = rmsDb
        if (rmsDb < SILENCE_DB) return // nothing to learn from silence / pauses

        if (re.size != n) { re = FloatArray(n); im = FloatArray(n) }
        for (k in 0 until n) {
            val x = ((wave[k].toInt() and 0xFF) - 128).toFloat()
            val w = 0.5f - 0.5f * cos(2.0 * PI * k / (n - 1)).toFloat() // Hann
            re[k] = x * w
            im[k] = 0f
        }
        fft(re, im)

        val binHz = sampleRateHz / n
        var total = 0.0
        for (k in 1 until n / 2) total += (re[k] * re[k] + im[k] * im[k]).toDouble()
        if (total <= 0.0) return

        val l = layout
        val aLong = alpha(dtSec, LONG_TAU_SEC)
        val aRms = alpha(dtSec, RMS_TAU_SEC)
        val aShort = alpha(dtSec, SHORT_TAU_SEC)
        for (i in l.analysisRange) {
            val lo = l.bandLow(i)
            val hi = l.bandHigh(i)
            var p = 0.0
            val kLo = max(1, kotlin.math.ceil(lo / binHz).toInt())
            val kHi = minOf(n / 2 - 1, kotlin.math.floor(hi / binHz).toInt())
            if (kHi >= kLo) {
                for (k in kLo..kHi) p += (re[k] * re[k] + im[k] * im[k]).toDouble()
            } else {
                // Band narrower than one FFT bin: take its share of the nearest bin.
                val k = (l.bandCenter(i) / binHz).toInt().coerceIn(1, n / 2 - 1)
                p = (re[k] * re[k] + im[k] * im[k]).toDouble() * ((hi - lo) / binHz)
            }
            val rel = (10.0 * log10(p / total + 1e-12)).toFloat()
            shortRel[i] += aShort * (rel - shortRel[i])
            longRel[i] += aLong * (rel - longRel[i])
        }
        longRmsDb += aRms * (rmsDb - longRmsDb)
        learnedSeconds += dtSec
    }

    /**
     * Usual level (dBFS) of each MBC band, for [DspMapping.bandSettings]. The learned balance
     * is kept within ±[MAX_DEVIATION_DB] of the generic music model so a quiet or noisy
     * measurement cannot push a threshold somewhere absurd.
     */
    fun references(): FloatArray {
        val l = layout
        return FloatArray(l.mbcBands) { i ->
            if (i !in l.analysisRange) 0f
            else longRmsDb + longRel[i].coerceIn(model(i) - MAX_DEVIATION_DB, model(i) + MAX_DEVIATION_DB)
        }
    }

    /** How far each band currently is above (+) or below (−) its usual level, dB. */
    fun liveOvershoot(): FloatArray {
        val l = layout
        val refs = references()
        return FloatArray(l.mbcBands) { i ->
            if (i !in l.analysisRange || lastRmsDb < SILENCE_DB) Float.NaN
            else lastRmsDb + shortRel[i] - refs[i]
        }
    }

    companion object {
        const val SILENCE_DB = -60f
        const val LONG_TAU_SEC = 10f
        const val RMS_TAU_SEC = 4f
        const val SHORT_TAU_SEC = 0.05f
        const val MAX_DEVIATION_DB = 12f

        private fun alpha(dt: Float, tau: Float) = (1f - exp(-dt / tau)).coerceIn(0f, 1f)

        /** In-place iterative radix-2 FFT. */
        fun fft(re: FloatArray, im: FloatArray) {
            val n = re.size
            var j = 0
            for (i in 1 until n) {
                var bit = n shr 1
                while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
                j = j xor bit
                if (i < j) {
                    var t = re[i]; re[i] = re[j]; re[j] = t
                    t = im[i]; im[i] = im[j]; im[j] = t
                }
            }
            var len = 2
            while (len <= n) {
                val ang = -2.0 * PI / len
                val wr = cos(ang).toFloat()
                val wi = sin(ang).toFloat()
                var i = 0
                while (i < n) {
                    var cr = 1f
                    var ci = 0f
                    for (k in 0 until len / 2) {
                        val a = i + k
                        val b = a + len / 2
                        val xr = re[b] * cr - im[b] * ci
                        val xi = re[b] * ci + im[b] * cr
                        re[b] = re[a] - xr; im[b] = im[a] - xi
                        re[a] += xr; im[a] += xi
                        val ncr = cr * wr - ci * wi
                        ci = cr * wi + ci * wr
                        cr = ncr
                    }
                    i += len
                }
                len = len shl 1
            }
        }
    }
}
