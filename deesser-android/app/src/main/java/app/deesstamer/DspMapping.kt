package app.deesstamer

import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.log2
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Pure math that turns the user's settings into the per-band numbers of Android's
 * DynamicsProcessing multi-band compressor. No Android classes here so it can be unit tested.
 *
 * Band layout (see [Layout]):
 *   band 0            : below 1.6 kHz, never touched
 *   bands 1..N        : 1/3- or 1/6-octave analysis bands from 1.6 kHz to 16 kHz
 *   band N+1          : 16–20 kHz, never touched
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

    /** Broadband level assumed when the level is not measured (auto level off). */
    const val ASSUMED_RMS_DB = -20f

    /** Spike size used to express INTENSITY as "about how many dB a spike is pulled down". */
    const val DEPTH_REFERENCE_OVERSHOOT_DB = 12f

    /** Lowest analysis edge: one third of an octave below 2 kHz. */
    private val LOW_EDGE_OCT = -1.0 / 3.0

    /** Analysis region spans 2 kHz·2^(-1/3) … 2 kHz·2^3 = 1587 Hz … 16 kHz (10/3 octaves). */
    class Layout(val resolution: Resolution) {
        val bandsPerOctave = resolution.bandsPerOctave
        val analysisBands = 10 * bandsPerOctave / 3
        val mbcBands = analysisBands + 2

        /** analysisBands + 1 edges. */
        val edges: FloatArray = FloatArray(analysisBands + 1) { k ->
            (2_000.0 * 2.0.pow(LOW_EDGE_OCT + k.toDouble() / bandsPerOctave)).toFloat()
        }

        /** Upper cutoff of each MBC band. */
        val cutoffs: FloatArray = FloatArray(mbcBands) { i -> if (i < edges.size) edges[i] else TOP_CUTOFF_HZ }

        fun bandLow(i: Int) = edges[i - 1]
        fun bandHigh(i: Int) = edges[i]
        fun bandCenter(i: Int) = sqrt(bandLow(i) * bandHigh(i))
        val analysisRange: IntRange get() = 1..analysisBands
    }

    private val layouts = Resolution.entries.associateWith { Layout(it) }
    fun layout(r: Resolution): Layout = layouts.getValue(r)
    fun layout(p: DeEsserParams): Layout = layout(p.resolution)

    private fun pct(v: Float) = (v / 100f).coerceIn(0f, 1f)
    private fun lerp(a: Float, b: Float, t: Float) = a + (b - a) * t

    /**
     * Typical level of an analysis band relative to the broadband level, for finished music:
     * roughly pink (a 1/3-octave band ≈ -15 dB) with an extra ~3 dB/oct roll-off in the treble.
     * Narrower bands hold proportionally less energy.
     */
    fun modelRelativeDb(centerHz: Float, bandsPerOctave: Int = 3): Float =
        -15f - 3f * log2(centerHz / 2_000f) - 10f * log10(bandsPerOctave / 3f)

    /** Usual level (dBFS) of a band when the broadband level is [rmsDb]. */
    fun referenceDb(centerHz: Float, bandsPerOctave: Int = 3, rmsDb: Float = ASSUMED_RMS_DB): Float =
        rmsDb + modelRelativeDb(centerHz, bandsPerOctave)

    /** Processing 0% = +30 dB over the usual level (almost never), 50% = +10 dB, 100% = -10 dB. */
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

    /**
     * Reduction for a spike [DEPTH_REFERENCE_OVERSHOOT_DB] above the normal threshold (before
     * [thresholdOffsetDb]; lowering the threshold makes the same spike overshoot by more).
     * Shown next to INTENSITY as an approximate "range"/"depth" (DynamicsProcessing cannot
     * cap reduction).
     */
    fun estimatedDepthDb(intensity: Float, mix: Float, thresholdOffsetDb: Float = 0f): Float =
        max(0f, DEPTH_REFERENCE_OVERSHOOT_DB - thresholdOffsetDb) * (1f - 1f / ratio(intensity, mix))

    // ---- threshold offsets: global knob + optional per-band fine tuning ----

    /** Both offsets are limited to ±this many dB. */
    const val THRESHOLD_OFFSET_RANGE_DB = 12f

    /** Per-band offsets are stored per 1/6-octave analysis band (the finer layout). */
    const val FINE_BAND_COUNT = 20

    val ZERO_BAND_OFFSETS: List<Float> = List(FINE_BAND_COUNT) { 0f }

    private fun clampOffset(db: Float) = db.coerceIn(-THRESHOLD_OFFSET_RANGE_DB, THRESHOLD_OFFSET_RANGE_DB)

    /** Stored fine bands (0-based) that make up analysis band [i] (1-based) of [layout]. */
    fun fineBandsOf(layout: Layout, i: Int): IntRange {
        val per = FINE_BAND_COUNT / layout.analysisBands
        return (i - 1) * per until i * per
    }

    /** Per-band offset of analysis band [i], or 0 when the per-band adjustment is off. */
    fun bandOffsetDb(p: DeEsserParams, layout: Layout, i: Int): Float {
        if (!p.bandOffsetsEnabled || i !in layout.analysisRange) return 0f
        val r = fineBandsOf(layout, i)
        return r.map { p.bandOffsetsDb.getOrElse(it) { 0f } }.average().toFloat()
    }

    /** Everything that moves band [i]'s threshold besides the usual level and PROCESSING. */
    fun thresholdShiftDb(p: DeEsserParams, layout: Layout, i: Int): Float =
        p.thresholdOffsetDb + bandOffsetDb(p, layout, i)

    /** Moves analysis band [i]'s offset by [deltaDb] (both halves alike in the coarse layout). */
    fun withBandOffsetDelta(p: DeEsserParams, layout: Layout, i: Int, deltaDb: Float): DeEsserParams {
        if (i !in layout.analysisRange) return p
        val r = fineBandsOf(layout, i)
        val list = List(FINE_BAND_COUNT) { k ->
            val v = p.bandOffsetsDb.getOrElse(k) { 0f }
            if (k in r) clampOffset(v + deltaDb) else v
        }
        return p.copy(bandOffsetsDb = list)
    }

    /** Sets analysis band [i]'s offset back to 0. */
    fun withBandOffsetCleared(p: DeEsserParams, layout: Layout, i: Int): DeEsserParams {
        if (i !in layout.analysisRange) return p
        val r = fineBandsOf(layout, i)
        return p.copy(bandOffsetsDb = List(FINE_BAND_COUNT) { k -> if (k in r) 0f else p.bandOffsetsDb.getOrElse(k) { 0f } })
    }

    /** Stored offsets made safe: exactly [FINE_BAND_COUNT] values, finite, within range. */
    fun sanitizeBandOffsets(values: List<Float>?): List<Float> =
        List(FINE_BAND_COUNT) { k -> values?.getOrNull(k)?.takeIf { it.isFinite() }?.let(::clampOffset) ?: 0f }

    /** Sharp = hard knee, soft = 12 dB knee. */
    fun kneeDb(sharpness: Float): Float = lerp(12f, 0f, pct(sharpness))

    /** Sharpness scales each target's base attack/release: soft ×2, sharp ×0.5. */
    fun timeFactor(sharpness: Float): Float = lerp(2f, 0.5f, pct(sharpness))

    /** Higher bands react faster (as soothe2 does): ×1.4 at 2 kHz, ×1 at 4 kHz, ×0.5 at 16 kHz. */
    fun frequencyTimeFactor(centerHz: Float): Float = sqrt(4_000f / centerHz).coerceIn(0.5f, 1.5f)

    /** Range of a target, ordered and clamped to the analysed region. */
    fun focusRange(fp: FocusParams): Pair<Float, Float> {
        val lo = minOf(fp.lowHz, fp.highHz).coerceIn(MIN_FREQ_HZ, MAX_FREQ_HZ)
        val hi = maxOf(fp.lowHz, fp.highHz).coerceIn(MIN_FREQ_HZ, MAX_FREQ_HZ)
        return lo to hi
    }

    /**
     * Analysis bands covered by a target, enabled or not: those whose centre lies in its
     * range, or the single band nearest to the range if it is narrower than a band.
     */
    fun coveredBands(fp: FocusParams, layout: Layout = layout(Resolution.THIRD)): List<Int> {
        val (lo, hi) = focusRange(fp)
        val inside = layout.analysisRange.filter { layout.bandCenter(it) in lo..hi }
        if (inside.isNotEmpty()) return inside
        val mid = sqrt(lo * hi)
        return listOf(layout.analysisRange.minBy { abs(log2(layout.bandCenter(it) / mid)) })
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

    /**
     * Per-MBC-band settings. [references] (dBFS per MBC band) are the measured usual levels
     * when auto level is on; null uses the fixed estimate.
     */
    fun bandSettings(p: DeEsserParams, references: FloatArray? = null): List<BandSetting> {
        val layout = layout(p)
        val claims = HashMap<Int, MutableList<Focus>>()
        for (f in Focus.entries) {
            val fp = p.focus(f)
            if (!fp.enabled) continue
            for (b in coveredBands(fp, layout)) claims.getOrPut(b) { mutableListOf() } += f
        }
        val knee = kneeDb(p.sharpness)
        val tf = timeFactor(p.sharpness)
        return List(layout.mbcBands) { i ->
            val owners = claims[i]
            if (owners.isNullOrEmpty()) {
                BandSetting(layout.cutoffs[i], null, 0f, 1f, 0f, 1f, 50f)
            } else {
                val center = layout.bandCenter(i)
                val ref = references?.getOrNull(i) ?: referenceDb(center, layout.bandsPerOctave)
                // Most sensitive target wins; ties go to the stronger one.
                val owner = owners.minWith(
                    compareBy<Focus> { marginDb(p.focus(it).processing) }
                        .thenByDescending { p.focus(it).intensity },
                )
                val fp = p.focus(owner)
                val ftf = frequencyTimeFactor(center)
                BandSetting(
                    cutoffHz = layout.cutoffs[i],
                    owner = owner,
                    thresholdDb = (ref + marginDb(fp.processing) + thresholdShiftDb(p, layout, i)).coerceIn(-90f, 0f),
                    ratio = ratio(fp.intensity, p.mix),
                    kneeDb = knee,
                    attackMs = owner.baseAttackMs * tf * ftf,
                    releaseMs = owner.baseReleaseMs * tf * ftf,
                )
            }
        }
    }

    /** Post-EQ gains (dB) per MBC band: solo for FILTER listen, plus output gain. */
    fun postEqGains(p: DeEsserParams): FloatArray {
        val layout = layout(p)
        val solo = if (p.listen == ListenMode.FILTER) coveredBands(p.focus(p.selected), layout).toSet() else null
        return FloatArray(layout.mbcBands) { i ->
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
