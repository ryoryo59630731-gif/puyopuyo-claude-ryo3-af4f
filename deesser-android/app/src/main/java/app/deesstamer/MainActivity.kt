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
import android.widget.Button
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import app.deesstamer.ui.BandView
import app.deesstamer.ui.CurveView
import app.deesstamer.ui.FocusColors
import app.deesstamer.ui.KnobView
import kotlin.math.ln
import kotlin.math.pow

class MainActivity : Activity() {

    private lateinit var power: Switch
    private lateinit var status: TextView
    private lateinit var warning: TextView
    private lateinit var curve: CurveView
    private lateinit var band: BandView
    private lateinit var focusGroup: RadioGroup
    private lateinit var focusButtons: Map<Focus, RadioButton>
    private lateinit var focusEnabled: Switch
    private lateinit var focusLow: KnobView
    private lateinit var focusHigh: KnobView
    private lateinit var processing: KnobView
    private lateinit var intensity: KnobView
    private lateinit var focusHint: TextView
    private lateinit var listenGroup: RadioGroup
    private lateinit var resolutionGroup: RadioGroup
    private lateinit var autoSwitch: Switch
    private lateinit var autoStatus: TextView
    private lateinit var sharpness: KnobView
    private lateinit var mix: KnobView
    private lateinit var outputGain: KnobView
    private lateinit var shelfSwitch: Switch
    private lateinit var shelfFreq: KnobView
    private lateinit var shelfGain: KnobView
    private lateinit var applyGroup: RadioGroup
    private lateinit var bootSwitch: Switch
    private lateinit var sessions: TextView
    private lateinit var helpSwitch: Switch
    private lateinit var helpViews: List<View>

    /** Guards against feedback loops while the UI is being filled from state. */
    private var rendering = false
    private val stateListener: () -> Unit = { render() }
    private val liveListener: () -> Unit = {
        band.live = AppState.live
        renderAutoStatus()
    }

    private val supported by lazy { EffectEngine.isSupported() }

    private fun focusName(f: Focus) = getString(
        when (f) {
            Focus.GUITAR -> R.string.focus_guitar
            Focus.VOCAL -> R.string.focus_vocal
            Focus.HIHAT -> R.string.focus_hihat
        },
    )

    private fun focusHintText(f: Focus) = getString(
        when (f) {
            Focus.GUITAR -> R.string.focus_hint_guitar
            Focus.VOCAL -> R.string.focus_hint_vocal
            Focus.HIHAT -> R.string.focus_hint_hihat
        },
    )

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
        focusGroup = findViewById(R.id.focusGroup)
        focusButtons = mapOf(
            Focus.GUITAR to findViewById(R.id.focusGuitar),
            Focus.VOCAL to findViewById(R.id.focusVocal),
            Focus.HIHAT to findViewById(R.id.focusHihat),
        )
        focusEnabled = findViewById(R.id.focusEnabled)
        focusLow = findViewById(R.id.focusLow)
        focusHigh = findViewById(R.id.focusHigh)
        processing = findViewById(R.id.processing)
        intensity = findViewById(R.id.intensity)
        focusHint = findViewById(R.id.focusHint)
        listenGroup = findViewById(R.id.listenGroup)
        resolutionGroup = findViewById(R.id.resolutionGroup)
        autoSwitch = findViewById(R.id.autoSwitch)
        autoStatus = findViewById(R.id.autoStatus)
        sharpness = findViewById(R.id.sharpness)
        mix = findViewById(R.id.mix)
        outputGain = findViewById(R.id.outputGain)
        shelfSwitch = findViewById(R.id.shelfSwitch)
        shelfFreq = findViewById(R.id.shelfFreq)
        shelfGain = findViewById(R.id.shelfGain)
        applyGroup = findViewById(R.id.applyGroup)
        bootSwitch = findViewById(R.id.bootSwitch)
        sessions = findViewById(R.id.sessions)
        helpSwitch = findViewById(R.id.helpSwitch)
        helpViews = viewsTagged(findViewById(R.id.root), HELP_TAG)

        band.labels = Focus.entries.associateWith { focusName(it) }
        curve.axisLabel = getString(R.string.curve_axis)

