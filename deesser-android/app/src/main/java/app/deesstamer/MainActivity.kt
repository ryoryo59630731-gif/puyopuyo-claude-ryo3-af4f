package app.deesstamer

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.media.audiofx.AudioEffect
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.widget.RadioGroup
import android.widget.Switch
import android.widget.TextView
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import app.deesstamer.ui.BandView
import app.deesstamer.ui.CurveView
import app.deesstamer.ui.KnobView
import kotlin.math.ln
import kotlin.math.pow

class MainActivity : Activity() {

    private lateinit var power: Switch
    private lateinit var status: TextView
    private lateinit var warning: TextView
    private lateinit var curve: CurveView
    private lateinit var band: BandView
    private lateinit var rangeGroup: RadioGroup
    private lateinit var customRow: LinearLayout
    private lateinit var customLow: KnobView
    private lateinit var customHigh: KnobView
    private lateinit var processing: KnobView
    private lateinit var intensity: KnobView
    private lateinit var sharpness: KnobView
    private lateinit var mix: KnobView
    private lateinit var listenGroup: RadioGroup
    private lateinit var shelfSwitch: Switch
    private lateinit var shelfFreq: KnobView
    private lateinit var shelfGain: KnobView
    private lateinit var outputGain: KnobView
    private lateinit var targetGroup: RadioGroup
    private lateinit var bootSwitch: Switch
    private lateinit var sessions: TextView

    /** Guards against feedback loops while the UI is being filled from state. */
    private var rendering = false
    private val stateListener: () -> Unit = { render() }

