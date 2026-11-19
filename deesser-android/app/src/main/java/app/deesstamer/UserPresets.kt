package app.deesstamer

/** A preset the user saved under their own name. */
data class UserPreset(val name: String, val params: DeEsserParams)

/**
 * Text format for user presets, kept free of Android classes so it can be unit tested.
 *
 * Each preset is a block of `key=value` lines. Unknown keys are ignored and missing keys keep
 * the current value, so presets saved now still load after new settings are added (and the
 * other way round). Only the sound is stored: where the effect is applied, start-on-boot,
 * auto level (needs a permission) and the Normal / Filter monitor stay as they are.
 */
object PresetCodec {
    private const val VERSION = 1
    // Record separator: a control character, so it never appears in a cleaned name or a value.
    private const val PRESET_SEP = "\n\u001E\n"
    const val MAX_NAME_LENGTH = 40

    /** Name as stored: single line, trimmed, no control characters, limited length. */
    fun cleanName(raw: String): String =
        raw.filter { !it.isISOControl() }.trim().take(MAX_NAME_LENGTH)

    fun encode(p: DeEsserParams): String = buildString {
        fun put(k: String, v: Any) = append(k).append('=').append(v).append('\n')
        put("v", VERSION)
        for (f in Focus.entries) {
            val fp = p.focus(f)
            val k = f.name.lowercase()
            put("$k.enabled", fp.enabled)
            put("$k.low", fp.lowHz)
            put("$k.high", fp.highHz)
            put("$k.processing", fp.processing)
            put("$k.intensity", fp.intensity)
        }
        put("selected", p.selected.name)
        put("sharpness", p.sharpness)
        put("mix", p.mix)
        put("resolution", p.resolution.name)
        put("shelf.enabled", p.shelfEnabled)
        put("shelf.freq", p.shelfFreqHz)
        put("shelf.gain", p.shelfGainDb)
        put("output", p.outputGainDb)
    }.trimEnd()

    /** Applies the stored sound settings on top of [current]. */
    fun decode(text: String, current: DeEsserParams): DeEsserParams {
        val m = text.lineSequence()
            .mapNotNull { line -> line.indexOf('=').takeIf { it > 0 }?.let { line.substring(0, it) to line.substring(it + 1) } }
            .toMap()
        fun f(k: String, d: Float) = m[k]?.toFloatOrNull()?.takeIf { it.isFinite() } ?: d
        fun b(k: String, d: Boolean) = m[k]?.toBooleanStrictOrNull() ?: d
        var p = current
        for (focus in Focus.entries) {
            val k = focus.name.lowercase()
            val d = p.focus(focus)
            p = p.withFocus(
                focus,
                FocusParams(
                    enabled = b("$k.enabled", d.enabled),
                    lowHz = f("$k.low", d.lowHz),
                    highHz = f("$k.high", d.highHz),
                    processing = f("$k.processing", d.processing).coerceIn(0f, 100f),
                    intensity = f("$k.intensity", d.intensity).coerceIn(0f, 100f),
                ),
            )
        }
        return p.copy(
            selected = Focus.entries.firstOrNull { it.name == m["selected"] } ?: p.selected,
            sharpness = f("sharpness", p.sharpness).coerceIn(0f, 100f),
            mix = f("mix", p.mix).coerceIn(0f, 100f),
            resolution = Resolution.entries.firstOrNull { it.name == m["resolution"] } ?: p.resolution,
            shelfEnabled = b("shelf.enabled", p.shelfEnabled),
            shelfFreqHz = f("shelf.freq", p.shelfFreqHz),
            shelfGainDb = f("shelf.gain", p.shelfGainDb),
            outputGainDb = f("output", p.outputGainDb),
        )
    }

    /** The sound of [preset] on top of the [current] device choices. */
    fun applyTo(preset: UserPreset, current: DeEsserParams): DeEsserParams =
        decode(encode(preset.params), current)

    /** Whole list as one string: `name` on the first line of each block, then its settings. */
    fun encodeAll(list: List<UserPreset>): String =
        list.joinToString(PRESET_SEP) { cleanName(it.name) + "\n" + encode(it.params) }

    /** Reads [encodeAll] output; each preset is decoded on top of [base]. Bad blocks are skipped. */
    fun decodeAll(text: String?, base: DeEsserParams): List<UserPreset> {
        if (text.isNullOrBlank()) return emptyList()
        return text.split(PRESET_SEP).mapNotNull { block ->
            val name = cleanName(block.substringBefore('\n'))
            if (name.isEmpty() || !block.contains('\n')) null
            else UserPreset(name, decode(block.substringAfter('\n'), base))
        }
    }

    /** First "base N" not used yet, for the name field's suggestion. */
    fun suggestName(base: String, existing: Collection<String>): String {
        var n = existing.size + 1
        while ("$base $n" in existing) n++
        return "$base $n"
    }
}
