package app.deesstamer

import android.media.AudioManager
import android.media.AudioTrack

/**
 * Sample rate of the audio that a Visualizer on the output mix (session 0) really captures.
 *
 * `Visualizer.getSamplingRate()` cannot be trusted: on the Sony NW-WM1AM2 it reports 44.1 kHz
 * while the mix runs at 192 kHz (Hi-Res streaming on) or 48 kHz (off), which put every FFT bin
 * at the wrong frequency. The native rate of the output that media is currently routed to
 * follows the Hi-Res switch, wired / Bluetooth changes and so on, so it is used instead, and
 * the reported value only as a fallback.
 */
object OutputRate {

    /** Native rate (Hz) of the output media currently plays to, or 0 if unknown. */
    fun mediaOutputHz(): Int =
        runCatching { AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC) }.getOrDefault(0)

    /** The rate to analyse with: the output's native rate when plausible, else [reportedHz]. */
    fun pick(nativeHz: Int, reportedHz: Float): Float =
        if (nativeHz in MIN_HZ..MAX_HZ) nativeHz.toFloat() else reportedHz

    private const val MIN_HZ = 8_000
    private const val MAX_HZ = 768_000
}
