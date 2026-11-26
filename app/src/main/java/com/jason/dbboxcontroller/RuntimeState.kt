package com.jason.dbboxcontroller

import java.text.SimpleDateFormat
import java.util.ArrayDeque
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArrayList

object RuntimeState {
    @Volatile var accessibilityRunning: Boolean = false
    @Volatile var screenMonitorRunning: Boolean = false
    @Volatile var isSelfInForeground: Boolean = false

    @Volatile var esp32Reachable: Boolean = false
    @Volatile var esp32BleConnected: Boolean = false
    @Volatile var esp32Mode: String = "video"
    @Volatile var esp32LastSeenIp: String = ""

    @Volatile var detectorState: String = "IDLE"
    @Volatile var lastPortraitOcr: String = ""
    @Volatile var lastLandscapeOcr: String = ""
    @Volatile var lastCountdownSec: Int? = null
    @Volatile var totalSkippedAds: Int = 0
    @Volatile var latestScreenshotJpeg: ByteArray? = null
    @Volatile var latestFrameWidth: Int = 0
    @Volatile var latestFrameHeight: Int = 0

    private val logs = ArrayDeque<String>(80)
    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    fun addListener(listener: () -> Unit) {
        listeners.add(listener)
    }

    fun removeListener(listener: () -> Unit) {
        listeners.remove(listener)
    }

    fun notifyChanged() {
        for (l in listeners) {
            try {
                l()
            } catch (_: Exception) {
            }
        }
    }

    fun log(msg: String) {
        val line = "[${timeFmt.format(Date())}] $msg"
        synchronized(logs) {
            if (logs.size >= 80) {
                logs.removeFirst()
            }
            logs.addLast(line)
        }
        notifyChanged()
    }

    fun getLogs(): List<String> = synchronized(logs) { logs.toList().reversed() }
}