        // Per-target knobs edit whichever target is selected.
        freqKnob(focusLow, R.string.knob_low, Focus.VOCAL.defaultLowHz) { p, v ->
            p.updateFocus(p.selected) { it.copy(lowHz = v) }
        }
        freqKnob(focusHigh, R.string.knob_high, Focus.VOCAL.defaultHighHz) { p, v ->
            p.updateFocus(p.selected) { it.copy(highHz = v) }
        }
        percentKnob(processing, R.string.knob_processing, 50f) { p, v ->
            p.updateFocus(p.selected) { it.copy(processing = v) }
        }
        percentKnob(intensity, R.string.knob_intensity, 50f) { p, v ->
            p.updateFocus(p.selected) { it.copy(intensity = v) }
        }
        // Shown as approximate depth, like the Range / Depth controls of Pro-DS and soothe2.
        intensity.formatter = { v ->
            getString(R.string.depth_format, DspMapping.estimatedDepthDb(v, AppState.params.mix))
        }

        val d = DeEsserParams()
        percentKnob(sharpness, R.string.knob_sharpness, d.sharpness) { p, v -> p.copy(sharpness = v) }
        percentKnob(mix, R.string.knob_mix, d.mix) { p, v -> p.copy(mix = v) }
        dbKnob(outputGain, R.string.knob_output, d.outputGainDb, -12f, 6f, 0xFFA0AEC0.toInt()) { p, v -> p.copy(outputGainDb = v) }
        freqKnob(shelfFreq, R.string.knob_shelf_freq, d.shelfFreqHz, 2_000f, 16_000f) { p, v -> p.copy(shelfFreqHz = v) }
        dbKnob(shelfGain, R.string.knob_shelf_gain, d.shelfGainDb, -18f, 0f, 0xFFE2E8F0.toInt()) { p, v -> p.copy(shelfGainDb = v) }

        focusGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            val f = focusButtons.entries.firstOrNull { it.value.id == id }?.key ?: return@setOnCheckedChangeListener
            AppState.update { it.copy(selected = f) }
        }
        focusEnabled.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { p -> p.updateFocus(p.selected) { it.copy(enabled = on) } }
        }
        listenGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.update { it.copy(listen = if (id == R.id.listenFilter) ListenMode.FILTER else ListenMode.NORMAL) }
        }
        resolutionGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.update { it.copy(resolution = if (id == R.id.resSixth) Resolution.SIXTH else Resolution.THIRD) }
        }
        autoSwitch.setOnCheckedChangeListener { _, on ->
            if (rendering) return@setOnCheckedChangeListener
            if (!on) {
                AppState.update { it.copy(autoLevel = false) }
            } else if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                enableAutoLevel()
            } else {
                requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), REQ_AUDIO)
            }
        }
        shelfSwitch.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { it.copy(shelfEnabled = on) }
        }
        applyGroup.setOnCheckedChangeListener { _, id ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.update {
                it.copy(applyMode = if (id == R.id.applySessions) ApplyMode.SESSIONS else ApplyMode.GLOBAL)
            }
        }
        bootSwitch.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { it.copy(startOnBoot = on) }
        }
        power.setOnCheckedChangeListener { _, on ->
            if (rendering) return@setOnCheckedChangeListener
            setPower(on)
        }
        helpSwitch.setOnCheckedChangeListener { _, on ->
            if (rendering) return@setOnCheckedChangeListener
            AppState.prefs.showHelp = on
            render()
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
        AppState.addLiveListener(liveListener)
        // Resume the effect if it was on but the process was killed.
        if (AppState.prefs.powerOn && !AppState.serviceRunning && supported) {
            runCatching { DeEsserService.start(this) }
        }
        render()
    }

    override fun onStop() {
        AppState.removeListener(stateListener)
        AppState.removeLiveListener(liveListener)
        super.onStop()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_AUDIO) return
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            enableAutoLevel()
        } else {
            Toast.makeText(this, R.string.auto_permission_denied, Toast.LENGTH_LONG).show()
            render()
        }
    }

    private fun enableAutoLevel() {
        AppState.update { it.copy(autoLevel = true) }
        // Let the running service re-declare its foreground type while we are visible.
        if (AppState.serviceRunning) {
            DeEsserService.start(this) { action = DeEsserService.ACTION_REFRESH }
        }
        render()
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

            val showHelp = AppState.prefs.showHelp
            helpSwitch.isChecked = showHelp
            for (v in helpViews) v.visibility = if (showHelp) View.VISIBLE else View.GONE

            val sel = p.selected
            val fp = p.focus(sel)
            val color = FocusColors.of(sel)

            band.params = p
            curve.color = color
            curve.setCurve(
                thresholdDb = DspMapping.marginDb(fp.processing),
                ratio = DspMapping.ratio(fp.intensity, p.mix),
                kneeDb = DspMapping.kneeDb(p.sharpness),
                enabled = fp.enabled,
            )

            for ((f, b) in focusButtons) {
                b.text = focusName(f) + "\n" + getString(if (p.focus(f).enabled) R.string.focus_on else R.string.focus_off)
            }
            focusGroup.check(focusButtons.getValue(sel).id)
            focusEnabled.text = getString(R.string.focus_enabled, focusName(sel))
            focusEnabled.isChecked = fp.enabled
            focusHint.text = focusHintText(sel)

            for (k in listOf(focusLow, focusHigh, processing, intensity)) {
                k.accentColor = color
                k.isEnabled = fp.enabled
            }
            focusLow.defaultValue = sel.defaultLowHz
            focusHigh.defaultValue = sel.defaultHighHz
            processing.defaultValue = FocusParams.defaultFor(sel).processing
            intensity.defaultValue = FocusParams.defaultFor(sel).intensity
            focusLow.value = fp.lowHz
            focusHigh.value = fp.highHz
            processing.value = fp.processing
            intensity.value = fp.intensity

            listenGroup.check(if (p.listen == ListenMode.FILTER) R.id.listenFilter else R.id.listenNormal)
            resolutionGroup.check(if (p.resolution == Resolution.SIXTH) R.id.resSixth else R.id.resThird)
            autoSwitch.isChecked = p.autoLevel
            band.live = AppState.live
            renderAutoStatus()

            sharpness.value = p.sharpness
            mix.value = p.mix
            outputGain.value = p.outputGainDb
            shelfFreq.value = p.shelfFreqHz
            shelfGain.value = p.shelfGainDb
            shelfFreq.isEnabled = p.shelfEnabled
            shelfGain.isEnabled = p.shelfEnabled
            shelfSwitch.isChecked = p.shelfEnabled

            applyGroup.check(if (p.applyMode == ApplyMode.SESSIONS) R.id.applySessions else R.id.applyGlobal)
            bootSwitch.isChecked = p.startOnBoot

            renderStatus(p)
        } finally {
            rendering = false
        }
    }

    private fun renderAutoStatus() {
        val p = AppState.params
        autoStatus.text = when {
            !p.autoLevel -> getString(R.string.auto_status_off)
            !AppState.serviceRunning -> getString(R.string.auto_status_stopped)
            else -> when (AppState.autoStatus) {
                AppState.AutoStatus.NO_PERMISSION -> getString(R.string.auto_status_no_permission)
                AppState.AutoStatus.FAILED -> getString(R.string.auto_status_failed, AppState.autoError ?: "?")
                AppState.AutoStatus.RUNNING ->
                    if (AppState.autoLearnedSeconds < LevelAnalyzer.LONG_TAU_SEC) {
                        getString(R.string.auto_status_learning, AppState.autoLearnedSeconds)
                    } else {
                        getString(R.string.auto_status_running)
                    }
                AppState.AutoStatus.OFF -> getString(R.string.auto_status_stopped)
            }
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
            if (p.applyMode == ApplyMode.GLOBAL) {
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
            val hz = OutputRate.mediaOutputHz()
            if (hz > 0) append('\n').append(getString(R.string.status_output_rate, fmtKhz(hz)))
        }
    }

    private fun fmtKhz(hz: Int): String =
        if (hz % 1000 == 0) (hz / 1000).toString() else "%.1f".format(hz / 1000f)

    /** All views under [root] whose tag equals [tag]. */
    private fun viewsTagged(root: View, tag: String): List<View> {
        val out = ArrayList<View>()
        fun walk(v: View) {
            if (v.tag == tag) out += v
            if (v is android.view.ViewGroup) for (i in 0 until v.childCount) walk(v.getChildAt(i))
        }
        walk(root)
        return out
    }

    // ---- knob helpers ----

    private fun percentKnob(k: KnobView, label: Int, def: Float, set: (DeEsserParams, Float) -> DeEsserParams) {
        k.label = getString(label)
        k.min = 0f; k.max = 100f; k.defaultValue = def
        k.formatter = { "%.0f%%".format(it) }
        k.onValueChange = { v -> if (!rendering) AppState.update { set(it, v) } }
    }

    private fun freqKnob(
        k: KnobView, label: Int, def: Float,
        fMin: Float = DspMapping.MIN_FREQ_HZ, fMax: Float = DspMapping.MAX_FREQ_HZ,
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
        private const val REQ_AUDIO = 11
        private const val HELP_TAG = "help"
    }
}
