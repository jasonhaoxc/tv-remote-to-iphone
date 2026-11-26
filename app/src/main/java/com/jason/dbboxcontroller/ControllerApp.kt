package com.jason.dbboxcontroller

import android.app.Application

class ControllerApp : Application() {
    lateinit var config: AppConfig
        private set
    lateinit var webServer: WebConfigServer
        private set

    override fun onCreate() {
        super.onCreate()
        config = AppConfig(this)
        webServer = WebConfigServer(this, config)
        webServer.start()
    }
}
