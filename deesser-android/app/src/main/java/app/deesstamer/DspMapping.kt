package app.deesstamer

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

/**
 * Pure math that turns plug-in style knob values into the numbers the Android
 * DynamicsProcessing multi-band compressor understands. No Android classes here so it can be
 * unit tested on the JVM.
 */
object DspMapping {
    const val MIN_FREQ_HZ = 500f
    const val MAX_FREQ_HZ = 20_000f

    /** Upper edge of the top MBC band. Content above this is inaudible for most people. */
    const val TOP_CUTOFF_HZ = 20_000f

    /** If the detection band reaches this high, the band above it is processed as well. */
    const val TOP_MERGE_HZ = 19_000f

    /** Gain used to mute bands outside the detection range in FILTER listen mode. */
    const val SOLO_MUTE_DB = -60f

    private fun pct(v: Float) = (v / 100f).coerceIn(0f, 1f)
    private fun lerp(a: Float, b: Float, t: Float) = a + (b - a) * t

    /** Processing 0% = never triggers (0 dBFS), 100% = triggers at -60 dBFS. */
    fun thresholdDb(processing: Float): Float = -60f * pct(processing)

    /** Intensity sets the ratio on an exponential scale: 0% = 1:1, 50% ≈ 4.5:1, 100% = 20:1. */
    fun rawRatio(intensity: Float): Float = 20f.pow(pct(intensity))

    /**
     * DynamicsProcessing has no dry/wet control, so Mix is approximated by scaling the
     * gain reduction in dB: the ratio is chosen so that the reduction above threshold is
     * `mix × (reduction at the raw ratio)`.
     */
    fun ratio(intensity: Float, mix: Float): Float {
        val r = rawRatio(intensity)
        val slopeReduction = pct(mix) * (1f - 1f / r)
        return 1f / max(1f - slopeReduction, 0.05f)
    }

    /** Sharp = hard knee (0 dB), soft = 24 dB knee. */
    fun kneeDb(sharpness: Float): Float = lerp(24f, 0f, pct(sharpness))

    /** Sharper settings also react faster. */
    fun attackMs(sharpness: Float): Float = lerp(5f, 0.5f, pct(sharpness))
    fun releaseMs(sharpness: Float): Float = lerp(150f, 40f, pct(sharpness))

    /** Low and high edge (Hz) of the detection band, always ordered and inside the usable range. */
    fun bandEdges(p: DeEsserParams): Pair<Float, Float> {
        val (lo, hi) = if (p.range == RangeMode.CUSTOM) {
            min(p.customLowHz, p.customHighHz) to max(p.customLowHz, p.customHighHz)
        } else {
            p.range.lowHz to p.range.highHz
        }
        val l = lo.coerceIn(MIN_FREQ_HZ, MAX_FREQ_HZ / 1.25f)
        val h = hi.coerceIn(l * 1.25f, MAX_FREQ_HZ)
        return l to h
    }

    /**
     * Static gain curve of a soft-knee downward compressor (standard formulation).
     * Returns output level in dB for an input level in dB.
     */
    fun compressorOutputDb(inputDb: Float, thresholdDb: Float, ratio: Float, kneeDb: Float): Float {
        val over = inputDb - thresholdDb
        return when {
            kneeDb > 0f && 2f * abs(over) <= kneeDb -> {
                val x = over + kneeDb / 2f
                inputDb + (1f / ratio - 1f) * x * x / (2f * kneeDb)
            }
            over <= 0f -> inputDb
            else -> thresholdDb + over / ratio
        }
    }

    /** Positive number: how many dB the band is turned down at the given band level. */
    fun gainReductionDb(p: DeEsserParams, bandLevelDb: Float): Float =
        bandLevelDb - compressorOutputDb(bandLevelDb, thresholdDb(p.processing), ratio(p.intensity, p.mix), kneeDb(p.sharpness))

    /** Description of the three MBC bands: [below, detection, above]. */
    data class MbcLayout(
        val cutoffs: FloatArray,
        val compressed: BooleanArray,
    )

    fun mbcLayout(p: DeEsserParams): MbcLayout {
        val (lo, hi) = bandEdges(p)
        val mergeTop = hi >= TOP_MERGE_HZ
        val mid = if (mergeTop) TOP_MERGE_HZ else hi
        return MbcLayout(
            cutoffs = floatArrayOf(lo, mid, TOP_CUTOFF_HZ),
            compressed = booleanArrayOf(false, true, mergeTop),
        )
    }

    /** Post-EQ gains (dB) for the three bands; used for FILTER listen mode and output gain. */
    fun postEqGains(p: DeEsserParams): FloatArray {
        val layout = mbcLayout(p)
        return FloatArray(3) { i ->
            val solo = p.listen == ListenMode.FILTER && !(i == 1 || layout.compressed[i])
            (if (solo) SOLO_MUTE_DB else 0f) + p.outputGainDb
        }
    }
}