    private val supported by lazy { EffectEngine.isSupported() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppState.init(this)
        setContentView(R.layout.activity_main)
        setupEdgeToEdge(findViewById(R.id.root))

        power = findViewById(R.id.power)
        status = findViewById(R.id.status)
        warning = findViewById(R.id.warning)
        curve = findViewById(R.id.curve)
        band = findViewById(R.id.band)
        rangeGroup = findViewById(R.id.rangeGroup)
        customRow = findViewById(R.id.customRow)
        customLow = findViewById(R.id.customLow)
        customHigh = findViewById(R.id.customHigh)
        processing = findViewById(R.id.processing)
        intensity = findViewById(R.id.intensity)
        sharpness = findViewById(R.id.sharpness)
        mix = findViewById(R.id.mix)
        listenGroup = findViewById(R.id.listenGroup)
        shelfSwitch = findViewById(R.id.shelfSwitch)
        shelfFreq = findViewById(R.id.shelfFreq)
        shelfGain = findViewById(R.id.shelfGain)
        outputGain = findViewById(R.id.outputGain)
        targetGroup = findViewById(R.id.targetGroup)
        bootSwitch = findViewById(R.id.bootSwitch)
        sessions = findViewById(R.id.sessions)

        val d = DeEsserParams()
        percentKnob(processing, R.string.knob_processing, d.processing) { p, v -> p.copy(processing = v) }
        percentKnob(intensity, R.string.knob_intensity, d.intensity) { p, v -> p.copy(intensity = v) }
        percentKnob(sharpness, R.string.knob_sharpness, d.sharpness) { p, v -> p.copy(sharpness = v) }
        percentKnob(mix, R.string.knob_mix, d.mix) { p, v -> p.copy(mix = v) }
        freqKnob(customLow, R.string.knob_low, d.customLowHz, 1_000f, 16_000f) { p, v -> p.copy(customLowHz = v) }
        freqKnob(customHigh, R.string.knob_high, d.customHighHz, 2_000f, 20_000f) { p, v -> p.copy(customHighHz = v) }
        freqKnob(shelfFreq, R.string.knob_shelf_freq, d.shelfFreqHz, 2_000f, 16_000f) { p, v -> p.copy(shelfFreqHz = v) }
        dbKnob(shelfGain, R.string.knob_shelf_gain, d.shelfGainDb, -18f, 0f, 0xFFF6AD55.toInt()) { p, v -> p.copy(shelfGainDb = v) }
        dbKnob(outputGain, R.string.knob_output, d.outputGainDb, -12f, 6f, 0xFFB794F4.toInt()) { p, v -> p.copy(outputGainDb = v) }

        rangeGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            val mode = when (id) {
                R.id.rangeLowHi -> RangeMode.LOW_HI
                R.id.rangeMidHi -> RangeMode.MID_HI
                R.id.rangeHigh -> RangeMode.HIGH
                R.id.rangeHiEnd -> RangeMode.HI_END
                else -> RangeMode.CUSTOM
            }
            AppState.update { it.copy(range = mode) }
        }
        listenGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.update { it.copy(listen = if (id == R.id.listenFilter) ListenMode.FILTER else ListenMode.NORMAL) }
        }
        shelfSwitch.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { it.copy(shelfEnabled = on) }
        }
        targetGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.update {
                it.copy(target = if (id == R.id.targetSessions) TargetMode.SESSIONS else TargetMode.GLOBAL)
            }
        }
        bootSwitch.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { it.copy(startOnBoot = on) }
        }
        power.setOnCheckedChangeListener { _, on ->
            if (rendering) return@setOnCheckedChangeListener
            setPower(on)
        }
        findViewById<Button>(R.id.presetButton).setOnClickListener { showPresets() }

        handleEffectPanelIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleEffectPanelIntent(intent)
    }

    override fun onStart() {
        super.onStart()
        AppState.addListener(stateListener)
        // Resume the effect if it was on but the process was killed.
        if (AppState.prefs.powerOn && !AppState.serviceRunning && supported) {
            runCatching { DeEsserService.start(this) }
        }
        render()
    }

    override fun onStop() {
        AppState.removeListener(stateListener)
        super.onStop()
    }

    /**
     * Music players open this screen from their "Equalizer" menu with their audio session id
     * (ACTION_DISPLAY_AUDIO_EFFECT_CONTROL_PANEL). Attach to that session right away.
     */
    private fun handleEffectPanelIntent(intent: Intent?) {
        if (intent?.action != AudioEffect.ACTION_DISPLAY_AUDIO_EFFECT_CONTROL_PANEL) return
        setResult(RESULT_OK)
        val session = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, AudioEffect.ERROR)
        val pkg = intent.getStringExtra(AudioEffect.EXTRA_PACKAGE_NAME)
        if (session <= 0 || !supported) return
        // In GLOBAL mode the player is already covered by the output mix; just make sure we run.
        AppState.prefs.powerOn = true
        DeEsserService.start(this) {
            action = DeEsserService.ACTION_ATTACH_SESSION
            putExtra(AudioEffect.EXTRA_AUDIO_SESSION, session)
            putExtra(AudioEffect.EXTRA_PACKAGE_NAME, pkg)
        }
    }

    private fun setPower(on: Boolean) {
        AppState.prefs.powerOn = on
        if (on) {
            if (android.os.Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
            ) {
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQ_NOTIF)
            }
            DeEsserService.start(this)
        } else {
            DeEsserService.stop(this)
        }
        render()
    }

    private fun showPresets() {
        val names = Presets.all.map { getString(it.nameRes) }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(R.string.presets)
            .setItems(names) { _, which -> AppState.update(Presets.all[which].apply) }
            .show()
    }

    private fun render() {
        rendering = true
        try {
            val p = AppState.params
            power.isChecked = AppState.serviceRunning || AppState.prefs.powerOn
            power.isEnabled = supported
            warning.visibility = if (supported) View.GONE else View.VISIBLE

            curve.params = p
            band.params = p

            rangeGroup.check(
                when (p.range) {
                    RangeMode.LOW_HI -> R.id.rangeLowHi
                    RangeMode.MID_HI -> R.id.rangeMidHi
                    RangeMode.HIGH -> R.id.rangeHigh
                    RangeMode.HI_END -> R.id.rangeHiEnd
                    RangeMode.CUSTOM -> R.id.rangeCustom
                },
            )
            customRow.visibility = if (p.range == RangeMode.CUSTOM) View.VISIBLE else View.GONE
            listenGroup.check(if (p.listen == ListenMode.FILTER) R.id.listenFilter else R.id.listenNormal)

            processing.value = p.processing
            intensity.value = p.intensity
            sharpness.value = p.sharpness
            mix.value = p.mix
            customLow.value = p.customLowHz
            customHigh.value = p.customHighHz
            shelfFreq.value = p.shelfFreqHz
            shelfGain.value = p.shelfGainDb
            outputGain.value = p.outputGainDb
            shelfFreq.isEnabled = p.shelfEnabled
            shelfGain.isEnabled = p.shelfEnabled

            shelfSwitch.isChecked = p.shelfEnabled
            targetGroup.check(if (p.target == TargetMode.SESSIONS) R.id.targetSessions else R.id.targetGlobal)
            bootSwitch.isChecked = p.startOnBoot

            renderStatus(p)
        } finally {
            rendering = false
        }
    }

    private fun renderStatus(p: DeEsserParams) {
        val engine = AppState.engine
        val list = engine.sessions()
        val global = list.firstOrNull { it.sessionId == AppState.GLOBAL_SESSION }
        val apps = list.filter { it.sessionId != AppState.GLOBAL_SESSION }

        status.text = when {
            !supported -> getString(R.string.status_unsupported)
            !AppState.serviceRunning -> getString(R.string.status_off)
            else -> buildString {
                append(getString(R.string.status_on))
                if (p.listen == ListenMode.FILTER) append(" · ").append(getString(R.string.status_filter))
            }
        }

        sessions.text = if (!AppState.serviceRunning) {
            getString(R.string.sessions_idle)
        } else buildString {
            if (p.target == TargetMode.GLOBAL) {
                append(getString(R.string.sessions_global)).append(": ")
                val globalErr = engine.errors[AppState.GLOBAL_SESSION]
                append(
                    when {
                        global == null && globalErr != null -> getString(R.string.state_failed, globalErr)
                        global == null -> getString(R.string.state_disabled)
                        !global.hasControl -> getString(R.string.state_no_control)
                        else -> getString(R.string.state_active)
                    },
                )
                if (global == null) append('\n').append(getString(R.string.sessions_try_per_app))
            } else {
                append(getString(R.string.sessions_apps, apps.size))
                for (s in apps) {
                    append("\n  • ").append(s.packageName ?: "?").append(" (#").append(s.sessionId).append(')')
                    if (!s.hasControl) append(" ").append(getString(R.string.state_no_control))
                }
                if (apps.isEmpty()) append('\n').append(getString(R.string.sessions_hint))
            }
        }
    }

    // ---- knob helpers ----

    private fun percentKnob(k: KnobView, label: Int, def: Float, set: (DeEsserParams, Float) -> DeEsserParams) {
        k.label = getString(label)
        k.min = 0f; k.max = 100f; k.defaultValue = def
        k.formatter = { "%.0f%%".format(it) }
        k.onValueChange = { v -> if (!rendering) AppState.update { set(it, v) } }
    }

    private fun freqKnob(
        k: KnobView, label: Int, def: Float, fMin: Float, fMax: Float,
        set: (DeEsserParams, Float) -> DeEsserParams,
    ) {
        k.label = getString(label)
        k.min = fMin; k.max = fMax; k.defaultValue = def
        k.toValue = { t -> fMin * (fMax / fMin).pow(t) }
        k.toPosition = { v -> (ln(v / fMin) / ln(fMax / fMin)) }
        k.formatter = { BandView.fmtHz(it) + "Hz" }
        k.accentColor = 0xFF63B3ED.toInt()
        k.onValueChange = { v -> if (!rendering) AppState.update { set(it, v) } }
    }

    private fun dbKnob(
        k: KnobView, label: Int, def: Float, lo: Float, hi: Float, color: Int,
        set: (DeEsserParams, Float) -> DeEsserParams,
    ) {
        k.label = getString(label)
        k.min = lo; k.max = hi; k.defaultValue = def
        k.toValue = { t -> lo + (hi - lo) * t }
        k.toPosition = { v -> (v - lo) / (hi - lo) }
        k.formatter = { "%+.1f dB".format(it) }
        k.accentColor = color
        // Snap to 0.5 dB steps.
        k.onValueChange = { v -> if (!rendering) AppState.update { set(it, Math.round(v * 2f) / 2f) } }
    }

    // ---- edge-to-edge (enforced on Android 15 when targeting API 35) ----

    @Suppress("DEPRECATION")
    private fun setupEdgeToEdge(root: ScrollView) {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
        } else {
            root.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
        val base = (16 * resources.displayMetrics.density).toInt()
        root.setOnApplyWindowInsetsListener { v, insets ->
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                v.setPadding(base + i.left, base + i.top, base + i.right, base + i.bottom)
            } else {
                v.setPadding(
                    base + insets.systemWindowInsetLeft, base + insets.systemWindowInsetTop,
                    base + insets.systemWindowInsetRight, base + insets.systemWindowInsetBottom,
                )
            }
            insets
        }
    }

    companion object {
        private const val REQ_NOTIF = 10
    }
}
