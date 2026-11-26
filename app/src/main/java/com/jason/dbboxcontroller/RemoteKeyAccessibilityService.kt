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

    // Known TV casting / AirPlay mirroring apps
    private val mirroringPackages = listOf(
        "com.dangbei.screencast",
        "com.hpplay.happyplay",
        "com.ionitech.airscreen",
        "com.apowersoft.mirror",
        "com.waxrain.airplaydmr"
    )

    override fun onServiceConnected() {
        super.onServiceConnected()
        config = AppConfig(this)
        esp32 = Esp32Client(config)
        RuntimeState.accessibilityRunning = true
        RuntimeState.log("无障碍遥控服务已启动（仅在投屏播放画面自动接管）")
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event == null || event.eventType != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            return
        }
        val pkg = event.packageName?.toString() ?: return
        val cls = event.className?.toString() ?: ""

        // Ignore transient system overlays/toasts/dialogs so they don't break mirroring state
        if (pkg == "android" || pkg == "com.android.systemui" || cls.contains("Toast") || cls.contains("Dialog")) {
            return
        }

        val prevMirror = RuntimeState.isInMirroringApp
        RuntimeState.currentForegroundPkg = pkg
        RuntimeState.currentForegroundClass = cls

        val inMirror = isMirroringPackage(pkg)
        RuntimeState.isInMirroringApp = inMirror

        if (inMirror != prevMirror) {
            if (inMirror) {
                RuntimeState.log("检测到进入投屏画面 ($pkg)，自动开启遥控接管")
            } else {
                RuntimeState.log("已离开投屏画面 ($pkg)，遥控器恢复控制盒子")
            }
        }
    }

    private fun isMirroringPackage(pkg: String?): Boolean {
        if (pkg.isNullOrBlank()) return false
        val lowerPkg = pkg.lowercase()
        return mirroringPackages.any { lowerPkg.startsWith(it) }
    }

    private fun refreshMirroringStateFromActiveWindow(): Boolean {
        val activePkg = try {
            rootInActiveWindow?.packageName?.toString()
        } catch (_: Exception) {
            null
        }
        if (!activePkg.isNullOrBlank() && activePkg != "android" && activePkg != "com.android.systemui") {
            val inMirror = isMirroringPackage(activePkg)
            if (inMirror != RuntimeState.isInMirroringApp) {
                RuntimeState.isInMirroringApp = inMirror
                RuntimeState.currentForegroundPkg = activePkg
                RuntimeState.log(
                    if (inMirror) "检测到投屏前台 ($activePkg)，自动开启遥控接管"
                    else "已离开投屏 ($activePkg)，遥控器恢复控制盒子"
                )
            }
            return inMirror
        }
        return RuntimeState.isInMirroringApp
    }

    override fun onInterrupt() {
        RuntimeState.accessibilityRunning = false
        RuntimeState.log("无障碍遥控接管服务被中断")
    }

    override fun onDestroy() {
        super.onDestroy()
        RuntimeState.accessibilityRunning = false
        RuntimeState.isInMirroringApp = false
        scope.cancel()
        RuntimeState.log("无障碍遥控接管服务已停止")
    }

    override fun onKeyEvent(event: KeyEvent): Boolean {
        if (!::config.isInitialized) return false

        // Never hijack D-pad when our own control app is in the foreground
        if (RuntimeState.isSelfInForeground) {
            return false
        }

        // Auto-detect: only intercept remote keys while an AirPlay/mirroring screen is in foreground!
        // Also query rootInActiveWindow live so it works immediately even if mirroring was already open.
        if (!refreshMirroringStateFromActiveWindow()) {
            return false
        }

        // Inside mirroring, MENU key lets the user manually pause/resume remote interception anytime
        if (event.keyCode == KeyEvent.KEYCODE_MENU) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                val nextState = !config.remoteInterceptEnabled
                config.remoteInterceptEnabled = nextState
                val text = if (nextState) "已恢复 iPhone 遥控接管" else "已临时暂停遥控接管（恢复控制盒子）"
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
