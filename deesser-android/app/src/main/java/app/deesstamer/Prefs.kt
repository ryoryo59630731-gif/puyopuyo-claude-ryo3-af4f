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
            processing = sp.getFloat("processing", d.processing),
            intensity = sp.getFloat("intensity", d.intensity),
            sharpness = sp.getFloat("sharpness", d.sharpness),
            mix = sp.getFloat("mix", d.mix),
            range = enumOr(sp.getString("range", null), d.range),
            customLowHz = sp.getFloat("customLowHz", d.customLowHz),
            customHighHz = sp.getFloat("customHighHz", d.customHighHz),
            listen = enumOr(sp.getString("listen", null), d.listen),
            shelfEnabled = sp.getBoolean("shelfEnabled", d.shelfEnabled),
            shelfFreqHz = sp.getFloat("shelfFreqHz", d.shelfFreqHz),
            shelfGainDb = sp.getFloat("shelfGainDb", d.shelfGainDb),
            outputGainDb = sp.getFloat("outputGainDb", d.outputGainDb),
            target = enumOr(sp.getString("target", null), d.target),
            startOnBoot = sp.getBoolean("startOnBoot", d.startOnBoot),
        )
    }

    fun save(p: DeEsserParams) {
        sp.edit()
            .putFloat("processing", p.processing)
            .putFloat("intensity", p.intensity)
            .putFloat("sharpness", p.sharpness)
            .putFloat("mix", p.mix)
            .putString("range", p.range.name)
            .putFloat("customLowHz", p.customLowHz)
            .putFloat("customHighHz", p.customHighHz)
            .putString("listen", p.listen.name)
            .putBoolean("shelfEnabled", p.shelfEnabled)
            .putFloat("shelfFreqHz", p.shelfFreqHz)
            .putFloat("shelfGainDb", p.shelfGainDb)
            .putFloat("outputGainDb", p.outputGainDb)
            .putString("target", p.target.name)
            .putBoolean("startOnBoot", p.startOnBoot)
            .apply()
    }

    private inline fun <reified E : Enum<E>> enumOr(name: String?, fallback: E): E =
        name?.let { n -> enumValues<E>().firstOrNull { it.name == n } } ?: fallback
}
