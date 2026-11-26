package com.jason.dbboxcontroller

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONObject

data class RoiRect(
    val enabled: Boolean,
    val left: Float,
    val top: Float,
    val right: Float,
    val bottom: Float
) {
    fun clamped(): RoiRect {
        val l = left.coerceIn(0f, 0.98f)
        val t = top.coerceIn(0f, 0.98f)
        val r = right.coerceIn(l + 0.02f, 1f)
        val b = bottom.coerceIn(t + 0.02f, 1f)
        return RoiRect(enabled, l, t, r, b)
    }

    fun toJson(): JSONObject = JSONObject().apply {
        put("enabled", enabled)
        put("left", left.toDouble())
        put("top", top.toDouble())
        put("right", right.toDouble())
        put("bottom", bottom.toDouble())
    }

    companion object {
        fun fromJson(obj: JSONObject, fallback: RoiRect): RoiRect {
            return RoiRect(
                enabled = obj.optBoolean("enabled", fallback.enabled),
                left = obj.optDouble("left", fallback.left.toDouble()).toFloat(),
                top = obj.optDouble("top", fallback.top.toDouble()).toFloat(),
                right = obj.optDouble("right", fallback.right.toDouble()).toFloat(),
                bottom = obj.optDouble("bottom", fallback.bottom.toDouble()).toFloat()
            ).clamped()
        }
    }
}

class AppConfig(context: Context) {
    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences("dbbox_controller_cfg", Context.MODE_PRIVATE)

    var esp32Ip: String
        get() = prefs.getString("esp32_ip", "") ?: ""
        set(value) = prefs.edit().putString("esp32_ip", value.trim()).apply()

    var esp32Port: Int
        get() = prefs.getInt("esp32_port", 4210)
        set(value) = prefs.edit().putInt("esp32_port", value).apply()

    var remoteInterceptEnabled: Boolean
        get() = prefs.getBoolean("remote_intercept_enabled", true)
        set(value) = prefs.edit().putBoolean("remote_intercept_enabled", value).apply()

    var autoSkipAdEnabled: Boolean
        get() = prefs.getBoolean("auto_skip_ad_enabled", true)
        set(value) = prefs.edit().putBoolean("auto_skip_ad_enabled", value).apply()

    var pollIntervalMs: Long
        get() = prefs.getLong("poll_interval_ms", 800L)
        set(value) = prefs.edit().putLong("poll_interval_ms", value.coerceIn(300L, 5000L)).apply()

    var cooldownMs: Long
        get() = prefs.getLong("cooldown_ms", 4500L)
        set(value) = prefs.edit().putLong("cooldown_ms", value.coerceIn(1500L, 15000L)).apply()

    var confirmFrames: Int
        get() = prefs.getInt("confirm_frames", 2)
        set(value) = prefs.edit().putInt("confirm_frames", value.coerceIn(1, 5)).apply()

    // Keywords indicating an ad is currently showing (comma-separated)
    var adKeywords: String
        get() = prefs.getString("ad_keywords", "广告,上滑继续观看,上滑继续,跳过,赞助,秒后,s后,倒计时,可跳过,可关闭,奖励,继续观看") ?: ""
        set(value) = prefs.edit().putString("ad_keywords", value.trim()).apply()

    // Keywords indicating countdown has ended and ad can be skipped immediately
    var finishKeywords: String
        get() = prefs.getString("finish_keywords", "上滑继续观看,上滑继续,已获得,跳过广告,点击跳过,可关闭,上滑跳过") ?: ""
        set(value) = prefs.edit().putString("finish_keywords", value.trim()).apply()

