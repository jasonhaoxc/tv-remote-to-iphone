package com.jason.dbboxcontroller

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import java.net.Inet4Address
import java.net.NetworkInterface

class MainActivity : AppCompatActivity() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private lateinit var config: AppConfig
    private lateinit var esp32: Esp32Client

    private lateinit var tvWebUrl: TextView
    private lateinit var tvStatusSummary: TextView
    private lateinit var tvOcrPreview: TextView
    private lateinit var tvLogs: TextView
    private lateinit var btnScreenMonitor: Button
    private lateinit var btnToggleRemote: Button
    private lateinit var btnToggleAutoSkip: Button

    private val screenCaptureLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == Activity.RESULT_OK && result.data != null) {
                val svcIntent = Intent(this, ScreenMonitorService::class.java).apply {
                    action = ScreenMonitorService.ACTION_START
                    putExtra(ScreenMonitorService.EXTRA_RESULT_CODE, result.resultCode)
                    putExtra(ScreenMonitorService.EXTRA_RESULT_DATA, result.data)
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    startForegroundService(svcIntent)
                } else {
                    startService(svcIntent)
                }
            } else {
                RuntimeState.log("用户取消了投屏截屏授权")
            }
        }

    private val stateListener: () -> Unit = {
        runOnUiThread { refreshUi() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        config = (application as ControllerApp).config
        esp32 = Esp32Client(config)

        tvWebUrl = findViewById(R.id.tvWebUrl)
        tvStatusSummary = findViewById(R.id.tvStatusSummary)
        tvOcrPreview = findViewById(R.id.tvOcrPreview)
        tvLogs = findViewById(R.id.tvLogs)
        btnScreenMonitor = findViewById(R.id.btnScreenMonitor)
        btnToggleRemote = findViewById(R.id.btnToggleRemote)
        btnToggleAutoSkip = findViewById(R.id.btnToggleAutoSkip)

        findViewById<Button>(R.id.btnDiscover).setOnClickListener {
            scope.launch {
                RuntimeState.log("正在局域网扫描 Xiaozhi-HID...")
                esp32.discover()
                refreshUi()
            }
        }

        findViewById<Button>(R.id.btnAccessibility).setOnClickListener {
            try {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            } catch (e: Exception) {
                RuntimeState.log("无法打开无障碍设置: ${e.message}")
            }
        }

        btnScreenMonitor.setOnClickListener {
            if (RuntimeState.screenMonitorRunning) {
                val stopIntent = Intent(this, ScreenMonitorService::class.java).apply {
                    action = ScreenMonitorService.ACTION_STOP
                }
                startService(stopIntent)
            } else {
                val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
                screenCaptureLauncher.launch(mgr.createScreenCaptureIntent())
            }
        }

        btnToggleRemote.setOnClickListener {
            config.remoteInterceptEnabled = !config.remoteInterceptEnabled
            RuntimeState.log("遥控器接管已${if (config.remoteInterceptEnabled) "开启" else "关闭"}")
            refreshUi()
        }

        btnToggleAutoSkip.setOnClickListener {
            config.autoSkipAdEnabled = !config.autoSkipAdEnabled
            RuntimeState.log("自动跳过广告已${if (config.autoSkipAdEnabled) "开启" else "关闭"}")
            refreshUi()
        }

        findViewById<Button>(R.id.btnTestPrev).setOnClickListener {
            scope.launch { esp32.prevVideo() }
        }
        findViewById<Button>(R.id.btnTestNext).setOnClickListener {
            scope.launch { esp32.nextVideo() }
        }
        findViewById<Button>(R.id.btnTestTap).setOnClickListener {
            scope.launch { esp32.tapPause() }
        }

        RuntimeState.addListener(stateListener)
        findViewById<Button>(R.id.btnDiscover).requestFocus()

        // Auto-discover ESP32 on startup
        scope.launch {
            if (config.esp32Ip.isBlank()) {
                esp32.discover()
            } else {
                esp32.queryStatus()
            }
            refreshUi()
        }

        // Auto-start screen monitor on launch (silent & zero-popup once PROJECT_MEDIA is allowed via appops)
        if (!RuntimeState.screenMonitorRunning) {
            val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
            screenCaptureLauncher.launch(mgr.createScreenCaptureIntent())
        }
        refreshUi()
    }

    override fun onResume() {
        super.onResume()
        RuntimeState.isSelfInForeground = true
        refreshUi()
    }

    override fun onPause() {
        super.onPause()
        RuntimeState.isSelfInForeground = false
    }

    override fun onDestroy() {
        super.onDestroy()
        RuntimeState.removeListener(stateListener)
        scope.cancel()
    }

    private fun refreshUi() {
        val localIp = getLocalIpv4()
        tvWebUrl.text = "浏览器校准: http://$localIp:8088"

        val espText = if (RuntimeState.esp32Reachable) {
            "${config.esp32Ip} (在线 · 蓝牙${if (RuntimeState.esp32BleConnected) "已连iPhone" else "未连"})"
        } else if (config.esp32Ip.isNotBlank()) {
            "${config.esp32Ip} (离线/待检测)"
        } else {
            "未发现 (请点下方搜索)"
        }
        val accText = if (RuntimeState.accessibilityRunning) "已运行" else "未开启"
        val monText = if (RuntimeState.screenMonitorRunning) {
            "运行中 (${RuntimeState.latestFrameWidth}x${RuntimeState.latestFrameHeight})"
        } else {
            "未启动"
        }
        tvStatusSummary.text = "ESP32-S3: $espText\n无障碍接管: $accText\n投屏截屏监控: $monText"

        btnScreenMonitor.text = if (RuntimeState.screenMonitorRunning) {
            "3. 停止投屏截屏与自动跳广告"
        } else {
            "3. 开启投屏截屏与自动跳广告"
        }
        btnToggleRemote.text = "遥控接管: ${if (config.remoteInterceptEnabled) "开" else "关"}"
        btnToggleAutoSkip.text = "自动跳广告: ${if (config.autoSkipAdEnabled) "开" else "关"}"

        val pOcr = RuntimeState.lastPortraitOcr.ifBlank { "(空)" }
        val lOcr = RuntimeState.lastLandscapeOcr.ifBlank { "(空)" }
        tvOcrPreview.text = "状态: ${RuntimeState.detectorState} | 已自动跳广告: ${RuntimeState.totalSkippedAds}\n" +
            "竖屏OCR: $pOcr\n横屏OCR: $lOcr"

        val logLines = RuntimeState.getLogs().take(20)
        tvLogs.text = if (logLines.isEmpty()) "等待启动..." else logLines.joinToString("\n")
    }

    private fun getLocalIpv4(): String {
        try {
            val ifaces = NetworkInterface.getNetworkInterfaces() ?: return "盒子IP"
            for (iface in ifaces) {
                if (!iface.isUp || iface.isLoopback) continue
                for (addr in iface.inetAddresses) {
                    if (addr is Inet4Address && !addr.isLoopbackAddress) {
                        return addr.hostAddress ?: continue
                    }
                }
            }
        } catch (_: Exception) {
        }
        return "盒子IP"
    }
}
