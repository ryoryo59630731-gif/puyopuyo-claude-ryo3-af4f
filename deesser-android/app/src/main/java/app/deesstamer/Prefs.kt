package app.deesstamer

import android.content.Context
import android.content.SharedPreferences

/** Saves and loads [DeEsserParams] plus the "power" switch. */
class Prefs(context: Context) {
    private val sp: SharedPreferences =
        context.applicationContext.getSharedPreferences("deesser", Context.MODE_PRIVATE)

    var powerOn: Boolean
        get() = sp.getBoolean("power", false)
        set(v) = sp.edit().putBoolean("power", v).apply()

    fun load(): DeEsserParams {
        val d = DeEsserParams()
        return DeEsserParams(
            guitar = loadFocus(Focus.GUITAR, d.guitar),
            vocal = loadFocus(Focus.VOCAL, d.vocal),
            hihat = loadFocus(Focus.HIHAT, d.hihat),
            selected = enumOr(sp.getString("selected", null), d.selected),
            sharpness = sp.getFloat("sharpness", d.sharpness),
            mix = sp.getFloat("mix", d.mix),
            listen = enumOr(sp.getString("listen", null), d.listen),
            resolution = enumOr(sp.getString("resolution", null), d.resolution),
            autoLevel = sp.getBoolean("autoLevel", d.autoLevel),
            shelfEnabled = sp.getBoolean("shelfEnabled", d.shelfEnabled),
            shelfFreqHz = sp.getFloat("shelfFreqHz", d.shelfFreqHz),
            shelfGainDb = sp.getFloat("shelfGainDb", d.shelfGainDb),
            outputGainDb = sp.getFloat("outputGainDb", d.outputGainDb),
            // Key kept from v1 ("target") so the apply-mode choice survives the update.
            applyMode = enumOr(sp.getString("target", null), d.applyMode),
            startOnBoot = sp.getBoolean("startOnBoot", d.startOnBoot),
        )
    }

    fun save(p: DeEsserParams) {
        val e = sp.edit()
        for (f in Focus.entries) saveFocus(e, f, p.focus(f))
        e.putString("selected", p.selected.name)
            .putFloat("sharpness", p.sharpness)
            .putFloat("mix", p.mix)
            .putString("listen", p.listen.name)
            .putString("resolution", p.resolution.name)
            .putBoolean("autoLevel", p.autoLevel)
            .putBoolean("shelfEnabled", p.shelfEnabled)
            .putFloat("shelfFreqHz", p.shelfFreqHz)
            .putFloat("shelfGainDb", p.shelfGainDb)
            .putFloat("outputGainDb", p.outputGainDb)
            .putString("target", p.applyMode.name)
            .putBoolean("startOnBoot", p.startOnBoot)
            .apply()
    }

    private fun key(f: Focus, name: String) = "focus_${f.name}_$name"

    private fun loadFocus(f: Focus, d: FocusParams) = FocusParams(
        enabled = sp.getBoolean(key(f, "enabled"), d.enabled),
        lowHz = sp.getFloat(key(f, "low"), d.lowHz),
        highHz = sp.getFloat(key(f, "high"), d.highHz),
        processing = sp.getFloat(key(f, "processing"), d.processing),
        intensity = sp.getFloat(key(f, "intensity"), d.intensity),
    )

    private fun saveFocus(e: SharedPreferences.Editor, f: Focus, fp: FocusParams) {
        e.putBoolean(key(f, "enabled"), fp.enabled)
            .putFloat(key(f, "low"), fp.lowHz)
            .putFloat(key(f, "high"), fp.highHz)
            .putFloat(key(f, "processing"), fp.processing)
            .putFloat(key(f, "intensity"), fp.intensity)
    }

    private inline fun <reified E : Enum<E>> enumOr(name: String?, fallback: E): E =
        name?.let { n -> enumValues<E>().firstOrNull { it.name == n } } ?: fallback
}
