package app.deesstamer

/** Which part of the spectrum is watched (and reduced) for sibilance / harshness. */
enum class RangeMode(val lowHz: Float, val highHz: Float) {
    LOW_HI(1_000f, 4_000f),
    MID_HI(2_000f, 4_000f),
    HIGH(5_000f, 10_000f),
    HI_END(5_000f, 20_000f),
    CUSTOM(0f, 0f),
}

/** What the user hears. FILTER solos the detection band so it is easy to find the harsh range. */
enum class ListenMode { NORMAL, FILTER }

/**
 * Where the effect is attached. Only one at a time: attaching to both the global mix and a
 * player's own session would process that player twice.
 */
enum class TargetMode {
    /** Audio session 0 (global output mix): every app, if the device allows it. */
    GLOBAL,
    /** Only the sessions that players announce (or open via their equalizer menu). */
    SESSIONS,
}

/**
 * Everything the user can set. Knob values are percentages (0..100) like a plug-in UI;
 * [DspMapping] turns them into compressor numbers.
 */
data class DeEsserParams(
    val processing: Float = 45f,
    val intensity: Float = 60f,
    val sharpness: Float = 50f,
    val mix: Float = 100f,
    val range: RangeMode = RangeMode.HIGH,
    val customLowHz: Float = 6_000f,
    val customHighHz: Float = 12_000f,
    val listen: ListenMode = ListenMode.NORMAL,
    val shelfEnabled: Boolean = false,
    val shelfFreqHz: Float = 8_000f,
    val shelfGainDb: Float = -4f,
    val outputGainDb: Float = 0f,
    val target: TargetMode = TargetMode.GLOBAL,
    val startOnBoot: Boolean = false,
)

data class Preset(val nameRes: Int, val apply: (DeEsserParams) -> DeEsserParams)

object Presets {
    val all: List<Preset> = listOf(
        Preset(R.string.preset_default) { p ->
            DeEsserParams(target = p.target, startOnBoot = p.startOnBoot)
        },
        Preset(R.string.preset_gentle) { p ->
            p.copy(processing = 35f, intensity = 45f, sharpness = 30f, mix = 100f, range = RangeMode.HIGH,
                shelfEnabled = false, listen = ListenMode.NORMAL)
        },
        Preset(R.string.preset_strong) { p ->
            p.copy(processing = 60f, intensity = 80f, sharpness = 65f, mix = 100f, range = RangeMode.HIGH,
                shelfEnabled = false, listen = ListenMode.NORMAL)
        },
        Preset(R.string.preset_cymbals) { p ->
            p.copy(processing = 45f, intensity = 60f, sharpness = 40f, mix = 100f, range = RangeMode.HI_END,
                shelfEnabled = false, listen = ListenMode.NORMAL)
        },
        Preset(R.string.preset_harsh_mids) { p ->
            p.copy(processing = 40f, intensity = 50f, sharpness = 35f, mix = 100f, range = RangeMode.MID_HI,
                shelfEnabled = false, listen = ListenMode.NORMAL)
        },
        Preset(R.string.preset_fatigue) { p ->
            p.copy(processing = 40f, intensity = 55f, sharpness = 40f, mix = 100f, range = RangeMode.HIGH,
                shelfEnabled = true, shelfFreqHz = 9_000f, shelfGainDb = -4f, listen = ListenMode.NORMAL)
        },
    )
}
