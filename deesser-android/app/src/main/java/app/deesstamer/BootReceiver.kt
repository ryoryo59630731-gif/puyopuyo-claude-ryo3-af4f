package app.deesstamer

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/** Restarts the effect after a reboot when the user turned on "start on boot". */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED &&
            intent.action != Intent.ACTION_MY_PACKAGE_REPLACED
        ) return
        AppState.init(context)
        if (AppState.params.startOnBoot && AppState.prefs.powerOn) {
            runCatching { DeEsserService.start(context) }
                .onFailure { Log.w("BootReceiver", "could not start service", it) }
        }
    }
}
