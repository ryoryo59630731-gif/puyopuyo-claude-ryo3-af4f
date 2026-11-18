package app.deesstamer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.random.Random

class LevelAnalyzerTest {

    private val layout = DspMapping.layout(Resolution.THIRD)
    private val sr = 48_000f
    private val n = 1024

    /** Unsigned 8-bit waveform like Android's Visualizer delivers. */
    private fun wave(sample: (Int) -> Double): ByteArray =
        ByteArray(n) { k -> ((sample(k) * 127.0).roundToInt().coerceIn(-128, 127) + 128).toByte() }

    private fun bandOf(freq: Float) = layout.analysisRange.first { freq >= layout.bandLow(it) && freq < layout.bandHigh(it) }

    @Test
    fun fftFindsASine() {
        val re = FloatArray(n) { k -> sin(2 * PI * 64 * k / n).toFloat() }
        val im = FloatArray(n)
        LevelAnalyzer.fft(re, im)
        val peak = (1 until n / 2).maxBy { hypot(re[it], im[it]) }
        assertEquals(64, peak)
    }

    @Test
    fun loudBandRisesAboveItsUsualLevel() {
        val a = LevelAnalyzer(layout)
        val rnd = Random(1)
        val noise = { _: Int -> rnd.nextDouble(-0.3, 0.3) }
        // Learn on noise for a while.
        repeat(200) { a.process(wave(noise), sr, -20f, 0.05f) }
        assertTrue(a.learnedSeconds > 9f)
        // Then a burst of 7 kHz on top (a "sibilant").
        val burst = { k: Int -> rnd.nextDouble(-0.2, 0.2) + 0.6 * sin(2 * PI * 7_000.0 * k / sr) }
        repeat(3) { a.process(wave(burst), sr, -14f, 0.05f) }
        val live = a.liveOvershoot()
        val b = bandOf(7_000f)
        val others = layout.analysisRange.filter { kotlin.math.abs(it - b) > 1 }
        assertTrue("burst band should stick out", live[b] > 10f)
        assertTrue("burst band should be the highest", others.all { live[it] < live[b] - 6f })
    }

    @Test
    fun silenceIsIgnored() {
        val a = LevelAnalyzer(layout)
        val before = a.references()
        repeat(100) { a.process(wave { 0.0 }, sr, -90f, 0.05f) }
        assertEquals(0f, a.learnedSeconds, 0f)
        assertTrue(before.contentEquals(a.references()))
        assertTrue(a.liveOvershoot()[5].isNaN())
    }

    @Test
    fun referencesFollowLevelAndStayNearTheModel() {
        val a = LevelAnalyzer(layout)
        val tone = { k: Int -> 0.8 * sin(2 * PI * 3_000.0 * k / sr) } // all energy in one band
        repeat(1_000) { a.process(wave(tone), sr, -10f, 0.05f) }
        assertEquals(-10f, a.longRmsDb, 0.5f)
        val refs = a.references()
        for (i in layout.analysisRange) {
            val model = DspMapping.modelRelativeDb(layout.bandCenter(i), 3)
            val rel = refs[i] - a.longRmsDb
            assertTrue("band $i stays within ±12 dB of the model", rel >= model - 12.01f && rel <= model + 12.01f)
        }
    }

    @Test
    fun changingResolutionResets() {
        val a = LevelAnalyzer(layout)
        repeat(50) { a.process(wave { k -> 0.5 * sin(2 * PI * 5_000.0 * k / sr) }, sr, -15f, 0.05f) }
        a.layout = DspMapping.layout(Resolution.SIXTH)
        assertEquals(0f, a.learnedSeconds, 0f)
        assertEquals(DspMapping.layout(Resolution.SIXTH).mbcBands, a.references().size)
    }
}
