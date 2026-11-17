package app.deesstamer

import android.content.Context
import android.os.Handler
import android.os.Looper

/**
 * Process-wide state shared by the UI and the foreground service. Everything runs on the
 * main thread.
 */
object AppState {
    lateinit var prefs: Prefs
        private set

    var params: DeEsserParams = DeEsserParams()
        private set

    val engine = EffectEngine()

    /** True while [DeEsserService] is alive. */
    var serviceRunning = false
        internal set

    private val listeners = LinkedHashSet<() -> Unit>()
    private val main = Handler(Looper.getMainLooper())
    private var saveScheduled = false

    fun init(context: Context) {
        if (::prefs.isInitialized) return
        prefs = Prefs(context)
        params = prefs.load()
        engine.onChanged = { notifyListeners() }
    }

    fun addListener(l: () -> Unit) { listeners += l }
    fun removeListener(l: () -> Unit) { listeners -= l }

    fun notifyListeners() {
        listeners.toList().forEach { it() }
    }

    fun update(transform: (DeEsserParams) -> DeEsserParams) {
        val old = params
        val new = transform(old)
        if (new == old) return
        params = new
        scheduleSave()
        if (serviceRunning) {
            if (old.applyMode != new.applyMode) syncTargets()
            engine.applyAll(new)
        }
        notifyListeners()
    }

    /** Attach / detach effects so they match the selected apply mode. */
    fun syncTargets() {
        if (!serviceRunning) {
            engine.detachAll()
            return
        }
        when (params.applyMode) {
            ApplyMode.GLOBAL -> {
                engine.detachAll { it != GLOBAL_SESSION }
                if (!engine.has(GLOBAL_SESSION)) engine.attach(GLOBAL_SESSION, null, params)
            }
            ApplyMode.SESSIONS -> {
                engine.detach(GLOBAL_SESSION)
                engine.errors.remove(GLOBAL_SESSION)
            }
        }
    }

    /** Attach to a player's session if per-player mode is selected. */
    fun onSessionOpened(sessionId: Int, packageName: String?) {
        if (serviceRunning && params.applyMode == ApplyMode.SESSIONS && sessionId > 0) {
            engine.attach(sessionId, packageName, params)
        }
    }

    // Knob drags fire many updates per second; write to disk at most every 300 ms.
    private fun scheduleSave() {
        if (saveScheduled) return
        saveScheduled = true
        main.postDelayed({
            saveScheduled = false
            prefs.save(params)
        }, 300)
    }

    const val GLOBAL_SESSION = 0
}
