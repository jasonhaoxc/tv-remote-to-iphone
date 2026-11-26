package com.jason.dbboxcontroller

import android.accessibilityservice.AccessibilityService
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.accessibility.AccessibilityEvent
import android.widget.Toast
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

class RemoteKeyAccessibilityService : AccessibilityService() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var config: AppConfig
    private lateinit var esp32: Esp32Client

    private var lastActionMs: Long = 0L

    override fun onServiceConnected() {
        super.onServiceConnected()
        config = AppConfig(this)
        esp32 = Esp32Client(config)
        RuntimeState.accessibilityRunning = true
        RuntimeState.log("无障碍遥控接管服务已启动")
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        // Key interception is handled in onKeyEvent
    }

    override fun onInterrupt() {
        RuntimeState.accessibilityRunning = false
        RuntimeState.log("无障碍遥控接管服务被中断")
    }

    override fun onDestroy() {
        super.onDestroy()
        RuntimeState.accessibilityRunning = false
        scope.cancel()
        RuntimeState.log("无障碍遥控接管服务已停止")
    }

    override fun onKeyEvent(event: KeyEvent): Boolean {
        if (!::config.isInitialized) return false

        // Never hijack D-pad when our own control app is in the foreground
        if (RuntimeState.isSelfInForeground) {
            return false
        }

        // MENU key toggles remote interception on/off anytime
        if (event.keyCode == KeyEvent.KEYCODE_MENU) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                val nextState = !config.remoteInterceptEnabled
                config.remoteInterceptEnabled = nextState
                val text = if (nextState) "已开启 iPhone 遥控接管" else "已暂停 iPhone 遥控接管"
                RuntimeState.log(text)
                showToast(text)
                return true
            }
            return true
        }

        if (!config.remoteInterceptEnabled) {
            return false
        }

        val actionCmd: String?
        val actionLabel: String?
        when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_CHANNEL_DOWN,
            KeyEvent.KEYCODE_PAGE_DOWN -> {
                actionCmd = "SWIPE_UP"
                actionLabel = "遥控→下一个视频"
            }
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_CHANNEL_UP,
            KeyEvent.KEYCODE_PAGE_UP -> {
                actionCmd = "SWIPE_DOWN"
                actionLabel = "遥控→上一个视频"
            }
            KeyEvent.KEYCODE_DPAD_CENTER,
            KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE -> {
                actionCmd = "TAP"
                actionLabel = "遥控→暂停/播放"
            }
            KeyEvent.KEYCODE_DPAD_RIGHT -> {
                actionCmd = "DOUBLE_TAP"
                actionLabel = "遥控→双击点赞"
            }
            else -> return false
        }

        // Consume both ACTION_DOWN and ACTION_UP so the casting app won't react to the remote key
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            val now = System.currentTimeMillis()
            if (now - lastActionMs >= 260L) {
                lastActionMs = now
                scope.launch {
                    val reply = esp32.sendCommand(actionCmd)
                    if (reply != null && reply.ok) {
                        RuntimeState.log(actionLabel)
                    } else {
                        RuntimeState.log("$actionLabel (ESP32未响应)")
                    }
                }
            }
        }
        return true
    }

    private fun showToast(msg: String) {
        mainHandler.post {
            Toast.makeText(applicationContext, msg, Toast.LENGTH_SHORT).show()
        }
    }
}
