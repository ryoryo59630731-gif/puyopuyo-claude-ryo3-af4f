package app.deesstamer

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.media.audiofx.AudioEffect
import android.os.Build
import android.os.IBinder

/**
 * Keeps the effects alive while other apps play audio, and listens for players that announce
 * their audio sessions.
 */
class DeEsserService : Service() {

    private val sessionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val session = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, AudioEffect.ERROR)
            if (session <= 0) return
            val pkg = intent.getStringExtra(AudioEffect.EXTRA_PACKAGE_NAME)
            when (intent.action) {
                AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION ->
                    AppState.onSessionOpened(session, pkg)
                AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION ->
                    AppState.engine.detach(session)
            }
            updateNotification()
        }
    }

    private val stateListener: () -> Unit = { updateNotification() }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        AppState.init(this)
        createChannel()
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        val filter = IntentFilter().apply {
            addAction(AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
            addAction(AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(sessionReceiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            registerReceiver(sessionReceiver, filter)
        }

        AppState.serviceRunning = true
        AppState.syncTargets()
        AppState.addListener(stateListener)
        AppState.notifyListeners()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                AppState.prefs.powerOn = false
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_ATTACH_SESSION -> {
                val session = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, AudioEffect.ERROR)
                val pkg = intent.getStringExtra(AudioEffect.EXTRA_PACKAGE_NAME)
                AppState.onSessionOpened(session, pkg)
            }
        }
        updateNotification()
        return START_STICKY
    }

    override fun onDestroy() {
        runCatching { unregisterReceiver(sessionReceiver) }
        AppState.removeListener(stateListener)
        AppState.serviceRunning = false
        AppState.engine.detachAll()
        AppState.notifyListeners()
        super.onDestroy()
    }

    private fun createChannel() {
        val nm = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            CHANNEL_ID, getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW,
        ).apply { setShowBadge(false) }
        nm.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, DeEsserService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val sessions = AppState.engine.sessions()
        val text = when (AppState.params.applyMode) {
            ApplyMode.GLOBAL ->
                if (sessions.any { it.sessionId == AppState.GLOBAL_SESSION }) getString(R.string.notif_global_on)
                else getString(R.string.notif_global_failed)
            ApplyMode.SESSIONS ->
                getString(R.string.notif_sessions, sessions.count { it.sessionId != AppState.GLOBAL_SESSION })
        }
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(getString(R.string.notif_title))
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, getString(R.string.notif_stop), stop).build())
            .build()
    }

    private fun updateNotification() {
        if (!AppState.serviceRunning) return
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, buildNotification())
    }

    companion object {
        private const val CHANNEL_ID = "deesser"
        private const val NOTIFICATION_ID = 1
        const val ACTION_STOP = "app.deesstamer.STOP"
        const val ACTION_ATTACH_SESSION = "app.deesstamer.ATTACH_SESSION"

        fun start(context: Context, extra: Intent.() -> Unit = {}) {
            val intent = Intent(context, DeEsserService::class.java).apply(extra)
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, DeEsserService::class.java))
        }
    }
}
