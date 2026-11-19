package app.deesstamer

import android.Manifest
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.audiofx.Visualizer
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.rule.GrantPermissionRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.PI
import kotlin.math.sin

/**
 * Diagnostics for devices whose audio path differs from AOSP (written for the Sony NW-WM1AM2).
 * Nothing here asserts on the effect itself: it only logs (tag "WalkmanDiag").
 *
 * - [visualizerSampleRate]: which sample rate the Visualizer's waveform really has, found from
 *   where two known tones land in its spectrum, compared with the rate it reports.
 * - [rmsReduction]: how much each mode lowers a single tone, measured with the Visualizer's
 *   RMS meter, which does not depend on the sample rate.
 */
@RunWith(AndroidJUnit4::class)
class WalkmanDiagTest {

    @get:Rule
    val permissions: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.RECORD_AUDIO)

    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx: Context get() = instr.targetContext
    private val am get() = ctx.getSystemService(AudioManager::class.java)

    private fun log(msg: String) = Log.i("WalkmanDiag", msg)
    private fun onMain(block: () -> Unit) = instr.runOnMainSync(block)

    private class Tone(val sessionId: Int, val tones: List<Pair<Double, Double>>) {
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
                    var v = 0.0
                    for ((hz, amp) in tones) v += amp * sin(2 * PI * hz * t)
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

    @Test
    fun visualizerSampleRate() {
        val prop = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
        val player = Tone(am.generateAudioSessionId(), listOf(500.0 to 0.3, 7_000.0 to 0.3))
        player.start()
        SystemClock.sleep(2_500)
        val v = Visualizer(0)
        try {
            v.setEnabled(false)
            v.setCaptureSize(1024)
            v.setScalingMode(Visualizer.SCALING_MODE_NORMALIZED)
            v.setEnabled(true)
            val reported = v.samplingRate / 1000f
            val n = 1024
            val power = DoubleArray(n / 2)
            val wave = ByteArray(n)
            var frames = 0
            val end = SystemClock.elapsedRealtime() + 3_000
            while (SystemClock.elapsedRealtime() < end) {
                SystemClock.sleep(60)
                if (v.getWaveForm(wave) != Visualizer.SUCCESS) continue
                val re = FloatArray(n) { ((wave[it].toInt() and 0xFF) - 128).toFloat() }
                if (re.all { it == 0f }) continue
                // Hann window so the two tones show up as clean peaks.
                for (i in 0 until n) re[i] *= (0.5 - 0.5 * kotlin.math.cos(2 * PI * i / (n - 1))).toFloat()
                val im = FloatArray(n)
                LevelAnalyzer.fft(re, im)
                for (k in 1 until n / 2) power[k] += (re[k] * re[k] + im[k] * im[k]).toDouble()
                frames++
            }
            val first = (2 until n / 2).maxByOrNull { power[it] }!!
            val second = (2 until n / 2).filter { kotlin.math.abs(it - first) > 4 }.maxByOrNull { power[it] }!!
            val lo = minOf(first, second).toFloat()
            val hi = maxOf(first, second).toFloat()
            // The two tones are 500 Hz and 7 kHz, so the true rate is n * f / bin for each.
            val fromLo = n * 500f / lo
            val fromHi = n * 7_000f / hi
            log("frames=$frames reportedRate=$reported Hz outputSampleRateProperty=$prop mediaOutputHz=${OutputRate.mediaOutputHz()}")
            log("peaks at bins $lo and $hi (ratio %.2f, expected 14.0)".format(hi / lo))
            log("rate implied by 500 Hz peak=%.0f Hz, by 7 kHz peak=%.0f Hz".format(fromLo, fromHi))
            log("at the reported rate those bins are %.0f Hz and %.0f Hz".format(lo * reported / n, hi * reported / n))
        } finally {
            v.release()
            player.stop()
        }
    }

    private class RmsMeter {
        val v = Visualizer(0).apply {
            setEnabled(false)
            setCaptureSize(1024)
            setMeasurementMode(Visualizer.MEASUREMENT_MODE_PEAK_RMS)
            setEnabled(true)
        }
        private val m = Visualizer.MeasurementPeakRms()
        fun measure(ms: Long): Float {
            var sum = 0f; var n = 0
            val end = SystemClock.elapsedRealtime() + ms
            while (SystemClock.elapsedRealtime() < end) {
                SystemClock.sleep(50)
                if (v.getMeasurementPeakRms(m) == Visualizer.SUCCESS && m.mRms > -9000) { sum += m.mRms / 100f; n++ }
            }
            return if (n > 0) sum / n else Float.NaN
        }
        fun release() = v.release()
    }

    /** Saves `dumpsys media.audio_flinger` (run as the shell user) next to the app's files. */
    private fun saveDump(name: String) {
        val pfd = instr.uiAutomation.executeShellCommand("dumpsys media.audio_flinger")
        val text = android.os.ParcelFileDescriptor.AutoCloseInputStream(pfd).use { it.readBytes() }
        java.io.File(ctx.getExternalFilesDir(null), name).writeBytes(text)
    }

    private fun scenario(label: String, global: Boolean, params: DeEsserParams, hz: Double, amp: Double) {
        val session = am.generateAudioSessionId()
        val target = if (global) 0 else session
        val player = Tone(session, listOf(hz to amp))
        player.start()
        SystemClock.sleep(2_000)
        val engine = EffectEngine()
        val meter = RmsMeter()
        try {
            val off1 = meter.measure(1_500)
            var ok = false
            onMain { ok = engine.attach(target, "diag", params) }
            SystemClock.sleep(800)
            // Where the effect actually sits while it is on (thread, chain order, sample rate).
            saveDump("af_" + label.trim().replace(' ', '_') + ".txt")
            val on = meter.measure(1_500)
            onMain { engine.detachAll() }
            SystemClock.sleep(800)
            val off2 = meter.measure(1_500)
            val reduction = (off1 + off2) / 2 - on
            log("%-28s attached=%s rms off=%.1f on=%.1f off=%.1f dBFS -> reduction %.1f dB".format(
                label, ok, off1, on, off2, reduction))
        } finally {
            meter.release()
            onMain { engine.detachAll() }
            player.stop()
        }
    }

    @Test
    fun rmsReduction() {
        val strong = DeEsserParams(
            guitar = FocusParams.defaultFor(Focus.GUITAR).copy(enabled = false),
            hihat = FocusParams.defaultFor(Focus.HIHAT).copy(enabled = false),
            vocal = FocusParams(true, 5_000f, 10_000f, processing = 100f, intensity = 100f),
            autoLevel = false,
        )
        val default = DeEsserParams()
        scenario("session strong 7k loud", false, strong, 7_000.0, 0.3)
        scenario("session strong 500 loud", false, strong, 500.0, 0.3)
        scenario("session default 7k loud", false, default, 7_000.0, 0.3)
        scenario("session default 7k quiet", false, default, 7_000.0, 0.003)
        scenario("session default 500 loud", false, default, 500.0, 0.3)
        scenario("global strong 7k loud", true, strong, 7_000.0, 0.3)
        scenario("global strong 500 loud", true, strong, 500.0, 0.3)
        scenario("global default 7k loud", true, default, 7_000.0, 0.3)
        scenario("global default 7k quiet", true, default, 7_000.0, 0.003)
    }
}
