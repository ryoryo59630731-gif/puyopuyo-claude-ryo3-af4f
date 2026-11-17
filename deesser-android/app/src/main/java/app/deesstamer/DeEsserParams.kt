package app.deesstamer

/**
 * The three kinds of harsh sound the app goes after. Each one has its own frequency range,
 * sensitivity and strength, and reaction times suited to that kind of sound:
 * sibilance and hi-hats are short bursts (fast attack / release), distorted-guitar fizz is
 * sustained (slower, so it behaves like a dynamic EQ instead of pumping).
 */
enum class Focus(
    val defaultLowHz: Float,
    val defaultHighHz: Float,
    val baseAttackMs: Float,
    val baseReleaseMs: Float,
) {
    GUITAR(2_500f, 5_000f, 4f, 120f),
    VOCAL(5_000f, 10_000f, 1f, 60f),
    HIHAT(8_000f, 16_000f, 0.7f, 45f),
}

data class FocusParams(
    val enabled: Boolean,
    val lowHz: Float,
    val highHz: Float,
    /** 0..100: how readily it reacts (higher = reacts to smaller peaks). */
    val processing: Float,
    /** 0..100: how hard it pushes down once triggered (ratio). */
    val intensity: Float,
) {
    companion object {
        fun defaultFor(f: Focus) = when (f) {
            Focus.GUITAR -> FocusParams(true, f.defaultLowHz, f.defaultHighHz, 45f, 45f)
            Focus.VOCAL -> FocusParams(true, f.defaultLowHz, f.defaultHighHz, 55f, 60f)
            Focus.HIHAT -> FocusParams(true, f.defaultLowHz, f.defaultHighHz, 50f, 55f)
        }
    }
}

/** What the user hears. FILTER solos the selected target's bands to help find the harsh range. */
enum class ListenMode { NORMAL, FILTER }

/**
 * Where the effect is attached. Only one at a time: attaching to both the global mix and a
 * player's own session would process that player twice.
 */
enum class ApplyMode {
    /** Audio session 0 (global output mix): every app, if the device allows it. */
    GLOBAL,
    /** Only the sessions that players announce (or open via their equalizer menu). */
    SESSIONS,
}

data class DeEsserParams(
    val guitar: FocusParams = FocusParams.defaultFor(Focus.GUITAR),
    val vocal: FocusParams = FocusParams.defaultFor(Focus.VOCAL),
    val hihat: FocusParams = FocusParams.defaultFor(Focus.HIHAT),
    /** Target being edited in the UI; FILTER listen solos this one. */
    val selected: Focus = Focus.VOCAL,
    /** 0..100: knee and reaction speed, shared by all targets. */
    val sharpness: Float = 50f,
    /** 0..100: overall amount (approximation, see [DspMapping.ratio]). */
    val mix: Float = 100f,
    val listen: ListenMode = ListenMode.NORMAL,
    val shelfEnabled: Boolean = false,
    val shelfFreqHz: Float = 8_000f,
    val shelfGainDb: Float = -4f,
    val outputGainDb: Float = 0f,
    val applyMode: ApplyMode = ApplyMode.GLOBAL,
    val startOnBoot: Boolean = false,
) {
    fun focus(f: Focus): FocusParams = when (f) {
        Focus.GUITAR -> guitar
        Focus.VOCAL -> vocal
        Focus.HIHAT -> hihat
    }

    fun withFocus(f: Focus, fp: FocusParams): DeEsserParams = when (f) {
        Focus.GUITAR -> copy(guitar = fp)
        Focus.VOCAL -> copy(vocal = fp)
        Focus.HIHAT -> copy(hihat = fp)
    }

    fun updateFocus(f: Focus, transform: (FocusParams) -> FocusParams) = withFocus(f, transform(focus(f)))
}

data class Preset(val nameRes: Int, val apply: (DeEsserParams) -> DeEsserParams)

object Presets {
    /** Keeps device-level choices (apply mode, boot) and resets the sound. */
    private fun base(p: DeEsserParams) = DeEsserParams(applyMode = p.applyMode, startOnBoot = p.startOnBoot)

    private fun fp(f: Focus, on: Boolean, proc: Float, inten: Float, lo: Float = f.defaultLowHz, hi: Float = f.defaultHighHz) =
        FocusParams(on, lo, hi, proc, inten)

    val all: List<Preset> = listOf(
        Preset(R.string.preset_default) { p -> base(p) },
        Preset(R.string.preset_female_vocal) { p ->
            base(p).copy(
                vocal = fp(Focus.VOCAL, true, 65f, 70f, 5_500f, 11_000f),
                hihat = fp(Focus.HIHAT, true, 40f, 45f, 11_000f, 16_000f),
                guitar = fp(Focus.GUITAR, false, 40f, 40f),
                selected = Focus.VOCAL,
            )
        },
        Preset(R.string.preset_hihat) { p ->
            base(p).copy(
                hihat = fp(Focus.HIHAT, true, 65f, 70f, 7_000f, 16_000f),
                vocal = fp(Focus.VOCAL, true, 45f, 50f, 4_500f, 7_000f),
                guitar = fp(Focus.GUITAR, false, 40f, 40f),
                selected = Focus.HIHAT,
            )
        },
        Preset(R.string.preset_guitar) { p ->
            base(p).copy(
                guitar = fp(Focus.GUITAR, true, 60f, 55f, 2_000f, 6_000f),
                vocal = fp(Focus.VOCAL, true, 45f, 50f, 6_000f, 10_000f),
                hihat = fp(Focus.HIHAT, true, 40f, 45f),
                selected = Focus.GUITAR,
            )
        },
        Preset(R.string.preset_gentle) { p ->
            base(p).copy(
                guitar = fp(Focus.GUITAR, true, 35f, 35f),
                vocal = fp(Focus.VOCAL, true, 45f, 45f),
                hihat = fp(Focus.HIHAT, true, 40f, 40f),
                sharpness = 35f,
            )
        },
        Preset(R.string.preset_fatigue) { p ->
            base(p).copy(shelfEnabled = true, shelfFreqHz = 9_000f, shelfGainDb = -3f)
        },
    )
}
