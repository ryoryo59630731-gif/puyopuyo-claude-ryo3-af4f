package app.deesstamer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.pow

class DspMappingTest {

    private val off = FocusParams(false, 2_000f, 4_000f, 50f, 50f)
    private val third = DspMapping.layout(Resolution.THIRD)
    private val sixth = DspMapping.layout(Resolution.SIXTH)

    @Test
    fun thirdOctaveLayout() {
        assertEquals(10, third.analysisBands)
        assertEquals(12, third.mbcBands)
        assertEquals(1_587.4f, third.cutoffs[0], 0.5f)
        assertEquals(2_000f, third.cutoffs[1], 0.01f)
        assertEquals(16_000f, third.cutoffs[10], 0.5f)
        assertEquals(20_000f, third.cutoffs[11], 0.01f)
        for (i in third.analysisRange) {
            assertEquals(2.0.pow(1.0 / 3).toFloat(), third.bandHigh(i) / third.bandLow(i), 1e-3f)
        }
    }

    @Test
    fun sixthOctaveLayoutCoversSameRegionWithTwiceTheBands() {
        assertEquals(20, sixth.analysisBands)
        assertEquals(22, sixth.mbcBands)
        assertEquals(third.edges.first(), sixth.edges.first(), 0.5f)
        assertEquals(third.edges.last(), sixth.edges.last(), 0.5f)
        for (l in listOf(third, sixth)) {
            for (i in 1 until l.cutoffs.size) assertTrue("cutoffs must increase", l.cutoffs[i] > l.cutoffs[i - 1])
        }
    }

    @Test
    fun processingMapsToMarginOverUsualLevel() {
        assertEquals(30f, DspMapping.marginDb(0f), 1e-4f)
        assertEquals(10f, DspMapping.marginDb(50f), 1e-4f)
        assertEquals(-10f, DspMapping.marginDb(100f), 1e-4f)
    }

    @Test
    fun referenceLevelTiltsDownAndNarrowBandsHoldLessEnergy() {
        assertEquals(-35f, DspMapping.referenceDb(2_000f), 1e-3f)
        assertEquals(-41f, DspMapping.referenceDb(8_000f), 1e-3f)
        assertEquals(-3.01f, DspMapping.modelRelativeDb(4_000f, 6) - DspMapping.modelRelativeDb(4_000f, 3), 0.01f)
        assertEquals(-25f, DspMapping.referenceDb(2_000f, 3, rmsDb = -10f), 1e-3f)
    }

    @Test
    fun intensityMixAndDepth() {
        assertEquals(1f, DspMapping.rawRatio(0f), 1e-4f)
        assertEquals(20f, DspMapping.rawRatio(100f), 1e-3f)
        assertEquals(1f, DspMapping.ratio(70f, 0f), 1e-4f)
        assertEquals(1f / 0.525f, DspMapping.ratio(100f, 50f), 1e-3f)
        assertEquals(0f, DspMapping.estimatedDepthDb(0f, 100f), 1e-4f)
        assertEquals(12f * 0.95f, DspMapping.estimatedDepthDb(100f, 100f), 1e-3f)
        assertEquals(6f, DspMapping.estimatedDepthDb(DspMapping.run { 100f * kotlin.math.ln(2f) / kotlin.math.ln(20f) }, 100f), 1e-3f)
    }

    @Test
    fun compressorCurve() {
        assertEquals(-15f, DspMapping.compressorOutputDb(-10f, -20f, 2f, 0f), 1e-4f)
        assertEquals(-30f, DspMapping.compressorOutputDb(-30f, -20f, 2f, 0f), 1e-4f)
        val t = -20f; val r = 4f; val k = 12f
        assertEquals(t - k / 2f, DspMapping.compressorOutputDb(t - k / 2f, t, r, k), 1e-3f)
        assertEquals(t + (k / 2f) / r, DspMapping.compressorOutputDb(t + k / 2f, t, r, k), 1e-3f)
    }

