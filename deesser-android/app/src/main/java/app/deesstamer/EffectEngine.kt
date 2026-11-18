package app.deesstamer

import android.media.audiofx.AudioEffect
import android.media.audiofx.DynamicsProcessing
import android.util.Log
import kotlin.math.abs

/**
 * Owns one DynamicsProcessing effect per audio session.
 *
 * Session 0 is the global output mix ("apply to everything"). Whether a device still lets a
 * normal app attach to it depends on the manufacturer's audio stack, so failures are reported
 * rather than hidden. Positive session ids come from music players that announce their
 * sessions with ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION.
 *
 * Signal chain inside each effect:
 *   pre-EQ (static high shelf) -> MBC (per-band harshness control, 12 or 22 bands)
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
        /** Band count is fixed when the effect is created; a different layout needs a new effect. */
        val mbcBands: Int,
    )

    private val slots = LinkedHashMap<Int, Slot>()

    /** Last error per session id (kept after the slot is gone so the UI can show it). */
    val errors = LinkedHashMap<Int, String>()

    var onChanged: (() -> Unit)? = null

    /** Parameters most recently pushed, re-applied when we regain control of an effect. */
    private var lastParams: DeEsserParams? = null

    /** Measured usual band levels (auto level), or null for the fixed estimate. */
    private var references: FloatArray? = null

    /** Thresholds last written per session, to send only bands that moved. */
    private val sentThresholds = HashMap<Int, FloatArray>()

    fun has(sessionId: Int) = slots.containsKey(sessionId)

    fun sessions(): List<SessionInfo> =
        slots.map { (id, s) -> SessionInfo(id, s.packageName, s.hasControl) }

    private fun refsFor(p: DeEsserParams): FloatArray? =
        references?.takeIf { it.size == DspMapping.layout(p).mbcBands }

    fun attach(sessionId: Int, packageName: String?, params: DeEsserParams): Boolean {
        lastParams = params
        if (slots.containsKey(sessionId)) {
            applyTo(sessionId, params)
            return true
        }
        return try {
            val settings = DspMapping.bandSettings(params, refsFor(params))
            val effect = DynamicsProcessing(PRIORITY, sessionId, buildConfig(params, settings))
            val slot = Slot(effect, packageName, effect.hasControl(), settings.size)
            effect.setControlStatusListener { _, granted ->
                slot.hasControl = granted
                if (granted) lastParams?.let { applyTo(sessionId, it) }
                onChanged?.invoke()
            }
            effect.setEnabled(true)
            slots[sessionId] = slot
            sentThresholds[sessionId] = FloatArray(settings.size) { settings[it].thresholdDb }
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
        sentThresholds.remove(sessionId)
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

    /**
     * New measured reference levels (or null to go back to the fixed estimate). Only the MBC
     * bands whose threshold moved by more than [MIN_THRESHOLD_STEP_DB] are rewritten, to keep
     * parameter traffic to the audio server low.
     */
    fun setReferences(refs: FloatArray?) {
        references = refs
        val p = lastParams ?: return
        val settings = DspMapping.bandSettings(p, refsFor(p))
        for ((id, slot) in slots.toList()) {
            if (!slot.hasControl || slot.mbcBands != settings.size) continue
            val sent = sentThresholds.getOrPut(id) { FloatArray(settings.size) }
            try {
                for (i in settings.indices) {
                    if (abs(settings[i].thresholdDb - sent[i]) >= MIN_THRESHOLD_STEP_DB) {
                        slot.effect.setMbcBandAllChannelsTo(i, mbcBand(settings[i]))
                        sent[i] = settings[i].thresholdDb
                    }
                }
            } catch (t: Throwable) {
                Log.w(TAG, "setReferences($id) failed", t)
                applyTo(id, p)
            }
        }
    }

    private fun applyTo(sessionId: Int, params: DeEsserParams) {
        val slot = slots[sessionId] ?: return
        // Another app with higher priority owns this effect engine; writes would be rejected.
        if (!slot.hasControl) return
        val settings = DspMapping.bandSettings(params, refsFor(params))
        if (slot.mbcBands != settings.size) {
            recreate(sessionId, slot, params)
            return
        }
        try {
            slot.effect.setPreEqAllChannelsTo(buildPreEq(params))
            slot.effect.setMbcAllChannelsTo(buildMbc(settings))
            slot.effect.setPostEqAllChannelsTo(buildPostEq(params))
            sentThresholds[sessionId] = FloatArray(settings.size) { settings[it].thresholdDb }
            if (!slot.effect.enabled) slot.effect.setEnabled(true)
        } catch (t: Throwable) {
            // Typically the audio server restarted and the handle is dead: rebuild it.
            Log.w(TAG, "apply($sessionId) failed, recreating", t)
            recreate(sessionId, slot, params)
        }
    }

    private fun recreate(sessionId: Int, slot: Slot, params: DeEsserParams) {
        slots.remove(sessionId)
        sentThresholds.remove(sessionId)
        runCatching { slot.effect.setEnabled(false) }
        runCatching { slot.effect.release() }
        attach(sessionId, slot.packageName, params)
    }

    companion object {
        private const val TAG = "EffectEngine"

        /** Above-normal priority so we keep control when other apps open the same session. */
        private const val PRIORITY = 1000

        private const val CHANNELS = 2
        private const val PRE_EQ_BANDS = 2
        private const val MIN_THRESHOLD_STEP_DB = 0.5f

        fun isSupported(): Boolean = runCatching {
            AudioEffect.queryEffects()?.any { it.type == AudioEffect.EFFECT_TYPE_DYNAMICS_PROCESSING } == true
        }.getOrDefault(false)

        fun buildConfig(p: DeEsserParams, settings: List<DspMapping.BandSetting>): DynamicsProcessing.Config =
            DynamicsProcessing.Config.Builder(
                // The AOSP implementation only accepts the frequency-resolution variant
                // ("variant 1 not available for creation" on Android 11); timing is set by
                // the preferred frame duration below.
                DynamicsProcessing.VARIANT_FAVOR_FREQUENCY_RESOLUTION,
                CHANNELS,
                true, PRE_EQ_BANDS,
                true, settings.size,
                true, settings.size,
                false,
            )
                .setPreferredFrameDuration(10f)
                .setPreEqAllChannelsTo(buildPreEq(p))
                .setMbcAllChannelsTo(buildMbc(settings))
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

        /** Pass-through bands have ratio 1 and a 0 dBFS threshold. */
        private fun mbcBand(b: DspMapping.BandSetting) = DynamicsProcessing.MbcBand(
            true, b.cutoffHz, b.attackMs, b.releaseMs, b.ratio, b.thresholdDb, b.kneeDb,
            -90f, 1f, 0f, 0f,
        )

        fun buildMbc(settings: List<DspMapping.BandSetting>): DynamicsProcessing.Mbc {
            val mbc = DynamicsProcessing.Mbc(true, true, settings.size)
            settings.forEachIndexed { i, b -> mbc.setBand(i, mbcBand(b)) }
            return mbc
        }

        fun buildPostEq(p: DeEsserParams): DynamicsProcessing.Eq {
            val layout = DspMapping.layout(p)
            val gains = DspMapping.postEqGains(p)
            val eq = DynamicsProcessing.Eq(true, true, layout.mbcBands)
            for (i in 0 until layout.mbcBands) {
                eq.setBand(i, DynamicsProcessing.EqBand(true, layout.cutoffs[i], gains[i]))
            }
            return eq
        }
    }
}
