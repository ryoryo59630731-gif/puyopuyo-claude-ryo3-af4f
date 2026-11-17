package app.deesstamer

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DspMappingTest {

    @Test
    fun processingMapsToThreshold() {
        assertEquals(0f, DspMapping.thresholdDb(0f), 1e-4f)
        assertEquals(-30f, DspMapping.thresholdDb(50f), 1e-4f)
        assertEquals(-60f, DspMapping.thresholdDb(100f), 1e-4f)
        assertEquals(-60f, DspMapping.thresholdDb(150f), 1e-4f) // clamped
    }

    @Test
    fun intensityRatioIsExponential() {
        assertEquals(1f, DspMapping.rawRatio(0f), 1e-4f)
        assertEquals(20f, DspMapping.rawRatio(100f), 1e-3f)
        assertEquals(4.472f, DspMapping.rawRatio(50f), 1e-2f)
    }

    @Test
    fun mixScalesReductionInDb() {
        // Full mix keeps the raw ratio, zero mix disables compression.
        assertEquals(DspMapping.rawRatio(70f), DspMapping.ratio(70f, 100f), 1e-3f)
        assertEquals(1f, DspMapping.ratio(70f, 0f), 1e-4f)
        // Half mix at 20:1 -> slope reduction 0.475 -> ratio 1/0.525.
        assertEquals(1f / 0.525f, DspMapping.ratio(100f, 50f), 1e-3f)
    }

    @Test
    fun hardKneeCompressorCurve() {
        // 10 dB over a -20 dB threshold at 2:1 -> 5 dB over.
        assertEquals(-15f, DspMapping.compressorOutputDb(-10f, -20f, 2f, 0f), 1e-4f)
        assertEquals(-30f, DspMapping.compressorOutputDb(-30f, -20f, 2f, 0f), 1e-4f)
    }

    @Test
    fun softKneeIsContinuousAtItsEdges() {
        val t = -20f; val r = 4f; val k = 12f
        val below = DspMapping.compressorOutputDb(t - k / 2f, t, r, k)
        assertEquals(t - k / 2f, below, 1e-3f)
        val above = DspMapping.compressorOutputDb(t + k / 2f, t, r, k)
        assertEquals(t + (k / 2f) / r, above, 1e-3f)
        // Inside the knee the output is between unity and the hard-knee line.
        val mid = DspMapping.compressorOutputDb(t, t, r, k)
        assertTrue(mid < t && mid > t - k)
    }

    @Test
    fun gainReductionIsZeroBelowThreshold() {
        val p = DeEsserParams(processing = 50f, intensity = 60f, sharpness = 100f, mix = 100f)
        assertEquals(0f, DspMapping.gainReductionDb(p, -40f), 1e-4f)
        assertTrue(DspMapping.gainReductionDb(p, -10f) > 0f)
    }

    @Test
    fun presetRangesAndCustomRangeAreOrdered() {
        assertEquals(5_000f to 10_000f, DspMapping.bandEdges(DeEsserParams(range = RangeMode.HIGH)))
        val swapped = DspMapping.bandEdges(
            DeEsserParams(range = RangeMode.CUSTOM, customLowHz = 9_000f, customHighHz = 3_000f),
        )
        assertEquals(3_000f to 9_000f, swapped)
        val tooNarrow = DspMapping.bandEdges(
            DeEsserParams(range = RangeMode.CUSTOM, customLowHz = 6_000f, customHighHz = 6_000f),
        )
        assertTrue(tooNarrow.second >= tooNarrow.first * 1.25f - 1e-3f)
    }

    @Test
    fun hiEndMergesTopBand() {
        val layout = DspMapping.mbcLayout(DeEsserParams(range = RangeMode.HI_END))
        assertArrayEquals(floatArrayOf(5_000f, DspMapping.TOP_MERGE_HZ, DspMapping.TOP_CUTOFF_HZ), layout.cutoffs, 1e-3f)
        assertTrue(layout.compressed[1] && layout.compressed[2] && !layout.compressed[0])

        val high = DspMapping.mbcLayout(DeEsserParams(range = RangeMode.HIGH))
        assertTrue(!high.compressed[2])
        assertTrue(high.cutoffs.toList() == high.cutoffs.sorted())
    }

    @Test
    fun filterListenMutesOutsideBands() {
        val normal = DspMapping.postEqGains(DeEsserParams(listen = ListenMode.NORMAL, outputGainDb = -2f))
        assertArrayEquals(floatArrayOf(-2f, -2f, -2f), normal, 1e-4f)

        val filter = DspMapping.postEqGains(DeEsserParams(listen = ListenMode.FILTER, range = RangeMode.HIGH))
        assertArrayEquals(floatArrayOf(DspMapping.SOLO_MUTE_DB, 0f, DspMapping.SOLO_MUTE_DB), filter, 1e-4f)

        val filterHiEnd = DspMapping.postEqGains(DeEsserParams(listen = ListenMode.FILTER, range = RangeMode.HI_END))
        assertArrayEquals(floatArrayOf(DspMapping.SOLO_MUTE_DB, 0f, 0f), filterHiEnd, 1e-4f)
    }
}
