package com.jason.dbboxcontroller

import android.graphics.Bitmap
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.text.TextRecognition
import com.google.mlkit.vision.text.chinese.ChineseTextRecognizerOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.regex.Pattern

class AdDetectorEngine(
    private val config: AppConfig,
    private val esp32: Esp32Client
) {
    private enum class State {
        IDLE,
        COUNTDOWN,
        COOLDOWN
    }

    private val recognizer =
        TextRecognition.getClient(ChineseTextRecognizerOptions.Builder().build())

    private var state: State = State.IDLE
    private var activeRoi: String = "portrait"
    private var adConfirmCount: Int = 0
    private var lastCountdownSec: Int = 0
    private var countdownDeadlineMs: Long = 0L
    private var countdownMissingFrames: Int = 0
    private var cooldownUntilMs: Long = 0L

    // Matches "15s", "15S", "15秒", "15 s", "倒计时15", "剩余15"
    private val secSuffixPattern = Pattern.compile("(\\d{1,2})\\s*[sS秒]")
    private val secPrefixPattern = Pattern.compile("(?:倒计时|剩余|还有|广告)\\s*[:：]?\\s*(\\d{1,2})(?!\\d)")
    private val mmssPattern = Pattern.compile("\\b0?(\\d):(\\d{2})\\b")
    private val standaloneNumPattern = Pattern.compile("(?<!\\d)(\\d{1,2})(?!\\d)")

    fun isInCountdown(): Boolean = state == State.COUNTDOWN

    fun reset() {
        state = State.IDLE
        adConfirmCount = 0
        lastCountdownSec = 0
        countdownDeadlineMs = 0L
        countdownMissingFrames = 0
        cooldownUntilMs = 0L
        RuntimeState.detectorState = "IDLE"
        RuntimeState.lastCountdownSec = null
        RuntimeState.notifyChanged()
    }

    fun close() {
        try {
            recognizer.close()
        } catch (_: Exception) {
        }
    }

    suspend fun processFrame(fullFrame: Bitmap) = withContext(Dispatchers.Default) {
        val now = System.currentTimeMillis()

        if (state == State.COOLDOWN) {
            if (now < cooldownUntilMs) {
                return@withContext
            }
            state = State.IDLE
            adConfirmCount = 0
            RuntimeState.detectorState = "IDLE"
            RuntimeState.lastCountdownSec = null
            RuntimeState.notifyChanged()
        }

        val adKeywords = config.adKeywordList()
        val finishKeywords = config.finishKeywordList()

        if (state == State.COUNTDOWN) {
            val roi = if (activeRoi == "portrait") config.portraitRoi else config.landscapeRoi
            val text = ocrRoi(fullFrame, roi)
            if (activeRoi == "portrait") {
                RuntimeState.lastPortraitOcr = text
            } else {
                RuntimeState.lastLandscapeOcr = text
            }

            val sec = extractCountdownSeconds(text, containsAny(text, adKeywords))
            val isFinish = containsAny(text, finishKeywords)
            val isStillAd = containsAny(text, adKeywords)

            if (sec != null && sec > 0) {
                if (sec != lastCountdownSec) {
                    RuntimeState.log("广告读秒中 ($activeRoi): 剩余 ${sec}s [$text]")
                }
                lastCountdownSec = sec
                countdownMissingFrames = 0
                countdownDeadlineMs = now + sec * 1000L + 1000L
                RuntimeState.lastCountdownSec = sec
                RuntimeState.detectorState = "COUNTDOWN(${sec}s)"
                RuntimeState.notifyChanged()
                return@withContext
            }

            val shouldSkipNow = when {
                sec == 0 -> {
                    RuntimeState.log("读秒归零 [$text]，准备跳过广告")
                    true
                }
                isFinish -> {
                    RuntimeState.log("识别到结束提示 [$text]，准备跳过广告")
                    true
                }
                now >= countdownDeadlineMs -> {
                    RuntimeState.log("倒计时到达兜底时间，准备跳过广告")
                    true
                }
                !isStillAd && lastCountdownSec <= 2 -> {
                    countdownMissingFrames++
                    if (countdownMissingFrames >= 2) {
                        RuntimeState.log("读秒提示消失，准备跳过广告")
                        true
                    } else {
                        false
                    }
                }
                else -> false
            }

            if (shouldSkipNow) {
                triggerSkip("读秒广告结束")
            }
            return@withContext
        }

        // State == IDLE: check both Portrait and Landscape ROIs
        val pRoi = config.portraitRoi
        val lRoi = config.landscapeRoi

        val pText = if (pRoi.enabled) ocrRoi(fullFrame, pRoi) else ""
        val lText = if (lRoi.enabled) ocrRoi(fullFrame, lRoi) else ""

        RuntimeState.lastPortraitOcr = pText
        RuntimeState.lastLandscapeOcr = lText
        RuntimeState.notifyChanged()

        val matchPortrait = pRoi.enabled && containsAny(pText, adKeywords)
        val matchLandscape = lRoi.enabled && containsAny(lText, adKeywords)

        if (!matchPortrait && !matchLandscape) {
            adConfirmCount = 0
            return@withContext
        }

        val hitRoiName = if (matchPortrait) "portrait" else "landscape"
        val hitLabel = if (matchPortrait) "竖屏" else "横屏"
        val hitText = if (matchPortrait) pText else lText

        val sec = extractCountdownSeconds(hitText, true)
        if (sec != null && sec > 0) {
            state = State.COUNTDOWN
            activeRoi = hitRoiName
            lastCountdownSec = sec
            countdownMissingFrames = 0
            countdownDeadlineMs = now + sec * 1000L + 1200L
            adConfirmCount = 0
            RuntimeState.lastCountdownSec = sec
            RuntimeState.detectorState = "COUNTDOWN(${sec}s)"
            RuntimeState.log("检测到${hitLabel}读秒广告 (${sec}s): [$hitText]，等待读秒结束...")
            return@withContext
        }

        // Ad without countdown (or already finished)
        adConfirmCount++
        RuntimeState.detectorState = "AD_CONFIRM($adConfirmCount/${config.confirmFrames})"
        RuntimeState.notifyChanged()
        if (adConfirmCount >= config.confirmFrames) {
            triggerSkip("${hitLabel}广告[$hitText]")
        }
    }

    private suspend fun triggerSkip(reason: String) {
        val reply = esp32.nextVideo()
        val ok = reply?.ok == true
        if (ok) {
            RuntimeState.totalSkippedAds++
            RuntimeState.log("自动跳过成功 ($reason) -> 已发送下一个视频")
        } else {
            RuntimeState.log("自动跳过尝试 ($reason) -> ESP32未响应，进入冷却")
        }
        state = State.COOLDOWN
        adConfirmCount = 0
        lastCountdownSec = 0
        countdownMissingFrames = 0
        cooldownUntilMs = System.currentTimeMillis() + config.cooldownMs
        RuntimeState.lastCountdownSec = null
        RuntimeState.detectorState = "COOLDOWN"
        RuntimeState.notifyChanged()
    }

    private fun ocrRoi(fullFrame: Bitmap, roi: RoiRect): String {
        val w = fullFrame.width
        val h = fullFrame.height
        val x = (roi.left * w).toInt().coerceIn(0, w - 2)
        val y = (roi.top * h).toInt().coerceIn(0, h - 2)
        val cw = ((roi.right - roi.left) * w).toInt().coerceIn(2, w - x)
        val ch = ((roi.bottom - roi.top) * h).toInt().coerceIn(2, h - y)

        var crop: Bitmap? = null
        return try {
            crop = Bitmap.createBitmap(fullFrame, x, y, cw, ch)
            val image = InputImage.fromBitmap(crop, 0)
            val result = Tasks.await(recognizer.process(image))
            result.text
                .replace("\n", " ")
                .replace(Regex("\\s+"), " ")
                .trim()
        } catch (_: Exception) {
            ""
        } finally {
            crop?.recycle()
        }
    }

    private fun containsAny(text: String, keywords: List<String>): Boolean {
        if (text.isBlank()) return false
        val lower = text.lowercase()
        return keywords.any { kw -> kw.isNotBlank() && lower.contains(kw.lowercase()) }
    }

    internal fun extractCountdownSeconds(text: String, hasAdKeyword: Boolean): Int? {
        if (text.isBlank()) return null

        val m1 = secSuffixPattern.matcher(text)
        if (m1.find()) {
            val v = m1.group(1)?.toIntOrNull()
            if (v != null && v in 0..90) return v
        }

        val m2 = secPrefixPattern.matcher(text)
        if (m2.find()) {
            val v = m2.group(1)?.toIntOrNull()
            if (v != null && v in 0..90) return v
        }

        val m3 = mmssPattern.matcher(text)
        if (m3.find()) {
            val min = m3.group(1)?.toIntOrNull() ?: 0
            val sec = m3.group(2)?.toIntOrNull() ?: 0
            val total = min * 60 + sec
            if (total in 0..90) return total
        }

        if (hasAdKeyword) {
            val m4 = standaloneNumPattern.matcher(text)
            if (m4.find()) {
                val v = m4.group(1)?.toIntOrNull()
                if (v != null && v in 1..60) return v
            }
        }
        return null
    }
}
