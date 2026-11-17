package app.deesstamer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DspMappingTest {

    private val off = FocusParams(false, 2_000f, 4_000f, 50f, 50f)

    @Test
    fun bandEdgesAreThirdOctavesFrom1_6kTo16k() {
        assertEquals(12, DspMapping.MBC_BANDS)
        assertEquals(1_587.4f, DspMapping.MBC_CUTOFFS[0], 0.5f)
        assertEquals(2_000f, DspMapping.MBC_CUTOFFS[1], 0.01f)
        assertEquals(16_000f, DspMapping.MBC_CUTOFFS[10], 0.5f)
        assertEquals(20_000f, DspMapping.MBC_CUTOFFS[11], 0.01f)
        val c = DspMapping.MBC_CUTOFFS
        for (i in 1 until c.size) assertTrue("cutoffs must increase", c[i] > c[i - 1])
        // Each analysis band spans one third of an octave.
        for (i in 1..DspMapping.ANALYSIS_BANDS) {
            assertEquals(2.0.pow(1.0 / 3).toFloat(), DspMapping.bandHigh(i) / DspMapping.bandLow(i), 1e-3f)
        }
    }

    @Test
    fun processingMapsToMarginOverUsualLevel() {
        assertEquals(30f, DspMapping.marginDb(0f), 1e-4f)
        assertEquals(10f, DspMapping.marginDb(50f), 1e-4f)
        assertEquals(-10f, DspMapping.marginDb(100f), 1e-4f)
    }

    @Test
    fun referenceLevelTiltsDownTowardsTreble() {
        assertEquals(-35f, DspMapping.referenceDb(2_000f), 1e-3f)
        assertEquals(-41f, DspMapping.referenceDb(8_000f), 1e-3f)
    }

    @Test
    fun intensityAndMixSetRatio() {
        assertEquals(1f, DspMapping.rawRatio(0f), 1e-4f)
        assertEquals(20f, DspMapping.rawRatio(100f), 1e-3f)
        assertEquals(DspMapping.rawRatio(70f), DspMapping.ratio(70f, 100f), 1e-3f)
        assertEquals(1f, DspMapping.ratio(70f, 0f), 1e-4f)
        assertEquals(1f / 0.525f, DspMapping.ratio(100f, 50f), 1e-3f)
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
        val bands = DspMapping.coveredBands(vocal)
        assertEquals(listOf(6, 7, 8), bands) // centres ≈ 5.66k, 7.13k, 8.98k
        // Range narrower than one band still gets the nearest band.
        val narrow = FocusParams(true, 6_000f, 6_100f, 50f, 50f)
        assertEquals(1, DspMapping.coveredBands(narrow).size)
        // Reversed knobs behave the same as ordered ones.
        assertEquals(bands, DspMapping.coveredBands(vocal.copy(lowHz = 10_000f, highHz = 5_000f)))
    }

    @Test
    fun onlyEnabledTargetsGetBandsAndPassBandsStayFlat() {
        val p = DeEsserParams(guitar = off, hihat = off, vocal = FocusParams(true, 5_000f, 10_000f, 50f, 60f))
        val s = DspMapping.bandSettings(p)
        assertEquals(DspMapping.MBC_BANDS, s.size)
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
        // Threshold = usual level of that band + margin.
        val i = 7
        assertEquals(DspMapping.referenceDb(DspMapping.bandCenter(i)) + 10f, s[i].thresholdDb, 1e-3f)
    }

    @Test
    fun overlappingTargetsUseTheMoreSensitiveOne() {
        val p = DeEsserParams(
            guitar = off,
            vocal = FocusParams(true, 5_000f, 10_000f, processing = 40f, intensity = 50f),
            hihat = FocusParams(true, 8_000f, 16_000f, processing = 70f, intensity = 50f),
        )
        val s = DspMapping.bandSettings(p)
        // Band 8 (≈9 kHz) is claimed by both; hi-hat is more sensitive.
        assertEquals(Focus.HIHAT, s[8].owner)
        assertEquals(Focus.VOCAL, s[6].owner)
        assertEquals(Focus.HIHAT, s[10].owner)
    }

    @Test
    fun reactionTimesFollowTheTargetType() {
        val p = DeEsserParams(sharpness = 50f)
        val s = DspMapping.bandSettings(p)
        val guitarBand = s.first { it.owner == Focus.GUITAR }
        val vocalBand = s.first { it.owner == Focus.VOCAL }
        assertTrue(guitarBand.attackMs > vocalBand.attackMs)
        assertTrue(guitarBand.releaseMs > vocalBand.releaseMs)
        val sharp = DspMapping.bandSettings(p.copy(sharpness = 100f)).first { it.owner == Focus.VOCAL }
        assertTrue(sharp.attackMs < vocalBand.attackMs)
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

    private fun Double.pow(e: Double) = Math.pow(this, e)
}
