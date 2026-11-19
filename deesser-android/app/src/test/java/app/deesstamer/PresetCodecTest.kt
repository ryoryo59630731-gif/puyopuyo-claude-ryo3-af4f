package app.deesstamer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PresetCodecTest {

    private val custom = DeEsserParams(
        guitar = FocusParams(false, 2_200f, 4_800f, 33f, 44f),
        vocal = FocusParams(true, 5_500f, 11_000f, 66.5f, 77f),
        hihat = FocusParams(true, 9_000f, 15_000f, 12f, 88f),
        selected = Focus.HIHAT,
        sharpness = 70f,
        mix = 80f,
        resolution = Resolution.SIXTH,
        shelfEnabled = true,
        shelfFreqHz = 9_500f,
        shelfGainDb = -6.5f,
        outputGainDb = 1.5f,
    )

    @Test
    fun soundRoundTripsAndDeviceChoicesStay() {
        val current = DeEsserParams(
            applyMode = ApplyMode.SESSIONS, startOnBoot = true, autoLevel = true, listen = ListenMode.FILTER,
        )
        val p = PresetCodec.applyTo(UserPreset("x", custom), current)
        // Sound comes from the preset...
        assertEquals(custom.guitar, p.guitar)
        assertEquals(custom.vocal, p.vocal)
        assertEquals(custom.hihat, p.hihat)
        assertEquals(custom.selected, p.selected)
        assertEquals(custom.sharpness, p.sharpness, 0f)
        assertEquals(custom.mix, p.mix, 0f)
        assertEquals(custom.resolution, p.resolution)
        assertEquals(custom.shelfEnabled, p.shelfEnabled)
        assertEquals(custom.shelfFreqHz, p.shelfFreqHz, 0f)
        assertEquals(custom.shelfGainDb, p.shelfGainDb, 0f)
        assertEquals(custom.outputGainDb, p.outputGainDb, 0f)
        // ...where / how it runs stays as it was.
        assertEquals(ApplyMode.SESSIONS, p.applyMode)
        assertTrue(p.startOnBoot)
        assertTrue(p.autoLevel)
        assertEquals(ListenMode.FILTER, p.listen)
    }

    @Test
    fun listRoundTripsWithOrderAndJapaneseNames() {
        val list = listOf(
            UserPreset("女性ボーカル用", custom),
            UserPreset("やさしめ = 夜", DeEsserParams(mix = 40f)),
            UserPreset("A", DeEsserParams()),
        )
        val back = PresetCodec.decodeAll(PresetCodec.encodeAll(list), DeEsserParams())
        assertEquals(list.map { it.name }, back.map { it.name })
        assertEquals(custom.vocal, back[0].params.vocal)
        assertEquals(40f, back[1].params.mix, 0f)
    }

    @Test
    fun namesAreCleaned() {
        assertEquals("abc", PresetCodec.cleanName("  a\nb\tc  "))
        assertEquals(PresetCodec.MAX_NAME_LENGTH, PresetCodec.cleanName("x".repeat(100)).length)
        assertEquals("", PresetCodec.cleanName(" \n "))
    }

    @Test
    fun unknownKeysAreIgnoredAndMissingKeysKeepCurrent() {
        val current = DeEsserParams(mix = 55f, sharpness = 22f)
        val p = PresetCodec.decode("v=9\nmix=90\nfuture.knob=3\nsharpness=oops\nvocal.intensity=500", current)
        assertEquals(90f, p.mix, 0f)
        assertEquals(22f, p.sharpness, 0f) // unreadable value: keep current
        assertEquals(100f, p.vocal.intensity, 0f) // clamped
        assertEquals(current.guitar, p.guitar)
    }

    @Test
    fun brokenTextGivesNoPresets() {
        assertEquals(emptyList<UserPreset>(), PresetCodec.decodeAll(null, DeEsserParams()))
        assertEquals(emptyList<UserPreset>(), PresetCodec.decodeAll("", DeEsserParams()))
        assertEquals(emptyList<UserPreset>(), PresetCodec.decodeAll("only-a-name", DeEsserParams()))
    }

    @Test
    fun suggestedNameIsUnused() {
        assertEquals("マイプリセット 1", PresetCodec.suggestName("マイプリセット", emptyList()))
        assertEquals("P 3", PresetCodec.suggestName("P", listOf("P 1", "P 2")))
        assertEquals("P 3", PresetCodec.suggestName("P", listOf("P 2", "other")))
    }
}
