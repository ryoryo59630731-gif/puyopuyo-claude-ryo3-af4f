package app.deesstamer

import android.app.Application

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        AppState.init(this)
    }
}
