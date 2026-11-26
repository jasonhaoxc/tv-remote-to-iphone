package com.jason.dbboxcontroller

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.Image
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.DisplayMetrics
import android.view.WindowManager
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream

class ScreenMonitorService : Service() {

    companion object {
        const val ACTION_START = "com.jason.dbboxcontroller.ACTION_START_MONITOR"
        const val ACTION_STOP = "com.jason.dbboxcontroller.ACTION_STOP_MONITOR"
        const val EXTRA_RESULT_CODE = "result_code"
        const val EXTRA_RESULT_DATA = "result_data"
        private const val CHANNEL_ID = "screen_monitor_channel"
        private const val NOTIF_ID = 1001
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var config: AppConfig
    private lateinit var esp32: Esp32Client
    private lateinit var detector: AdDetectorEngine

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var monitorJob: Job? = null

    private var captureWidth: Int = 1280
    private var captureHeight: Int = 720
    private var captureDpi: Int = 160

    override fun onCreate() {
        super.onCreate()
        config = AppConfig(this)
        esp32 = Esp32Client(config)
        detector = AdDetectorEngine(config, esp32)
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopCapture()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED)
                @Suppress("DEPRECATION")
                val data: Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent::class.java)
                } else {
                    intent.getParcelableExtra(EXTRA_RESULT_DATA)
                }
                if (resultCode == Activity.RESULT_OK && data != null) {
                    startAsForeground()
                    startCapture(resultCode, data)
                } else {
                    RuntimeState.log("截屏授权无效，无法启动监控")
                    stopSelf()
                }
            }
        }
        return START_STICKY
    }

    private fun startAsForeground() {
        val openIntent = Intent(this, MainActivity::class.java)
        val pending = PendingIntent.getActivity(
            this, 0, openIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val notif: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("iPhone 投屏广告监控运行中")
            .setContentText("正在识别红果短剧竖屏/横屏广告并自动跳过")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pending)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(NOTIF_ID, notif)
        }
    }

    private fun startCapture(resultCode: Int, data: Intent) {
        stopCapture()
        val wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()
        @Suppress("DEPRECATION")
        wm.defaultDisplay.getRealMetrics(metrics)

        val srcW = metrics.widthPixels.coerceAtLeast(640)
        val srcH = metrics.heightPixels.coerceAtLeast(360)
        val scale = if (srcW > 1280) 1280f / srcW else 1f
        captureWidth = ((srcW * scale).toInt() / 2) * 2
        captureHeight = ((srcH * scale).toInt() / 2) * 2
        captureDpi = metrics.densityDpi

        val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val mp = mgr.getMediaProjection(resultCode, data)
        if (mp == null) {
            RuntimeState.log("创建 MediaProjection 失败")
            stopSelf()
            return
        }

        mp.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                RuntimeState.log("系统停止了投屏截屏会话")
                stopCapture()
                stopSelf()
            }
        }, null)

        val reader = ImageReader.newInstance(captureWidth, captureHeight, PixelFormat.RGBA_8888, 2)
        val vd = mp.createVirtualDisplay(
            "DbboxAdMonitor",
            captureWidth,
            captureHeight,
            captureDpi,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            reader.surface,
            null,
            null
        )

        mediaProjection = mp
        imageReader = reader
        virtualDisplay = vd
        RuntimeState.screenMonitorRunning = true
        RuntimeState.latestFrameWidth = captureWidth
        RuntimeState.latestFrameHeight = captureHeight
        RuntimeState.log("投屏截屏监控已启动 (${captureWidth}x${captureHeight})")

        monitorJob = scope.launch {
            while (isActive) {
                val interval = if (detector.isInCountdown()) 500L else config.pollIntervalMs
                try {
                    val img = imageReader?.acquireLatestImage()
                    if (img != null) {
                        var bmp: Bitmap? = null
                        try {
                            bmp = imageToBitmap(img)
                            if (bmp != null) {
                                savePreviewJpeg(bmp)
                                if (config.autoSkipAdEnabled) {
                                    detector.processFrame(bmp)
                                }
                            }
                        } finally {
                            bmp?.recycle()
                            img.close()
                        }
                    }
                } catch (e: Exception) {
                    RuntimeState.log("截屏分析异常: ${e.message}")
                }
                delay(interval)
            }
        }
    }

    private fun imageToBitmap(image: Image): Bitmap? {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowPadding = rowStride - pixelStride * captureWidth
        val paddedWidth = captureWidth + rowPadding / pixelStride
        val raw = Bitmap.createBitmap(paddedWidth, captureHeight, Bitmap.Config.ARGB_8888)
        raw.copyPixelsFromBuffer(buffer)
        return if (paddedWidth == captureWidth) {
            raw
        } else {
            val cropped = Bitmap.createBitmap(raw, 0, 0, captureWidth, captureHeight)
            raw.recycle()
            cropped
        }
    }

    private fun savePreviewJpeg(bmp: Bitmap) {
        val out = ByteArrayOutputStream()
        bmp.compress(Bitmap.CompressFormat.JPEG, 65, out)
        RuntimeState.latestScreenshotJpeg = out.toByteArray()
    }

    private fun stopCapture() {
        monitorJob?.cancel()
        monitorJob = null
        detector.reset()
        try {
            virtualDisplay?.release()
        } catch (_: Exception) {
        }
        virtualDisplay = null
        try {
            imageReader?.close()
        } catch (_: Exception) {
        }
        imageReader = null
        try {
            mediaProjection?.stop()
        } catch (_: Exception) {
        }
        mediaProjection = null
        RuntimeState.screenMonitorRunning = false
        RuntimeState.notifyChanged()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopCapture()
        detector.close()
        scope.cancel()
        RuntimeState.log("投屏截屏监控已停止")
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID,
                "投屏广告监控",
                NotificationManager.IMPORTANCE_LOW
            )
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(ch)
        }
    }
}
