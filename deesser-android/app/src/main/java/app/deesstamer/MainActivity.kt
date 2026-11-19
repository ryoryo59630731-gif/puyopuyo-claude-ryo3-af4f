package app.deesstamer

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.media.audiofx.AudioEffect
import android.os.Bundle
import android.text.InputFilter
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import app.deesstamer.ui.BandOffsetView
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
    private lateinit var thresholdOffset: KnobView
    private lateinit var bandSwitch: Switch
    private lateinit var bandOffsets: BandOffsetView
    private lateinit var bandSelected: TextView

    /** Selected band for per-band editing, as a 1/6-octave index so it survives layout changes. */
    private var selectedFineBand = 10

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
        thresholdOffset = findViewById(R.id.thresholdOffset)
        bandSwitch = findViewById(R.id.bandSwitch)
        bandOffsets = findViewById(R.id.bandOffsets)
        bandSelected = findViewById(R.id.bandSelected)

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
            val p = AppState.params
            getString(R.string.depth_format, DspMapping.estimatedDepthDb(v, p.mix, p.thresholdOffsetDb))
        }

        val d = DeEsserParams()
        percentKnob(sharpness, R.string.knob_sharpness, d.sharpness) { p, v -> p.copy(sharpness = v) }
        percentKnob(mix, R.string.knob_mix, d.mix) { p, v -> p.copy(mix = v) }
        dbKnob(outputGain, R.string.knob_output, d.outputGainDb, -12f, 6f, 0xFFA0AEC0.toInt()) { p, v -> p.copy(outputGainDb = v) }
        freqKnob(shelfFreq, R.string.knob_shelf_freq, d.shelfFreqHz, 2_000f, 16_000f) { p, v -> p.copy(shelfFreqHz = v) }
        dbKnob(shelfGain, R.string.knob_shelf_gain, d.shelfGainDb, -18f, 0f, 0xFFE2E8F0.toInt()) { p, v -> p.copy(shelfGainDb = v) }
        val range = DspMapping.THRESHOLD_OFFSET_RANGE_DB
        dbKnob(thresholdOffset, R.string.knob_threshold, d.thresholdOffsetDb, -range, range, 0xFFF687B3.toInt()) { p, v ->
            p.copy(thresholdOffsetDb = v)
        }

        // Per-band thresholds: tap a band to select it, change it with the buttons.
        bandOffsets.onSelect = { i ->
            selectedFineBand = DspMapping.fineBandsOf(DspMapping.layout(AppState.params), i).first
            render()
        }
        bandSwitch.setOnCheckedChangeListener { _, on ->
            if (!rendering) AppState.update { it.copy(bandOffsetsEnabled = on) }
        }
        fun editSelected(change: (DeEsserParams, DspMapping.Layout, Int) -> DeEsserParams) {
            AppState.update { p ->
                val layout = DspMapping.layout(p)
                change(p, layout, selectedBandIn(layout))
            }
        }
        findViewById<Button>(R.id.bandMinus).setOnClickListener {
            editSelected { p, l, i -> DspMapping.withBandOffsetDelta(p, l, i, -1f) }
        }
        findViewById<Button>(R.id.bandPlus).setOnClickListener {
            editSelected { p, l, i -> DspMapping.withBandOffsetDelta(p, l, i, 1f) }
        }
        findViewById<Button>(R.id.bandZero).setOnClickListener {
            editSelected { p, l, i -> DspMapping.withBandOffsetCleared(p, l, i) }
        }
        findViewById<Button>(R.id.bandResetAll).setOnClickListener {
            confirm(getString(R.string.band_reset_all_confirm)) {
                AppState.update { it.copy(bandOffsetsDb = DspMapping.ZERO_BAND_OFFSETS) }
            }
        }

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

    // ---- presets: built-in ones, then the user's own (★), then save / manage ----

    private fun showPresets() {
        val user = AppState.prefs.loadUserPresets()
        val entries = ArrayList<Pair<String, () -> Unit>>()
        for (p in Presets.all) entries += getString(p.nameRes) to { AppState.update(p.apply) }
        for (u in user) {
            entries += getString(R.string.preset_user_item, u.name) to {
                AppState.update { PresetCodec.applyTo(u, it) }
                toast(getString(R.string.preset_applied, u.name))
            }
        }
        entries += getString(R.string.preset_save) to { saveCurrentAsPreset() }
        if (user.isNotEmpty()) entries += getString(R.string.preset_manage) to { showManagePresets() }
        AlertDialog.Builder(this)
            .setTitle(R.string.presets)
            .setItems(entries.map { it.first }.toTypedArray()) { _, which -> entries[which].second() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun saveCurrentAsPreset() {
        val list = AppState.prefs.loadUserPresets()
        val suggestion = PresetCodec.suggestName(getString(R.string.preset_default_name), list.map { it.name })
        askName(getString(R.string.preset_save_title), suggestion) { name ->
            if (list.any { it.name == name }) {
                confirm(getString(R.string.preset_overwrite_confirm, name)) { storePreset(name) }
            } else {
                storePreset(name)
            }
        }
    }

    /** Saves the current sound under [name], replacing a preset of that name in place. */
    private fun storePreset(name: String) {
        val list = AppState.prefs.loadUserPresets().toMutableList()
        val preset = UserPreset(name, AppState.params)
        val i = list.indexOfFirst { it.name == name }
        if (i >= 0) list[i] = preset else list += preset
        AppState.prefs.saveUserPresets(list)
        toast(getString(R.string.preset_saved, name))
    }

    private fun showManagePresets() {
        val list = AppState.prefs.loadUserPresets()
        if (list.isEmpty()) return
        AlertDialog.Builder(this)
            .setTitle(R.string.preset_manage_title)
            .setItems(list.map { it.name }.toTypedArray()) { _, i -> showPresetActions(list[i].name) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showPresetActions(name: String) {
        val actions = arrayOf(
            getString(R.string.preset_rename),
            getString(R.string.preset_overwrite),
            getString(R.string.preset_delete),
        )
        AlertDialog.Builder(this)
            .setTitle(name)
            .setItems(actions) { _, which ->
                when (which) {
                    0 -> renamePreset(name)
                    1 -> confirm(getString(R.string.preset_overwrite_confirm, name)) { storePreset(name) }
                    2 -> confirm(getString(R.string.preset_delete_confirm, name)) {
                        AppState.prefs.saveUserPresets(AppState.prefs.loadUserPresets().filter { it.name != name })
                        toast(getString(R.string.preset_deleted, name))
                    }
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun renamePreset(old: String) {
        askName(getString(R.string.preset_rename), old) { name ->
            val list = AppState.prefs.loadUserPresets()
            when {
                name == old -> Unit
                list.any { it.name == name } -> toast(getString(R.string.preset_name_taken, name))
                else -> {
                    AppState.prefs.saveUserPresets(list.map { if (it.name == old) it.copy(name = name) else it })
                    toast(getString(R.string.preset_renamed, name))
                }
            }
        }
    }

    /** One-line name dialog; OK only closes it when the cleaned name is not empty. */
    private fun askName(title: String, initial: String, onOk: (String) -> Unit) {
        val input = EditText(this).apply {
            setSingleLine(true)
            filters = arrayOf(InputFilter.LengthFilter(PresetCodec.MAX_NAME_LENGTH))
            hint = getString(R.string.preset_name_hint)
            setText(initial)
            selectAll()
        }
        val pad = (20 * resources.displayMetrics.density).toInt()
        val box = FrameLayout(this).apply {
            setPadding(pad, pad / 2, pad, 0)
            addView(input)
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle(title)
            .setView(box)
            .setPositiveButton(android.R.string.ok, null)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val name = PresetCodec.cleanName(input.text.toString())
                if (name.isEmpty()) {
                    input.error = getString(R.string.preset_name_empty)
                } else {
                    dialog.dismiss()
                    onOk(name)
                }
            }
            input.requestFocus()
        }
        dialog.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
        dialog.show()
    }

    private fun confirm(message: String, onYes: () -> Unit) {
        AlertDialog.Builder(this)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok) { _, _ -> onYes() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_SHORT).show()

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
                thresholdDb = DspMapping.marginDb(fp.processing) + p.thresholdOffsetDb,
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

            thresholdOffset.value = p.thresholdOffsetDb
            intensity.invalidate() // its readout depends on MIX and THRESHOLD
            renderBandOffsets(p)

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

    /** Analysis band of [layout] that contains the selected 1/6-octave band. */
    private fun selectedBandIn(layout: DspMapping.Layout): Int {
        val per = DspMapping.FINE_BAND_COUNT / layout.analysisBands
        return (selectedFineBand.coerceIn(0, DspMapping.FINE_BAND_COUNT - 1) / per + 1)
    }

    private fun renderBandOffsets(p: DeEsserParams) {
        val layout = DspMapping.layout(p)
        val i = selectedBandIn(layout)
        bandSwitch.isChecked = p.bandOffsetsEnabled
        bandOffsets.params = p
        bandOffsets.selected = i
        val stored = DspMapping.fineBandsOf(layout, i).map { p.bandOffsetsDb.getOrElse(it) { 0f } }.average().toFloat()
        val text = StringBuilder(
            getString(
                R.string.band_selected,
                BandView.fmtHz(layout.bandLow(i)), BandView.fmtHz(layout.bandHigh(i)), "%+.1f dB".format(stored),
            ),
        )
        when {
            !p.bandOffsetsEnabled -> text.append('\n').append(getString(R.string.band_selected_off))
            DspMapping.bandSettings(p)[i].owner == null -> text.append('\n').append(getString(R.string.band_not_processed))
        }
        bandSelected.text = text
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