    // Default Portrait ROI (iPhone mirrored in center of 16:9 TV, top-right/bottom-left of phone area)
    var portraitRoi: RoiRect
        get() = RoiRect(
            enabled = prefs.getBoolean("portrait_roi_enabled", true),
            left = prefs.getFloat("portrait_roi_l", 0.34f),
            top = prefs.getFloat("portrait_roi_t", 0.04f),
            right = prefs.getFloat("portrait_roi_r", 0.66f),
            bottom = prefs.getFloat("portrait_roi_b", 0.16f)
        ).clamped()
        set(value) {
            val c = value.clamped()
            prefs.edit()
                .putBoolean("portrait_roi_enabled", c.enabled)
                .putFloat("portrait_roi_l", c.left)
                .putFloat("portrait_roi_t", c.top)
                .putFloat("portrait_roi_r", c.right)
                .putFloat("portrait_roi_b", c.bottom)
                .apply()
        }

    // Default Landscape ROI (fullscreen landscape drama on 16:9 TV, top-right ad pill)
    var landscapeRoi: RoiRect
        get() = RoiRect(
            enabled = prefs.getBoolean("landscape_roi_enabled", true),
            left = prefs.getFloat("landscape_roi_l", 0.65f),
            top = prefs.getFloat("landscape_roi_t", 0.11f),
            right = prefs.getFloat("landscape_roi_r", 0.95f),
            bottom = prefs.getFloat("landscape_roi_b", 0.24f)
        ).clamped()
        set(value) {
            val c = value.clamped()
            prefs.edit()
                .putBoolean("landscape_roi_enabled", c.enabled)
                .putFloat("landscape_roi_l", c.left)
                .putFloat("landscape_roi_t", c.top)
                .putFloat("landscape_roi_r", c.right)
                .putFloat("landscape_roi_b", c.bottom)
                .apply()
        }

    fun adKeywordList(): List<String> =
        adKeywords.split(",", "，", "\n").map { it.trim() }.filter { it.isNotEmpty() }

    fun finishKeywordList(): List<String> =
        finishKeywords.split(",", "，", "\n").map { it.trim() }.filter { it.isNotEmpty() }

    fun toJson(): JSONObject = JSONObject().apply {
        put("esp32Ip", esp32Ip)
        put("esp32Port", esp32Port)
        put("remoteInterceptEnabled", remoteInterceptEnabled)
        put("autoSkipAdEnabled", autoSkipAdEnabled)
        put("pollIntervalMs", pollIntervalMs)
        put("cooldownMs", cooldownMs)
        put("confirmFrames", confirmFrames)
        put("adKeywords", adKeywords)
        put("finishKeywords", finishKeywords)
        put("portraitRoi", portraitRoi.toJson())
        put("landscapeRoi", landscapeRoi.toJson())
    }

    fun updateFromJson(obj: JSONObject) {
        if (obj.has("esp32Ip")) esp32Ip = obj.optString("esp32Ip", esp32Ip)
        if (obj.has("esp32Port")) esp32Port = obj.optInt("esp32Port", esp32Port)
        if (obj.has("remoteInterceptEnabled")) remoteInterceptEnabled = obj.optBoolean("remoteInterceptEnabled", remoteInterceptEnabled)
        if (obj.has("autoSkipAdEnabled")) autoSkipAdEnabled = obj.optBoolean("autoSkipAdEnabled", autoSkipAdEnabled)
        if (obj.has("pollIntervalMs")) pollIntervalMs = obj.optLong("pollIntervalMs", pollIntervalMs)
        if (obj.has("cooldownMs")) cooldownMs = obj.optLong("cooldownMs", cooldownMs)
        if (obj.has("confirmFrames")) confirmFrames = obj.optInt("confirmFrames", confirmFrames)
        if (obj.has("adKeywords")) adKeywords = obj.optString("adKeywords", adKeywords)
        if (obj.has("finishKeywords")) finishKeywords = obj.optString("finishKeywords", finishKeywords)
        obj.optJSONObject("portraitRoi")?.let { portraitRoi = RoiRect.fromJson(it, portraitRoi) }
        obj.optJSONObject("landscapeRoi")?.let { landscapeRoi = RoiRect.fromJson(it, landscapeRoi) }
    }
}
