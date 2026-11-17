package app.deesstamer

import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Pure math that turns the user's settings into the per-band numbers of Android's
 * DynamicsProcessing multi-band compressor. No Android classes here so it can be unit tested.
 *
 * Band layout (12 MBC bands, upper edges in [MBC_CUTOFFS]):
 *   band 0            : below 1.6 kHz, never touched
 *   bands 1..10       : 1/3-octave analysis bands from 1.6 kHz to 16 kHz
 *   band 11           : 16–20 kHz, never touched
 * Each analysis band has its own detector, so only the narrow band that actually spikes is
 * turned down. A band belongs to every enabled target whose range contains the band's centre;
 * if several targets claim it, the most sensitive one wins.
 */
object DspMapping {
    const val MIN_FREQ_HZ = 1_600f
    const val MAX_FREQ_HZ = 16_000f
    const val TOP_CUTOFF_HZ = 20_000f

    /** Gain used to mute bands outside the soloed target in FILTER listen mode. */
    const val SOLO_MUTE_DB = -60f

    /** 1/3-octave edges around 2 kHz: 1587, 2000, 2520, … 12699, 16000 Hz. */
    val ANALYSIS_EDGES: FloatArray = FloatArray(11) { k -> (2_000.0 * 2.0.pow((k - 1) / 3.0)).toFloat() }

    const val ANALYSIS_BANDS = 10
    const val MBC_BANDS = ANALYSIS_BANDS + 2

    /** Upper cutoff of each MBC band. */
    val MBC_CUTOFFS: FloatArray = FloatArray(MBC_BANDS) { i ->
        if (i < ANALYSIS_EDGES.size) ANALYSIS_EDGES[i] else TOP_CUTOFF_HZ
    }

    /** Lower / upper edge and geometric centre of analysis band 1..10. */
    fun bandLow(i: Int) = ANALYSIS_EDGES[i - 1]
    fun bandHigh(i: Int) = ANALYSIS_EDGES[i]
    fun bandCenter(i: Int) = sqrt(bandLow(i) * bandHigh(i))

    private fun pct(v: Float) = (v / 100f).coerceIn(0f, 1f)
    private fun lerp(a: Float, b: Float, t: Float) = a + (b - a) * t

    /**
     * Typical level (dBFS, as seen by a band detector) of a 1/3-octave band in finished music:
     * roughly pink with an extra ~3 dB/oct roll-off in the treble. Thresholds are set relative
     * to this, so "Processing" means "how far above the usual level before reducing".
     */
    fun referenceDb(centerHz: Float): Float = -35f - 3f * log2(centerHz / 2_000f)

    /** Processing 0% = +30 dB over typical (almost never), 50% = +10 dB, 100% = -10 dB. */
    fun marginDb(processing: Float): Float = 30f - 40f * pct(processing)

    /** Intensity sets the ratio on an exponential scale: 0% = 1:1, 50% ≈ 4.5:1, 100% = 20:1. */
    fun rawRatio(intensity: Float): Float = 20f.pow(pct(intensity))

    /**
     * DynamicsProcessing has no dry/wet control, so Mix is approximated by scaling the
     * gain reduction in dB: the ratio is chosen so the reduction above threshold is
     * `mix × (reduction at the raw ratio)`.
     */
    fun ratio(intensity: Float, mix: Float): Float {
        val r = rawRatio(intensity)
        val slopeReduction = pct(mix) * (1f - 1f / r)
        return 1f / max(1f - slopeReduction, 0.05f)
    }

    /** Sharp = hard knee, soft = 12 dB knee. */
    fun kneeDb(sharpness: Float): Float = lerp(12f, 0f, pct(sharpness))

    /** Sharpness scales each target's base attack/release: soft ×2, sharp ×0.5. */
    fun timeFactor(sharpness: Float): Float = lerp(2f, 0.5f, pct(sharpness))

    /** Range of a target, ordered and clamped to the analysed region. */
    fun focusRange(fp: FocusParams): Pair<Float, Float> {
        val lo = minOf(fp.lowHz, fp.highHz).coerceIn(MIN_FREQ_HZ, MAX_FREQ_HZ)
        val hi = maxOf(fp.lowHz, fp.highHz).coerceIn(MIN_FREQ_HZ, MAX_FREQ_HZ)
        return lo to hi
    }

    /**
     * Analysis bands (1..10) covered by a target, enabled or not: those whose centre lies in
     * its range, or the single band nearest to the range if it is narrower than a band.
     */
    fun coveredBands(fp: FocusParams): List<Int> {
        val (lo, hi) = focusRange(fp)
        val inside = (1..ANALYSIS_BANDS).filter { bandCenter(it) in lo..hi }
        if (inside.isNotEmpty()) return inside
        val mid = sqrt(lo * hi)
        return listOf((1..ANALYSIS_BANDS).minBy { abs(log2(bandCenter(it) / mid)) })
    }

    data class BandSetting(
        val cutoffHz: Float,
        /** Target that controls this band, or null for a pass-through band. */
        val owner: Focus?,
        val thresholdDb: Float,
        val ratio: Float,
        val kneeDb: Float,
        val attackMs: Float,
        val releaseMs: Float,
    )

    fun bandSettings(p: DeEsserParams): List<BandSetting> {
        val claims = HashMap<Int, MutableList<Focus>>()
        for (f in Focus.entries) {
            val fp = p.focus(f)
            if (!fp.enabled) continue
            for (b in coveredBands(fp)) claims.getOrPut(b) { mutableListOf() } += f
        }
        val knee = kneeDb(p.sharpness)
        val tf = timeFactor(p.sharpness)
        return List(MBC_BANDS) { i ->
            val owners = claims[i]
            if (owners.isNullOrEmpty()) {
                BandSetting(MBC_CUTOFFS[i], null, 0f, 1f, 0f, 1f, 50f)
            } else {
                val ref = referenceDb(bandCenter(i))
                // Most sensitive target wins; ties go to the stronger one.
                val owner = owners.minWith(
                    compareBy<Focus> { marginDb(p.focus(it).processing) }
                        .thenByDescending { p.focus(it).intensity },
                )
                val fp = p.focus(owner)
                BandSetting(
                    cutoffHz = MBC_CUTOFFS[i],
                    owner = owner,
                    thresholdDb = (ref + marginDb(fp.processing)).coerceIn(-90f, 0f),
                    ratio = ratio(fp.intensity, p.mix),
                    kneeDb = knee,
                    attackMs = owner.baseAttackMs * tf,
                    releaseMs = owner.baseReleaseMs * tf,
                )
            }
        }
    }

    /** Post-EQ gains (dB) per MBC band: solo for FILTER listen, plus output gain. */
    fun postEqGains(p: DeEsserParams): FloatArray {
        val solo = if (p.listen == ListenMode.FILTER) coveredBands(p.focus(p.selected)).toSet() else null
        return FloatArray(MBC_BANDS) { i ->
            val muted = solo != null && i !in solo
            (if (muted) SOLO_MUTE_DB else 0f) + p.outputGainDb
        }
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
}
