package app.deesstamer

import android.Manifest
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.audiofx.DynamicsProcessing
import android.media.audiofx.Visualizer
import android.os.Build
import android.os.SystemClock
import android.util.Log
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.rule.GrantPermissionRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.sin

/**
 * On-device checks of everything that depends on the Android audio framework rather than on
 * pure math: effect creation on the global mix and on a player session, the 12/22-band
 * layouts, that the effect actually lowers a loud 7 kHz tone, the foreground service, the
 * Visualizer-based auto level, and the main screen.
 */
@RunWith(AndroidJUnit4::class)
class DeviceTest {

    @get:Rule
    val permissions: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.RECORD_AUDIO)

    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx: Context get() = instr.targetContext

    private fun log(msg: String) = Log.i("DeviceTest", msg)

    private fun onMain(block: () -> Unit) = instr.runOnMainSync(block)

    @Test
    fun dynamicsProcessingIsAvailable() {
        log("SDK_INT=${Build.VERSION.SDK_INT} release=${Build.VERSION.RELEASE}")
        assertTrue("DynamicsProcessing effect missing", EffectEngine.isSupported())
    }

    @Test
    fun effectCanBeCreatedWithBothLayoutsOnGlobalMix() {
        for (res in Resolution.entries) {
            val p = DeEsserParams(resolution = res, autoLevel = false)
            val settings = DspMapping.bandSettings(p)
            val dp = DynamicsProcessing(0, 0, EffectEngine.buildConfig(p, settings))
            try {
                assertEquals(0, dp.setEnabled(true))
                val mbc = dp.getMbcByChannelIndex(0)
                assertEquals(settings.size, mbc.bandCount)
                // Read back one compressed band to confirm the values arrived.
                val i = settings.indexOfFirst { it.owner != null }
                val band = dp.getMbcBandByChannelIndex(0, i)
                log("res=$res bands=${mbc.bandCount} band$i cutoff=${band.cutoffFrequency} thr=${band.threshold} ratio=${band.ratio}")
                assertEquals(settings[i].thresholdDb, band.threshold, 0.01f)
                assertEquals(settings[i].ratio, band.ratio, 0.01f)
                // Live update path used by auto level.
                dp.setMbcBandAllChannelsTo(i, DynamicsProcessing.MbcBand(band).apply { threshold = -50f })
                assertEquals(-50f, dp.getMbcBandByChannelIndex(0, i).threshold, 0.01f)
            } finally {
                dp.release()
            }
        }
    }

    // ---- does it actually reduce a harsh tone? ----

    private class TonePlayer(val sessionId: Int, val highAmp: Double = 0.3) {
        @Volatile private var running = true
        val track: AudioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
            )
            .setAudioFormat(
                AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(SR).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build(),
            )
            .setSessionId(sessionId)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        private val thread = Thread {
            val frames = 960
            val buf = ShortArray(frames * 2)
            var n = 0L
            track.play()
            while (running) {
                for (f in 0 until frames) {
                    val t = (n + f).toDouble() / SR
                    // A 500 Hz "body" and a loud 7 kHz "sibilant".
                    val v = 0.3 * sin(2 * PI * 500 * t) + highAmp * sin(2 * PI * 7_000 * t)
                    val s = (v * 32767).toInt().toShort()
                    buf[2 * f] = s; buf[2 * f + 1] = s
                }
                n += frames
                track.write(buf, 0, buf.size)
            }
        }
        fun start() = thread.start()
        fun stop() { running = false; thread.join(2_000); track.stop(); track.release() }
        companion object { const val SR = 48_000 }
    }

    /**
     * Measures the global output mix (session 0), i.e. after every player-session effect.
     * Returns (7 kHz over 500 Hz power in dB, broadband RMS dBFS).
     */
    private class MixMeter {
        val v = Visualizer(0).apply {
            setEnabled(false)
            setCaptureSize(1024)
            setScalingMode(Visualizer.SCALING_MODE_NORMALIZED)
            setMeasurementMode(Visualizer.MEASUREMENT_MODE_PEAK_RMS)
            setEnabled(true)
        }
        private val wave = ByteArray(1024)
        private val m = Visualizer.MeasurementPeakRms()
        private val sr = v.samplingRate / 1000f

        fun measure(ms: Long): Pair<Float, Float> {
            var hi = 0.0; var lo = 0.0; var rms = 0f; var nr = 0; var frames = 0
            val end = SystemClock.elapsedRealtime() + ms
            while (SystemClock.elapsedRealtime() < end) {
                SystemClock.sleep(60)
                if (v.getMeasurementPeakRms(m) == Visualizer.SUCCESS && m.mRms > -9000) { rms += m.mRms / 100f; nr++ }
                if (v.getWaveForm(wave) != Visualizer.SUCCESS) continue
                val re = FloatArray(1024) { ((wave[it].toInt() and 0xFF) - 128).toFloat() }
                if (re.all { it == 0f }) continue
                val im = FloatArray(1024)
                LevelAnalyzer.fft(re, im)
                fun p(f: Double): Double {
                    val k = Math.round(f * 1024 / sr).toInt()
                    return (k - 2..k + 2).sumOf { (re[it] * re[it] + im[it] * im[it]).toDouble() }
                }
                hi += p(7_000.0); lo += p(500.0); frames++
            }
            assertTrue("no audio captured on the output mix", frames > 3 && lo > 0)
            return (10 * log10(hi / lo)).toFloat() to (if (nr > 0) rms / nr else Float.NaN)
        }

        fun release() = v.release()
    }

    @Test
    fun effectLowersALoud7kHzToneOnAPlayerSession() {
        val am = ctx.getSystemService(AudioManager::class.java)
        val session = am.generateAudioSessionId()
        val player = TonePlayer(session)
        player.start()
        SystemClock.sleep(3_000)
        val engine = EffectEngine()
        val p = DeEsserParams(
            guitar = FocusParams.defaultFor(Focus.GUITAR).copy(enabled = false),
            hihat = FocusParams.defaultFor(Focus.HIHAT).copy(enabled = false),
            vocal = FocusParams(true, 5_000f, 10_000f, processing = 100f, intensity = 100f),
            autoLevel = false,
        )
        val meter = MixMeter()
        try {
            val off1 = meter.measure(2_500)
            onMain { assertTrue(engine.attach(session, "test", p)) }
            SystemClock.sleep(800)
            val on = meter.measure(2_500)
            onMain { engine.detachAll() }
            SystemClock.sleep(800)
            val off2 = meter.measure(2_500)
            log("7k/500 off=%.1f on=%.1f off=%.1f dB | rms off=%.1f on=%.1f off=%.1f".format(
                off1.first, on.first, off2.first, off1.second, on.second, off2.second))
            val reduction = (off1.first + off2.first) / 2 - on.first
            assertTrue("expected the 7 kHz tone to drop by >6 dB, got $reduction", reduction > 6f)
        } finally {
            meter.release()
            onMain { engine.detachAll() }
            player.stop()
        }
    }

    /** Default settings: a quiet 7 kHz tone is left alone, a loud one is pulled down. */
    @Test
    fun defaultSettingsReactOnlyToLoudHighs() {
        val am = ctx.getSystemService(AudioManager::class.java)
        val results = mutableMapOf<Double, Float>()
        for (amp in listOf(0.003, 0.3)) {
            val session = am.generateAudioSessionId()
            val player = TonePlayer(session, amp)
            player.start()
            SystemClock.sleep(2_500)
            val engine = EffectEngine()
            val meter = MixMeter()
            try {
                val off = meter.measure(2_000)
                onMain { assertTrue(engine.attach(session, "test", DeEsserParams())) }
                SystemClock.sleep(800)
                val on = meter.measure(2_000)
                results[amp] = off.first - on.first
            } finally {
                meter.release()
                onMain { engine.detachAll() }
                player.stop()
            }
        }
        log("default settings reduction: quiet=%.1f dB loud=%.1f dB".format(results[0.003], results[0.3]))
        assertTrue("quiet highs should be left alone", results.getValue(0.003) < 2f)
        assertTrue("loud highs should be reduced", results.getValue(0.3) > 3f)
    }

    /** With nothing to compress (all targets off, 0 dB gains) turning the effect on must not change loudness. */
    @Test
    fun neutralEffectKeepsLoudness() {
        val am = ctx.getSystemService(AudioManager::class.java)
        val session = am.generateAudioSessionId()
        val player = TonePlayer(session)
        player.start()
        SystemClock.sleep(3_000)
        val engine = EffectEngine()
        val off = FocusParams(false, 2_000f, 4_000f, 50f, 50f)
        val meter = MixMeter()
        try {
            val a = meter.measure(2_000)
            onMain { assertTrue(engine.attach(session, "test", DeEsserParams(guitar = off, vocal = off, hihat = off))) }
            SystemClock.sleep(800)
            val b = meter.measure(2_000)
            log("neutral: rms off=%.1f on=%.1f, 7k/500 off=%.1f on=%.1f".format(a.second, b.second, a.first, b.first))
            assertEquals("loudness changed when enabling a neutral effect", a.second, b.second, 1.5f)
            assertEquals("spectrum changed when enabling a neutral effect", a.first, b.first, 1.5f)
        } finally {
            meter.release()
            onMain { engine.detachAll() }
            player.stop()
        }
    }

    // ---- service, auto level and the screen ----

    @Test
    fun serviceAttachesToGlobalMixAndAutoLevelRuns() {
        val am = ctx.getSystemService(AudioManager::class.java)
        val player = TonePlayer(am.generateAudioSessionId())
        player.start()
        try {
            onMain {
                AppState.init(ctx)
                AppState.update { it.copy(applyMode = ApplyMode.GLOBAL, autoLevel = true) }
                AppState.prefs.powerOn = true
            }
            ctx.startForegroundService(Intent(ctx, DeEsserService::class.java))
            var waited = 0
            while (waited < 20_000 && !(AppState.serviceRunning && AppState.engine.has(0) &&
                    AppState.autoStatus != AppState.AutoStatus.OFF)) {
                SystemClock.sleep(200); waited += 200
            }
            log("service=${AppState.serviceRunning} sessions=${AppState.engine.sessions()} errors=${AppState.engine.errors} auto=${AppState.autoStatus} ${AppState.autoError}")
            assertTrue("service not running", AppState.serviceRunning)
            assertTrue("global effect not attached: ${AppState.engine.errors}", AppState.engine.has(0))
            assertEquals(AppState.AutoStatus.RUNNING, AppState.autoStatus)
            // Let auto level learn for a few seconds and check it produced live data.
            SystemClock.sleep(6_000)
            val live = AppState.live
            log("learned=${AppState.autoLearnedSeconds}s live=${live?.joinToString { "%.1f".format(it) }}")
            assertNotNull("no live data from Visualizer", live)
            assertTrue("auto level did not learn", AppState.autoLearnedSeconds > 1f)
            // Switch to the 1/6-octave layout while running: effect must be recreated, not lost.
            onMain { AppState.update { it.copy(resolution = Resolution.SIXTH) } }
            SystemClock.sleep(1_500)
            assertTrue("effect lost after layout change: ${AppState.engine.errors}", AppState.engine.has(0))
            // Per-player mode detaches the global effect.
            onMain { AppState.update { it.copy(applyMode = ApplyMode.SESSIONS, resolution = Resolution.THIRD) } }
            SystemClock.sleep(500)
            assertTrue(!AppState.engine.has(0))
        } finally {
            onMain { AppState.update { it.copy(applyMode = ApplyMode.GLOBAL, autoLevel = false) } }
            ctx.stopService(Intent(ctx, DeEsserService::class.java))
            player.stop()
        }
    }

    @Test
    fun mainScreenOpensAndScreenshot() {
        onMain { AppState.init(ctx) }
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            SystemClock.sleep(4_000)
            scenario.onActivity { a -> assertTrue(a.findViewById<android.view.View>(R.id.band).isShown) }
            val bmp = instr.uiAutomation.takeScreenshot()
            assertNotNull(bmp)
            val out = File(ctx.getExternalFilesDir(null), "main_api${Build.VERSION.SDK_INT}.png")
            out.outputStream().use { bmp!!.compress(Bitmap.CompressFormat.PNG, 100, it) }
            log("screenshot ${out.absolutePath}")
        }
    }
}
