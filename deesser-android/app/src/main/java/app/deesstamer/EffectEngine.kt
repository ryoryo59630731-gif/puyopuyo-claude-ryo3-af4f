package app.deesstamer

import android.media.audiofx.AudioEffect
import android.media.audiofx.DynamicsProcessing
import android.util.Log

/**
 * Owns one DynamicsProcessing effect per audio session.
 *
 * Session 0 is the global output mix ("apply to everything"). Whether a device still lets a
 * normal app attach to it depends on the manufacturer's audio stack, so failures are reported
 * rather than hidden. Positive session ids come from music players that announce their
 * sessions with ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION.
 *
 * Signal chain inside each effect:
 *   pre-EQ (static high shelf) -> 12-band MBC (per-target harshness control)
 *   -> post-EQ (listen solo + output gain)
 */
class EffectEngine {

    data class SessionInfo(
        val sessionId: Int,
        val packageName: String?,
        val hasControl: Boolean,
    )

    private class Slot(
        val effect: DynamicsProcessing,
        val packageName: String?,
        var hasControl: Boolean,
    )

    private val slots = LinkedHashMap<Int, Slot>()

    /** Last error per session id (kept after the slot is gone so the UI can show it). */
    val errors = LinkedHashMap<Int, String>()

    var onChanged: (() -> Unit)? = null

    fun has(sessionId: Int) = slots.containsKey(sessionId)

    fun sessions(): List<SessionInfo> =
        slots.map { (id, s) -> SessionInfo(id, s.packageName, s.hasControl) }

    /** Parameters most recently pushed, re-applied when we regain control of an effect. */
    private var lastParams: DeEsserParams? = null

    fun attach(sessionId: Int, packageName: String?, params: DeEsserParams): Boolean {
        lastParams = params
        if (slots.containsKey(sessionId)) {
            applyTo(sessionId, params)
            return true
        }
        return try {
            val effect = DynamicsProcessing(PRIORITY, sessionId, buildConfig(params))
            val slot = Slot(effect, packageName, effect.hasControl())
            effect.setControlStatusListener { _, granted ->
                slot.hasControl = granted
                if (granted) lastParams?.let { applyTo(sessionId, it) }
                onChanged?.invoke()
            }
            effect.setEnabled(true)
            slots[sessionId] = slot
            errors.remove(sessionId)
            onChanged?.invoke()
            true
        } catch (t: Throwable) {
            Log.w(TAG, "attach($sessionId) failed", t)
            errors[sessionId] = t.javaClass.simpleName + (t.message?.let { ": $it" } ?: "")
            onChanged?.invoke()
            false
        }
    }

    fun detach(sessionId: Int) {
        slots.remove(sessionId)?.let { slot ->
            runCatching { slot.effect.setEnabled(false) }
            runCatching { slot.effect.release() }
        }
        onChanged?.invoke()
    }

    fun detachAll(predicate: (Int) -> Boolean = { true }) {
        slots.keys.filter(predicate).forEach { detach(it) }
    }

    /** Push new parameters to every live effect. Effects that died are recreated once. */
    fun applyAll(params: DeEsserParams) {
        lastParams = params
        for (id in slots.keys.toList()) applyTo(id, params)
    }

    private fun applyTo(sessionId: Int, params: DeEsserParams) {
        val slot = slots[sessionId] ?: return
        // Another app with higher priority owns this effect engine; writes would be rejected.
        if (!slot.hasControl) return
        try {
            slot.effect.setPreEqAllChannelsTo(buildPreEq(params))
            slot.effect.setMbcAllChannelsTo(buildMbc(params))
            slot.effect.setPostEqAllChannelsTo(buildPostEq(params))
            if (!slot.effect.enabled) slot.effect.setEnabled(true)
        } catch (t: Throwable) {
            // Typically the audio server restarted and the handle is dead: rebuild it.
            Log.w(TAG, "apply($sessionId) failed, recreating", t)
            slots.remove(sessionId)
            runCatching { slot.effect.release() }
            attach(sessionId, slot.packageName, params)
        }
    }

    companion object {
        private const val TAG = "EffectEngine"

        /** Above-normal priority so we keep control when other apps open the same session. */
        private const val PRIORITY = 1000

        private const val CHANNELS = 2
        private const val PRE_EQ_BANDS = 2
        private const val MBC_BANDS = DspMapping.MBC_BANDS
        private const val POST_EQ_BANDS = DspMapping.MBC_BANDS

        fun isSupported(): Boolean = runCatching {
            AudioEffect.queryEffects()?.any { it.type == AudioEffect.EFFECT_TYPE_DYNAMICS_PROCESSING } == true
        }.getOrDefault(false)

        fun buildConfig(p: DeEsserParams): DynamicsProcessing.Config =
            DynamicsProcessing.Config.Builder(
                DynamicsProcessing.VARIANT_FAVOR_TIME_RESOLUTION,
                CHANNELS,
                true, PRE_EQ_BANDS,
                true, MBC_BANDS,
                true, POST_EQ_BANDS,
                false,
            )
                .setPreferredFrameDuration(10f)
                .setPreEqAllChannelsTo(buildPreEq(p))
                .setMbcAllChannelsTo(buildMbc(p))
                .setPostEqAllChannelsTo(buildPostEq(p))
                .build()

        /** Static high shelf: flat below [DeEsserParams.shelfFreqHz], fixed cut above it. */
        fun buildPreEq(p: DeEsserParams): DynamicsProcessing.Eq {
            val eq = DynamicsProcessing.Eq(true, true, PRE_EQ_BANDS)
            val shelfFreq = p.shelfFreqHz.coerceIn(1_000f, DspMapping.MAX_FREQ_HZ)
            val gain = if (p.shelfEnabled) p.shelfGainDb else 0f
            eq.setBand(0, DynamicsProcessing.EqBand(true, shelfFreq, 0f))
            eq.setBand(1, DynamicsProcessing.EqBand(true, DspMapping.TOP_CUTOFF_HZ, gain))
            return eq
        }

        fun buildMbc(p: DeEsserParams): DynamicsProcessing.Mbc {
            val mbc = DynamicsProcessing.Mbc(true, true, MBC_BANDS)
            DspMapping.bandSettings(p).forEachIndexed { i, b ->
                // Pass-through bands have ratio 1 and a 0 dBFS threshold.
                mbc.setBand(
                    i,
                    DynamicsProcessing.MbcBand(
                        true, b.cutoffHz, b.attackMs, b.releaseMs, b.ratio, b.thresholdDb, b.kneeDb,
                        -90f, 1f, 0f, 0f,
                    ),
                )
            }
            return mbc
        }

        fun buildPostEq(p: DeEsserParams): DynamicsProcessing.Eq {
            val gains = DspMapping.postEqGains(p)
            val eq = DynamicsProcessing.Eq(true, true, POST_EQ_BANDS)
            for (i in 0 until POST_EQ_BANDS) {
                eq.setBand(i, DynamicsProcessing.EqBand(true, DspMapping.MBC_CUTOFFS[i], gains[i]))
            }
            return eq
        }
    }
}
