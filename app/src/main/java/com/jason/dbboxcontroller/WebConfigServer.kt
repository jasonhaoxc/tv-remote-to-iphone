package com.jason.dbboxcontroller

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.net.ServerSocket
import java.net.Socket

// Lightweight built-in HTTP server (port 8088) for visual ROI calibration and live debugging
// from a phone or computer browser on the same LAN.
class WebConfigServer(
    private val context: Context,
    private val config: AppConfig,
    private val port: Int = 8088
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val esp32 = Esp32Client(config)
    @Volatile private var running = false
    private var serverSocket: ServerSocket? = null

    fun start() {
        if (running) return
        running = true
        scope.launch {
            try {
                val ss = ServerSocket(port)
                serverSocket = ss
                RuntimeState.log("Web 校准面板已启动: 端口 $port")
                while (running) {
                    val client = ss.accept()
                    scope.launch { handleClient(client) }
                }
            } catch (e: Exception) {
                RuntimeState.log("Web 校准面板异常: ${e.message}")
            }
        }
    }

    fun stop() {
        running = false
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
    }

    private fun handleClient(socket: Socket) {
        socket.use { s ->
            s.soTimeout = 5000
            val input = BufferedInputStream(s.getInputStream())
            val output = BufferedOutputStream(s.getOutputStream())

            val headerBytes = ByteArrayOutputStream()
            var prev3 = 0
            var prev2 = 0
            var prev1 = 0
            while (true) {
                val b = input.read()
                if (b == -1) return
                headerBytes.write(b)
                if (prev3 == '\r'.code && prev2 == '\n'.code && prev1 == '\r'.code && b == '\n'.code) {
                    break
                }
                prev3 = prev2
                prev2 = prev1
                prev1 = b
                if (headerBytes.size() > 16384) return
            }

            val headerStr = headerBytes.toString("UTF-8")
            val lines = headerStr.split("\r\n")
            if (lines.isEmpty()) return
            val reqParts = lines[0].split(" ")
            if (reqParts.size < 2) return
            val method = reqParts[0].uppercase()
            val fullPath = reqParts[1]
            val path = fullPath.substringBefore('?')

            var contentLength = 0
            for (i in 1 until lines.size) {
                val line = lines[i]
                if (line.lowercase().startsWith("content-length:")) {
                    contentLength = line.substringAfter(':').trim().toIntOrNull() ?: 0
                }
            }

            val body = if (contentLength > 0 && contentLength <= 65536) {
                val buf = ByteArray(contentLength)
                var readTotal = 0
                while (readTotal < contentLength) {
                    val r = input.read(buf, readTotal, contentLength - readTotal)
                    if (r <= 0) break
                    readTotal += r
                }
                String(buf, 0, readTotal, Charsets.UTF_8)
            } else {
                ""
            }

            when {
                method == "GET" && (path == "/" || path == "/index.html") -> {
                    val html = context.assets.open("web/index.html").use { it.readBytes() }
                    writeResponse(output, 200, "text/html; charset=utf-8", html)
                }
                method == "GET" && path == "/api/status" -> {
                    val json = buildStatusJson().toString().toByteArray(Charsets.UTF_8)
                    writeResponse(output, 200, "application/json; charset=utf-8", json)
                }
                method == "GET" && path == "/api/screenshot.jpg" -> {
                    val jpg = RuntimeState.latestScreenshotJpeg
                    if (jpg != null) {
                        writeResponse(output, 200, "image/jpeg", jpg)
                    } else {
                        writeResponse(
                            output, 404, "text/plain; charset=utf-8",
                            "尚未开启投屏截屏监控".toByteArray(Charsets.UTF_8)
                        )
                    }
                }
                method == "POST" && path == "/api/config" -> {
                    try {
                        val obj = JSONObject(body)
                        config.updateFromJson(obj)
                        RuntimeState.log("已通过 Web 面板更新配置")
                        val resp = JSONObject().put("ok", true).put("config", config.toJson())
                        writeResponse(output, 200, "application/json; charset=utf-8", resp.toString().toByteArray())
                    } catch (e: Exception) {
                        val resp = JSONObject().put("ok", false).put("error", e.message)
                        writeResponse(output, 400, "application/json; charset=utf-8", resp.toString().toByteArray())
                    }
                }
                method == "POST" && path == "/api/discover" -> {
                    val reply = runBlocking { esp32.discover() }
                    val resp = JSONObject()
                        .put("ok", reply != null)
                        .put("ip", reply?.ip ?: "")
                        .put("bleConnected", reply?.bleConnected ?: false)
                    writeResponse(output, 200, "application/json; charset=utf-8", resp.toString().toByteArray())
                }
                method == "POST" && path == "/api/action" -> {
                    val cmd = try {
                        JSONObject(body).optString("cmd", "SWIPE_UP")
                    } catch (_: Exception) {
                        "SWIPE_UP"
                    }
                    val reply = runBlocking { esp32.sendCommand(cmd) }
                    val resp = JSONObject()
                        .put("ok", reply?.ok == true)
                        .put("cmd", cmd)
                    writeResponse(output, 200, "application/json; charset=utf-8", resp.toString().toByteArray())
                }
                else -> {
                    writeResponse(output, 404, "text/plain; charset=utf-8", "Not Found".toByteArray())
                }
            }
        }
    }

    private fun buildStatusJson(): JSONObject {
        return JSONObject().apply {
            put("accessibilityRunning", RuntimeState.accessibilityRunning)
            put("screenMonitorRunning", RuntimeState.screenMonitorRunning)
            put("esp32Reachable", RuntimeState.esp32Reachable)
            put("esp32BleConnected", RuntimeState.esp32BleConnected)
            put("esp32Mode", RuntimeState.esp32Mode)
            put("detectorState", RuntimeState.detectorState)
            put("lastPortraitOcr", RuntimeState.lastPortraitOcr)
            put("lastLandscapeOcr", RuntimeState.lastLandscapeOcr)
            put("lastCountdownSec", RuntimeState.lastCountdownSec ?: JSONObject.NULL)
            put("totalSkippedAds", RuntimeState.totalSkippedAds)
            put("frameWidth", RuntimeState.latestFrameWidth)
            put("frameHeight", RuntimeState.latestFrameHeight)
            put("hasScreenshot", RuntimeState.latestScreenshotJpeg != null)
            put("config", config.toJson())
            put("logs", JSONArray(RuntimeState.getLogs().take(30)))
        }
    }

    private fun writeResponse(
        output: BufferedOutputStream,
        status: Int,
        contentType: String,
        body: ByteArray
    ) {
        val statusText = when (status) {
            200 -> "OK"
            400 -> "Bad Request"
            else -> "Not Found"
        }
        val header = buildString {
            append("HTTP/1.1 $status $statusText\r\n")
            append("Content-Type: $contentType\r\n")
            append("Content-Length: ${body.size}\r\n")
            append("Cache-Control: no-store\r\n")
            append("Connection: close\r\n\r\n")
        }
        output.write(header.toByteArray(Charsets.UTF_8))
        output.write(body)
        output.flush()
    }
}
