package com.jason.dbboxcontroller

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.SocketTimeoutException

data class Esp32Reply(
    val ok: Boolean,
    val ip: String,
    val bleConnected: Boolean,
    val mode: String,
    val raw: String
)

class Esp32Client(private val config: AppConfig) {

    suspend fun discover(timeoutMs: Int = 1800): Esp32Reply? = withContext(Dispatchers.IO) {
        val port = config.esp32Port
        val payload = "DISCOVER".toByteArray(Charsets.UTF_8)
        var socket: DatagramSocket? = null
        try {
            socket = DatagramSocket().apply {
                broadcast = true
                soTimeout = timeoutMs
            }

            val targets = buildBroadcastTargets()
            for (addr in targets) {
                try {
                    val packet = DatagramPacket(payload, payload.size, addr, port)
                    socket.send(packet)
                } catch (_: Exception) {
                }
            }

            val buf = ByteArray(512)
            val rx = DatagramPacket(buf, buf.size)
            socket.receive(rx)
            val text = String(rx.data, 0, rx.length, Charsets.UTF_8)
            val senderIp = rx.address.hostAddress ?: ""
            val reply = parseReply(text, senderIp)
            if (reply != null) {
                config.esp32Ip = reply.ip
                RuntimeState.esp32Reachable = true
                RuntimeState.esp32BleConnected = reply.bleConnected
                RuntimeState.esp32Mode = reply.mode
                RuntimeState.esp32LastSeenIp = reply.ip
                RuntimeState.log("发现 ESP32: ${reply.ip} (蓝牙:${if (reply.bleConnected) "已连" else "未连"})")
            }
            reply
        } catch (_: SocketTimeoutException) {
            RuntimeState.esp32Reachable = false
            RuntimeState.log("局域网未扫描到 ESP32-S3 (UDP:$port)")
            null
        } catch (e: Exception) {
            RuntimeState.log("扫描 ESP32 失败: ${e.message}")
            null
        } finally {
            socket?.close()
            RuntimeState.notifyChanged()
        }
    }

    suspend fun sendCommand(command: String, retries: Int = 2): Esp32Reply? = withContext(Dispatchers.IO) {
        var ip = config.esp32Ip
        if (ip.isBlank()) {
            val found = discover(1200)
            ip = found?.ip ?: return@withContext null
        }

        val port = config.esp32Port
        val payload = command.toByteArray(Charsets.UTF_8)
        var socket: DatagramSocket? = null
        try {
            socket = DatagramSocket().apply {
                soTimeout = 700
            }
            val addr = InetAddress.getByName(ip)
            val buf = ByteArray(512)
            repeat(retries.coerceAtLeast(1)) {
                try {
                    socket.send(DatagramPacket(payload, payload.size, addr, port))
                    val rx = DatagramPacket(buf, buf.size)
                    socket.receive(rx)
                    val text = String(rx.data, 0, rx.length, Charsets.UTF_8)
                    val reply = parseReply(text, ip)
                    if (reply != null) {
                        RuntimeState.esp32Reachable = true
                        RuntimeState.esp32BleConnected = reply.bleConnected
                        RuntimeState.esp32Mode = reply.mode
                        RuntimeState.esp32LastSeenIp = reply.ip
                        RuntimeState.notifyChanged()
                        return@withContext reply
                    }
                } catch (_: SocketTimeoutException) {
                    // Retry once
                }
            }
            RuntimeState.esp32Reachable = false
            RuntimeState.log("发送指令 [$command] 到 $ip 超时")
            null
        } catch (e: Exception) {
            RuntimeState.esp32Reachable = false
            RuntimeState.log("发送指令 [$command] 异常: ${e.message}")
            null
        } finally {
            socket?.close()
            RuntimeState.notifyChanged()
        }
    }

    suspend fun nextVideo(): Esp32Reply? = sendCommand("SWIPE_UP")
    suspend fun prevVideo(): Esp32Reply? = sendCommand("SWIPE_DOWN")
    suspend fun tapPause(): Esp32Reply? = sendCommand("TAP")
    suspend fun doubleTapLike(): Esp32Reply? = sendCommand("DOUBLE_TAP")
    suspend fun queryStatus(): Esp32Reply? = sendCommand("STATUS", retries = 1)

    private fun parseReply(raw: String, fallbackIp: String): Esp32Reply? {
        return try {
            val json = JSONObject(raw)
            val ip = json.optString("ip", fallbackIp).ifBlank { fallbackIp }
            Esp32Reply(
                ok = json.optBoolean("ok", false),
                ip = ip,
                bleConnected = json.optBoolean("connected", false),
                mode = json.optString("mode", "video"),
                raw = raw
            )
        } catch (_: Exception) {
            null
        }
    }

    private fun buildBroadcastTargets(): List<InetAddress> {
        val list = mutableListOf<InetAddress>()
        try {
            list.add(InetAddress.getByName("255.255.255.255"))
        } catch (_: Exception) {
        }
        try {
            val ifaces = NetworkInterface.getNetworkInterfaces() ?: return list
            for (iface in ifaces) {
                if (!iface.isUp || iface.isLoopback) continue
                for (ia in iface.interfaceAddresses) {
                    val bc = ia.broadcast
                    if (bc is Inet4Address && !list.contains(bc)) {
                        list.add(bc)
                    }
                }
            }
        } catch (_: Exception) {
        }
        return list
    }
}