    @Test
    fun targetCoversBandsWhoseCentreIsInRange() {
        val vocal = FocusParams(true, 5_000f, 10_000f, 50f, 50f)
        assertEquals(listOf(6, 7, 8), DspMapping.coveredBands(vocal, third)) // centres ≈ 5.66k, 7.13k, 8.98k
        assertEquals(6, DspMapping.coveredBands(vocal, sixth).size)
        val narrow = FocusParams(true, 6_000f, 6_100f, 50f, 50f)
        assertEquals(1, DspMapping.coveredBands(narrow, third).size)
        assertEquals(
            DspMapping.coveredBands(vocal, third),
            DspMapping.coveredBands(vocal.copy(lowHz = 10_000f, highHz = 5_000f), third),
        )
    }

    @Test
    fun onlyEnabledTargetsGetBandsAndPassBandsStayFlat() {
        val p = DeEsserParams(guitar = off, hihat = off, vocal = FocusParams(true, 5_000f, 10_000f, 50f, 60f))
        val s = DspMapping.bandSettings(p)
        assertEquals(third.mbcBands, s.size)
        for (i in s.indices) {
            if (i in 6..8) {
                assertEquals(Focus.VOCAL, s[i].owner)
                assertTrue(s[i].ratio > 1f)
            } else {
                assertNull(s[i].owner)
                assertEquals(1f, s[i].ratio, 0f)
                assertEquals(0f, s[i].thresholdDb, 0f)
            }
        }
        assertEquals(DspMapping.referenceDb(third.bandCenter(7)) + 10f, s[7].thresholdDb, 1e-3f)
    }

    @Test
    fun measuredReferencesReplaceTheEstimate() {
        val p = DeEsserParams(guitar = off, hihat = off, vocal = FocusParams(true, 5_000f, 10_000f, 50f, 60f))
        val refs = FloatArray(third.mbcBands) { -50f }
        val s = DspMapping.bandSettings(p, refs)
        assertEquals(-40f, s[7].thresholdDb, 1e-4f)
    }

    @Test
    fun sixthOctaveSettingsHaveMoreBands() {
        val s = DspMapping.bandSettings(DeEsserParams(resolution = Resolution.SIXTH))
        assertEquals(sixth.mbcBands, s.size)
        assertEquals(sixth.mbcBands, DspMapping.postEqGains(DeEsserParams(resolution = Resolution.SIXTH)).size)
    }

    @Test
    fun overlappingTargetsUseTheMoreSensitiveOne() {
        val p = DeEsserParams(
            guitar = off,
            vocal = FocusParams(true, 5_000f, 10_000f, processing = 40f, intensity = 50f),
            hihat = FocusParams(true, 8_000f, 16_000f, processing = 70f, intensity = 50f),
        )
        val s = DspMapping.bandSettings(p)
        assertEquals(Focus.HIHAT, s[8].owner) // ≈9 kHz, claimed by both
        assertEquals(Focus.VOCAL, s[6].owner)
        assertEquals(Focus.HIHAT, s[10].owner)
    }

    @Test
    fun reactionTimesFollowTargetTypeAndFrequency() {
        val p = DeEsserParams(sharpness = 50f)
        val s = DspMapping.bandSettings(p)
        val guitarBand = s.first { it.owner == Focus.GUITAR }
        val vocalBand = s.first { it.owner == Focus.VOCAL }
        assertTrue(guitarBand.attackMs > vocalBand.attackMs)
        assertTrue(guitarBand.releaseMs > vocalBand.releaseMs)
        val sharp = DspMapping.bandSettings(p.copy(sharpness = 100f)).first { it.owner == Focus.VOCAL }
        assertTrue(sharp.attackMs < vocalBand.attackMs)
        // Within one target, higher bands react faster.
        val hihatBands = s.filter { it.owner == Focus.HIHAT }
        assertTrue(hihatBands.last().attackMs < hihatBands.first().attackMs)
        assertEquals(1f, DspMapping.frequencyTimeFactor(4_000f), 1e-4f)
    }

    @Test
    fun filterListenSolosTheSelectedTarget() {
        val normal = DspMapping.postEqGains(DeEsserParams(outputGainDb = -2f))
        assertTrue(normal.all { it == -2f })

        val p = DeEsserParams(listen = ListenMode.FILTER, selected = Focus.VOCAL,
            vocal = FocusParams(true, 5_000f, 10_000f, 50f, 50f))
        val g = DspMapping.postEqGains(p)
        for (i in g.indices) {
            assertEquals(if (i in 6..8) 0f else DspMapping.SOLO_MUTE_DB, g[i], 1e-4f)
        }
    }
}
