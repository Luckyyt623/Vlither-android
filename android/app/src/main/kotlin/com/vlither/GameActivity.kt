package com.vlither

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.Manifest
import android.app.Activity
import android.app.AlarmManager
import android.app.NativeActivity
import android.app.PendingIntent
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.BitmapFactory
import android.graphics.Typeface
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Process
import android.os.SystemClock
import android.provider.MediaStore
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.text.InputType
import android.view.Display
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.LinearInterpolator
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.HashMap
import java.util.Date
import java.util.Locale
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

class GameActivity : NativeActivity() {

    private data class VoicePacket(
        val speakerId: String,
        val pcm: ByteArray,
        val gain: Float
    )

    private data class VoiceTrackHolder(
        val track: AudioTrack,
        @Volatile var lastUsedMs: Long
    )

    private data class VoiceCaptureConfig(
        val source: Int,
        val sampleRate: Int
    )

    private data class EncodedVoiceFrame(
        val pcm: ByteArray,
        val peak: Int
    )

    companion object {
        private const val TAG = "VlitherGame"

        private const val VOICE_PERMISSION_REQUEST = 4401
        private const val EVENT_NOTIFICATION_PERMISSION_REQUEST = 4402
        private const val CUSTOM_ARROW_REQUEST = 4403
        private const val SETTINGS_BACKUP_CREATE_REQUEST = 4404
        private const val SETTINGS_BACKUP_OPEN_REQUEST = 4405
        private const val SETTINGS_FILE_NAME = "user.dat"
        private const val CUSTOM_ARROW_FILE_NAME = "custom_arrow_image"
        private const val BACKUP_MANIFEST_ENTRY = "manifest.txt"
        private const val BACKUP_SETTINGS_ENTRY = "user.dat"
        private const val BACKUP_ARROW_ENTRY = "custom_arrow_image"
        private const val BACKUP_MANIFEST = "VLITHER_BACKUP_V1\n"
        private const val MIN_BACKUP_BYTES = 64
        private const val MAX_SETTINGS_BACKUP_BYTES = 1024 * 1024
        private const val MAX_ARROW_BACKUP_BYTES = 12 * 1024 * 1024
        private const val EVENT_ALARM_ACTION = "com.vlither.EVENT_START"
        private const val VOICE_SAMPLE_RATE = 16_000
        private const val VOICE_FRAME_BYTES = 640 // 20 ms, PCM16 mono @ 16 kHz
        private const val VOICE_MAX_QUEUE = 24
        private const val VOICE_MAX_SPEAKERS = 16
        private val VOICE_CAPTURE_CONFIGS = arrayOf(
            // Prefer a native-rate microphone path. Several Android/OEM audio
            // HALs advertise 16 kHz but fail on the first blocking read.
            VoiceCaptureConfig(MediaRecorder.AudioSource.MIC, 48_000),
            VoiceCaptureConfig(MediaRecorder.AudioSource.MIC, 16_000),
            VoiceCaptureConfig(MediaRecorder.AudioSource.MIC, 44_100),
            VoiceCaptureConfig(MediaRecorder.AudioSource.VOICE_RECOGNITION, 16_000),
            VoiceCaptureConfig(MediaRecorder.AudioSource.VOICE_RECOGNITION, 48_000),
            VoiceCaptureConfig(MediaRecorder.AudioSource.VOICE_COMMUNICATION, 16_000),
            VoiceCaptureConfig(MediaRecorder.AudioSource.VOICE_COMMUNICATION, 48_000)
        )
        private val voiceCaptureWanted = AtomicBoolean(false)
        private val voiceCaptureRunning = AtomicBoolean(false)
        private val voiceCaptureGeneration = AtomicLong(0)
        private val voiceCaptureFault = AtomicBoolean(false)
        private val voiceCaptureError = AtomicInteger(0)
        private val voiceCaptureSampleRate = AtomicInteger(0)
        private val voiceCaptureQueue = ArrayBlockingQueue<ByteArray>(VOICE_MAX_QUEUE)
        @Volatile private var voiceCaptureThread: Thread? = null
        @Volatile private var voiceRecord: AudioRecord? = null
        private val voicePlaybackQueue = LinkedBlockingQueue<VoicePacket>(VOICE_MAX_QUEUE)
        private val voicePlaybackRunning = AtomicBoolean(false)
        private val voicePlaybackGeneration = AtomicLong(0)
        private val voicePlaybackFault = AtomicBoolean(false)
        private val voiceNextPlaybackAttemptMs = AtomicLong(0)
        private val voiceLastPlaybackAtMs = AtomicLong(0)
        @Volatile private var voicePlaybackThread: Thread? = null
        private val voiceCaptureFrames = AtomicLong(0)
        private val voicePlaybackFrames = AtomicLong(0)
        private val voiceCapturePeak = AtomicInteger(0)
        private val voicePermissionRequestPending = AtomicBoolean(false)
        private val voicePermissionDenied = AtomicBoolean(false)
        private val eventNotificationPermissionRequestPending = AtomicBoolean(false)

        private data class PendingEventNotification(
            val id: String,
            val name: String,
            val serverIp: String,
            val startAtMs: Long
        )

        private val pendingEventNotifications =
            ConcurrentHashMap<String, PendingEventNotification>()

        /* Weak ref to the overlay so the static JNI callback can reach it */
        private var overlayRef: FrameLayout? = null
        private var scanAnimator: ObjectAnimator? = null

        private const val IME_EVENT_TEXT: Byte = 1
        private const val IME_EVENT_KEY: Byte = 2
        private const val IME_EVENT_COMPOSITION: Byte = 3
        private const val MAX_IME_EVENTS = 512
        private const val MAX_IME_TEXT_BYTES = 65536
        private val imeEvents = ConcurrentLinkedQueue<ByteArray>()
        private val imeEventCount = AtomicInteger(0)

        private fun enqueueImePacket(packet: ByteArray) {
            synchronized(imeEvents) {
                while (imeEventCount.get() >= MAX_IME_EVENTS) {
                    if (imeEvents.poll() == null) break
                    imeEventCount.decrementAndGet()
                }
                imeEvents.offer(packet)
                imeEventCount.incrementAndGet()
            }
        }

        private fun safeUtf8Length(utf8: ByteArray): Int {
            if (utf8.size <= MAX_IME_TEXT_BYTES) return utf8.size
            var length = MAX_IME_TEXT_BYTES
            while (length > 0 && (utf8[length].toInt() and 0xC0) == 0x80) {
                length--
            }
            return length
        }

        private fun enqueueImeText(text: String) {
            if (text.isEmpty()) return
            val utf8 = text.toByteArray(Charsets.UTF_8)
            val length = safeUtf8Length(utf8)
            val packet = ByteArray(length + 1)
            packet[0] = IME_EVENT_TEXT
            utf8.copyInto(packet, destinationOffset = 1, endIndex = length)
            enqueueImePacket(packet)
        }

        private fun putIntLe(packet: ByteArray, offset: Int, value: Int) {
            packet[offset] = value.toByte()
            packet[offset + 1] = (value ushr 8).toByte()
            packet[offset + 2] = (value ushr 16).toByte()
            packet[offset + 3] = (value ushr 24).toByte()
        }

        private fun bitmapToRgbaPacket(bitmap: android.graphics.Bitmap): ByteArray? {
            val width = bitmap.width
            val height = bitmap.height
            if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
                bitmap.recycle()
                return null
            }
            val count = width * height
            val argb = IntArray(count)
            bitmap.getPixels(argb, 0, width, 0, 0, width, height)
            bitmap.recycle()

            val packet = ByteArray(8 + count * 4)
            putIntLe(packet, 0, width)
            putIntLe(packet, 4, height)
            var out = 8
            for (pixel in argb) {
                packet[out++] = (pixel ushr 16).toByte()
                packet[out++] = (pixel ushr 8).toByte()
                packet[out++] = pixel.toByte()
                packet[out++] = (pixel ushr 24).toByte()
            }
            return packet
        }

        /** Decode packaged image assets for native Vulkan textures. Android's
         * BitmapFactory supplies WebP support that stb_image does not have. */
        @JvmStatic
        fun decodeAssetRgba(activity: Activity, pathUtf8: ByteArray): ByteArray? {
            val path = pathUtf8.toString(Charsets.UTF_8).removePrefix("app/res/")
            return try {
                activity.assets.open(path).use { input ->
                    val bitmap = BitmapFactory.decodeStream(input) ?: return null
                    bitmapToRgbaPacket(bitmap)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Could not decode asset $path", e)
                null
            }
        }

        /** Decode downloaded tag images (including WebP) for native Vulkan. */
        @JvmStatic
        fun decodeImageRgba(activity: Activity, encoded: ByteArray): ByteArray? {
            return try {
                val bitmap = BitmapFactory.decodeByteArray(encoded, 0, encoded.size)
                    ?: return null
                bitmapToRgbaPacket(bitmap)
            } catch (e: Exception) {
                Log.e(TAG, "Could not decode downloaded image", e)
                null
            }
        }

        @JvmStatic
        fun requestCustomArrow(activity: Activity): Boolean {
            return try {
                activity.runOnUiThread {
                    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE)
                        type = "image/*"
                    }
                    activity.startActivityForResult(intent, CUSTOM_ARROW_REQUEST)
                }
                true
            } catch (e: Exception) {
                Log.e(TAG, "Could not open custom-arrow picker", e)
                false
            }
        }

        @JvmStatic
        fun requestSettingsBackup(activity: Activity): Boolean {
            return try {
                activity.runOnUiThread {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
                        activity is GameActivity) {
                        /* Modern Android document pickers can return an empty
                           placeholder to NativeActivity without delivering a
                           usable result URI. Save through MediaStore instead. */
                        activity.exportSettingsBackupToDownloads()
                    } else {
                        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
                            .format(Date())
                        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "application/octet-stream"
                            putExtra(Intent.EXTRA_TITLE,
                                "Vlither-backup-$stamp.vlitherbackup")
                        }
                        activity.startActivityForResult(
                            intent, SETTINGS_BACKUP_CREATE_REQUEST)
                    }
                }
                true
            } catch (e: Exception) {
                Log.e(TAG, "Could not open settings-backup destination", e)
                false
            }
        }

        @JvmStatic
        fun requestSettingsRestore(activity: Activity): Boolean {
            return try {
                activity.runOnUiThread {
                    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE)
                        type = "application/octet-stream"
                    }
                    activity.startActivityForResult(
                        intent, SETTINGS_BACKUP_OPEN_REQUEST)
                }
                true
            } catch (e: Exception) {
                Log.e(TAG, "Could not open settings-backup picker", e)
                false
            }
        }

        private fun enqueueImeKey(keyCode: Int, action: Int, metaState: Int) {
            val packet = ByteArray(13)
            packet[0] = IME_EVENT_KEY
            putIntLe(packet, 1, keyCode)
            putIntLe(packet, 5, action)
            putIntLe(packet, 9, metaState)
            enqueueImePacket(packet)
        }

        private fun enqueueImeComposition(replaceCodePoints: Int, text: String) {
            val utf8 = text.toByteArray(Charsets.UTF_8)
            val length = safeUtf8Length(utf8)
            val packet = ByteArray(length + 5)
            packet[0] = IME_EVENT_COMPOSITION
            putIntLe(packet, 1, replaceCodePoints.coerceIn(0, 4096))
            utf8.copyInto(packet, destinationOffset = 5, endIndex = length)
            enqueueImePacket(packet)
        }

        /**
         * Native code polls this queue from the render thread. Android's IME
         * thread never calls into the native library directly, preventing a
         * missing/mismatched JNI callback from terminating the process.
         */
        @JvmStatic
        fun pollImeEvent(activity: Activity): ByteArray? {
            if (activity !is GameActivity) return null
            synchronized(imeEvents) {
                val packet = imeEvents.poll() ?: return null
                imeEventCount.decrementAndGet()
                return packet
            }
        }

        @JvmStatic
        fun clearImeEvents(activity: Activity) {
            if (activity !is GameActivity) return
            synchronized(imeEvents) {
                imeEvents.clear()
                imeEventCount.set(0)
            }
        }

        /** Queue clipboard text through the same safe IME channel. */
        @JvmStatic
        fun enqueueClipboardPaste(activity: Activity): Boolean {
            if (activity !is GameActivity) return false
            activity.runOnUiThread {
                val text = getClipboardText(activity)
                if (text.isNotEmpty()) enqueueImeText(text)
            }
            return true
        }

        @JvmStatic
        fun getUnlockRemainingMs(activity: Activity): Long {
            return try {
                MainActivity.getUnlockRemainingMsStatic(activity.applicationContext)
            } catch (e: Exception) {
                Log.e(TAG, "getUnlockRemainingMs error: ${e.message}")
                -1L
            }
        }

        /** Read Android's primary clipboard for the native ImGui backend. */
        @JvmStatic
        fun getClipboardText(activity: Activity): String {
            return try {
                val clipboard = activity.getSystemService(Context.CLIPBOARD_SERVICE)
                    as? ClipboardManager ?: return ""
                val clip = clipboard.primaryClip ?: return ""
                if (clip.itemCount <= 0) return ""
                clip.getItemAt(0).coerceToText(activity)?.toString() ?: ""
            } catch (e: Exception) {
                Log.e(TAG, "getClipboardText error: ${e.message}")
                ""
            }
        }

        /** Write text selected in ImGui to Android's primary clipboard. */
        @JvmStatic
        fun setClipboardText(activity: Activity, text: String) {
            try {
                val clipboard = activity.getSystemService(Context.CLIPBOARD_SERVICE)
                    as? ClipboardManager ?: return
                clipboard.setPrimaryClip(ClipData.newPlainText("Vlither text", text))
            } catch (e: Exception) {
                Log.e(TAG, "setClipboardText error: ${e.message}")
            }
        }

        /** UTF-8 JNI variants avoid modified-UTF-8 corruption for emoji. */
        @JvmStatic
        fun getClipboardUtf8(activity: Activity): ByteArray =
            getClipboardText(activity).toByteArray(Charsets.UTF_8)

        @JvmStatic
        fun setClipboardUtf8(activity: Activity, utf8: ByteArray) {
            setClipboardText(activity, utf8.toString(Charsets.UTF_8))
        }

        /** Enable or disable the real Android IME bridge used by ImGui. */
        @JvmStatic
        fun setTextInputActive(activity: Activity, active: Boolean) {
            (activity as? GameActivity)?.setTextInputActiveOnUi(active)
        }

        @JvmStatic
        fun requestAdFromC(activity: Activity) {
            try {
                val intent = android.content.Intent(activity, MainActivity::class.java)
                intent.flags = android.content.Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                activity.startActivity(intent)
            } catch (e: Exception) {
                Log.e(TAG, "requestAdFromC error: ${e.message}")
            }
        }

        /** Native game state owns the policy; Android owns permission,
         *  capture and local MediaStore output. This callback may originate
         *  on the native render thread, so all Activity work is posted to the
         *  UI thread. */
        @JvmStatic
        fun syncGameplayCapture(
            activity: Activity,
            recordingEnabled: Boolean,
            screenshotsEnabled: Boolean,
            sessionActive: Boolean
        ) {
            val game = activity as? GameActivity ?: return
            game.runOnUiThread {
                GameplayCaptureController.sync(
                    game, recordingEnabled, screenshotsEnabled, sessionActive
                )
            }
        }

        /** Called only for the local player's confirmed server `k` packet. */
        @JvmStatic
        fun onConfirmedKill(activity: Activity) {
            val game = activity as? GameActivity ?: return
            game.runOnUiThread {
                GameplayCaptureController.onConfirmedKill(game)
            }
        }

        private fun eventRequestCode(eventId: String): Int =
            eventId.hashCode() and 0x7fffffff

        private fun eventAlarmIntent(
            activity: Activity,
            event: PendingEventNotification
        ): PendingIntent {
            val intent = Intent(activity, EventNotificationReceiver::class.java).apply {
                action = EVENT_ALARM_ACTION
                putExtra(EventNotificationReceiver.EXTRA_EVENT_ID, event.id)
                putExtra(EventNotificationReceiver.EXTRA_EVENT_NAME, event.name)
                putExtra(EventNotificationReceiver.EXTRA_SERVER_IP, event.serverIp)
            }
            return PendingIntent.getBroadcast(
                activity,
                eventRequestCode(event.id),
                intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
        }

        private fun scheduleEventAlarm(
            activity: Activity,
            event: PendingEventNotification
        ): Boolean {
            if (event.startAtMs <= System.currentTimeMillis()) return false
            return try {
                val manager = activity.getSystemService(Context.ALARM_SERVICE)
                    as? AlarmManager ?: return false
                val pendingIntent = eventAlarmIntent(activity, event)
                when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                        manager.canScheduleExactAlarms() ->
                        manager.setExactAndAllowWhileIdle(
                            AlarmManager.RTC_WAKEUP, event.startAtMs, pendingIntent
                        )
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ->
                        manager.setAndAllowWhileIdle(
                            AlarmManager.RTC_WAKEUP, event.startAtMs, pendingIntent
                        )
                    else -> manager.setExact(
                        AlarmManager.RTC_WAKEUP, event.startAtMs, pendingIntent
                    )
                }
                Log.i(TAG, "Event reminder scheduled: ${event.id} at ${event.startAtMs}")
                true
            } catch (e: Exception) {
                Log.w(TAG, "Could not schedule event reminder: ${e.message}")
                false
            }
        }

        @JvmStatic
        fun scheduleEventNotification(
            activity: Activity,
            eventIdUtf8: ByteArray,
            eventNameUtf8: ByteArray,
            serverIpUtf8: ByteArray,
            startAtMs: Long
        ): Int {
            val game = activity as? GameActivity ?: return 0
            val event = PendingEventNotification(
                eventIdUtf8.toString(Charsets.UTF_8),
                eventNameUtf8.toString(Charsets.UTF_8),
                serverIpUtf8.toString(Charsets.UTF_8),
                startAtMs
            )
            if (event.id.isBlank() || event.startAtMs <= System.currentTimeMillis()) return 0
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                game.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
                PackageManager.PERMISSION_GRANTED) {
                pendingEventNotifications[event.id] = event
                if (eventNotificationPermissionRequestPending.compareAndSet(false, true)) {
                    game.runOnUiThread {
                        try {
                            game.requestPermissions(
                                arrayOf(Manifest.permission.POST_NOTIFICATIONS),
                                EVENT_NOTIFICATION_PERMISSION_REQUEST
                            )
                        } catch (e: Exception) {
                            eventNotificationPermissionRequestPending.set(false)
                            Log.w(TAG, "Could not request notification permission: ${e.message}")
                        }
                    }
                }
                return 0
            }
            pendingEventNotifications.remove(event.id)
            return if (scheduleEventAlarm(game, event)) 1 else 0
        }

        @JvmStatic
        fun cancelEventNotification(activity: Activity, eventIdUtf8: ByteArray) {
            val eventId = eventIdUtf8.toString(Charsets.UTF_8)
            if (eventId.isBlank()) return
            pendingEventNotifications.remove(eventId)
            try {
                val placeholder = PendingEventNotification(eventId, "", "", Long.MAX_VALUE)
                val manager = activity.getSystemService(Context.ALARM_SERVICE) as? AlarmManager
                manager?.cancel(eventAlarmIntent(activity, placeholder))
                val notifications = activity.getSystemService(Context.NOTIFICATION_SERVICE)
                    as? android.app.NotificationManager
                notifications?.cancel(eventRequestCode(eventId))
            } catch (e: Exception) {
                Log.w(TAG, "Could not cancel event reminder: ${e.message}")
            }
        }

        /** Prepare microphone permission before open-mic voice starts.
         *  The explicit Enable Voice action owns the Android permission prompt. */
        @JvmStatic
        fun prepareVoice(activity: Activity) {
            val game = activity as? GameActivity ?: return
            if (game.checkSelfPermission(Manifest.permission.RECORD_AUDIO) ==
                PackageManager.PERMISSION_GRANTED) {
                voicePermissionRequestPending.set(false)
                voicePermissionDenied.set(false)
                return
            }
            if (voicePermissionDenied.get()) return
            if (!voicePermissionRequestPending.compareAndSet(false, true)) return
            game.runOnUiThread {
                try {
                    game.requestPermissions(
                        arrayOf(Manifest.permission.RECORD_AUDIO),
                        VOICE_PERMISSION_REQUEST
                    )
                } catch (e: Exception) {
                    voicePermissionRequestPending.set(false)
                    Log.w(TAG, "Could not request microphone permission: ${e.message}")
                }
            }
        }

        /** Bitmask queried by native UI for live voice diagnostics.
         *  1=permission, 2=capture wanted, 4=capturing, 8=playback active,
         *  16=recent microphone signal above the noise floor,
         *  32=the last capture attempt failed and is being retried,
         *  64=the last speaker-output attempt failed and is being retried. */
        @JvmStatic
        fun getVoiceAudioState(activity: Activity): Int {
            val game = activity as? GameActivity ?: return 0
            var state = 0
            if (game.checkSelfPermission(Manifest.permission.RECORD_AUDIO) ==
                PackageManager.PERMISSION_GRANTED) state = state or 1
            if (voiceCaptureWanted.get()) state = state or 2
            if (voiceCaptureRunning.get()) state = state or 4
            val lastPlayback = voiceLastPlaybackAtMs.get()
            if (lastPlayback > 0 && SystemClock.elapsedRealtime() - lastPlayback < 1_000)
                state = state or 8
            if (voiceCapturePeak.get() >= 180) state = state or 16
            if (voiceCaptureFault.get()) state = state or 32
            if (voicePlaybackFault.get()) state = state or 64
            state = state or ((voiceCaptureError.get() and 0xff) shl 8)
            state = state or (((voiceCaptureSampleRate.get() / 1000) and 0xff) shl 16)
            return state
        }

        /** Native networking polls complete 20 ms PCM16/16 kHz frames here.
         *  Keeping this as a normal static JNI call avoids relying on a lazily
         *  resolved Kotlin external callback from the real-time recorder thread. */
        @JvmStatic
        fun pollVoiceCapture(activity: Activity): ByteArray? {
            if (activity !is GameActivity) return null
            return voiceCaptureQueue.poll()
        }

        @JvmStatic
        fun clearVoiceCapture(activity: Activity) {
            if (activity !is GameActivity) return
            voiceCaptureQueue.clear()
        }

        /** Enable or disable microphone transmission for Vlither Voice. */
        @JvmStatic
        fun setVoiceCapture(activity: Activity, active: Boolean) {
            val game = activity as? GameActivity ?: return
            voiceCaptureWanted.set(active)
            if (!active) {
                stopVoiceCapture()
                return
            }
            if (game.checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                PackageManager.PERMISSION_GRANTED) {
                prepareVoice(game)
                return
            }
            startVoiceCapture()
        }

        @Synchronized
        private fun startVoiceCapture() {
            if (!voiceCaptureWanted.get() || voiceCaptureThread?.isAlive == true) return
            val generation = voiceCaptureGeneration.incrementAndGet()
            voiceCaptureQueue.clear()
            voiceCapturePeak.set(0)
            voiceCaptureError.set(0)
            voiceCaptureFault.set(false)
            val captureThread = Thread({
                runVoiceCapture(generation)
            }, "VlitherVoiceCapture")
            voiceCaptureThread = captureThread
            try {
                captureThread.priority = Thread.MAX_PRIORITY
                captureThread.start()
            } catch (e: Exception) {
                if (voiceCaptureThread === captureThread) voiceCaptureThread = null
                markVoiceCaptureFailure(11, generation)
                Log.w(TAG, "Could not start voice worker: ${e.message}")
            }
        }

        private fun runVoiceCapture(generation: Long) {
            try {
                Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
            } catch (_: Exception) {}

            var configIndex = 0
            try {
                while (voiceCaptureWanted.get() &&
                    voiceCaptureGeneration.get() == generation) {
                    val config = VOICE_CAPTURE_CONFIGS[
                        configIndex % VOICE_CAPTURE_CONFIGS.size
                    ]
                    configIndex += 1
                    val recorder = openVoiceRecorder(config, generation)
                    if (recorder == null) {
                        waitForVoiceRetry(generation)
                        continue
                    }
                    if (!voiceCaptureWanted.get() ||
                        voiceCaptureGeneration.get() != generation) {
                        try { recorder.stop() } catch (_: Exception) {}
                        recorder.release()
                        break
                    }

                    voiceRecord = recorder
                    voiceCaptureRunning.set(true)
                    voiceCaptureSampleRate.set(config.sampleRate)
                    val input = ShortArray(maxOf(1, config.sampleRate / 50))
                    var filled = 0
                    try {
                        while (voiceCaptureWanted.get() &&
                            voiceCaptureGeneration.get() == generation) {
                            val read = recorder.read(
                                input,
                                filled,
                                input.size - filled,
                                AudioRecord.READ_BLOCKING
                            )
                            if (!voiceCaptureWanted.get() ||
                                voiceCaptureGeneration.get() != generation) break
                            if (read > 0) {
                                filled += read
                                if (filled < input.size) continue
                                val encoded = encodeVoiceFrame(input, config.sampleRate)
                                voiceCapturePeak.set(encoded.peak)
                                voiceCaptureFrames.incrementAndGet()
                                voiceCaptureError.set(0)
                                voiceCaptureFault.set(false)
                                if (!voiceCaptureQueue.offer(encoded.pcm)) {
                                    voiceCaptureQueue.poll()
                                    voiceCaptureQueue.offer(encoded.pcm)
                                }
                                filled = 0
                            } else if (read == 0) {
                                try { Thread.sleep(2) } catch (_: InterruptedException) {}
                            } else {
                                val error = when (read) {
                                    AudioRecord.ERROR_DEAD_OBJECT -> 5
                                    AudioRecord.ERROR_INVALID_OPERATION -> 6
                                    AudioRecord.ERROR_BAD_VALUE -> 7
                                    else -> 8
                                }
                                markVoiceCaptureFailure(error, generation)
                                Log.w(
                                    TAG,
                                    "Voice read failed source=${config.source} " +
                                        "rate=${config.sampleRate} code=$read"
                                )
                                break
                            }
                        }
                    } catch (e: Exception) {
                        markVoiceCaptureFailure(10, generation)
                        Log.w(
                            TAG,
                            "Voice capture exception source=${config.source} " +
                                "rate=${config.sampleRate}: ${e.message}"
                        )
                    } finally {
                        try { recorder.stop() } catch (_: Exception) {}
                        recorder.release()
                        if (voiceCaptureGeneration.get() == generation) {
                            if (voiceRecord === recorder) voiceRecord = null
                            voiceCaptureRunning.set(false)
                            voiceCapturePeak.set(0)
                        }
                    }
                    waitForVoiceRetry(generation)
                }
            } finally {
                if (voiceCaptureGeneration.get() == generation) {
                    if (voiceCaptureThread === Thread.currentThread()) {
                        voiceCaptureThread = null
                    }
                    voiceRecord = null
                    voiceCaptureRunning.set(false)
                    voiceCaptureSampleRate.set(0)
                    voiceCapturePeak.set(0)
                }
            }
        }

        private fun openVoiceRecorder(
            config: VoiceCaptureConfig,
            generation: Long
        ): AudioRecord? {
            voiceCaptureSampleRate.set(config.sampleRate)
            val minBuffer = AudioRecord.getMinBufferSize(
                config.sampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT
            )
            if (minBuffer <= 0) {
                markVoiceCaptureFailure(2, generation)
                return null
            }
            var recorder: AudioRecord? = null
            try {
                val inputFrameBytes = maxOf(2, config.sampleRate / 50 * 2)
                recorder = AudioRecord.Builder()
                    .setAudioSource(config.source)
                    .setAudioFormat(
                        AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(config.sampleRate)
                            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                            .build()
                    )
                    .setBufferSizeInBytes(maxOf(minBuffer, inputFrameBytes * 8))
                    .build()
                if (recorder.state != AudioRecord.STATE_INITIALIZED) {
                    markVoiceCaptureFailure(3, generation)
                    recorder.release()
                    return null
                }
                recorder.startRecording()
                if (recorder.recordingState != AudioRecord.RECORDSTATE_RECORDING) {
                    markVoiceCaptureFailure(4, generation)
                    try { recorder.stop() } catch (_: Exception) {}
                    recorder.release()
                    return null
                }
                Log.i(
                    TAG,
                    "Vlither Voice input source=${config.source} rate=${config.sampleRate}"
                )
                return recorder
            } catch (e: SecurityException) {
                markVoiceCaptureFailure(9, generation)
                Log.w(TAG, "Microphone permission unavailable: ${e.message}")
            } catch (e: Exception) {
                markVoiceCaptureFailure(3, generation)
                Log.w(
                    TAG,
                    "Voice input unavailable source=${config.source} " +
                        "rate=${config.sampleRate}: ${e.message}"
                )
            }
            try { recorder?.stop() } catch (_: Exception) {}
            try { recorder?.release() } catch (_: Exception) {}
            return null
        }

        private fun encodeVoiceFrame(input: ShortArray, sampleRate: Int): EncodedVoiceFrame {
            val outputSamples = VOICE_FRAME_BYTES / 2
            val pcm = ByteArray(VOICE_FRAME_BYTES)
            var peak = 0
            for (i in 0 until outputSamples) {
                val positionNumerator = i.toLong() * sampleRate.toLong()
                val index = (positionNumerator / VOICE_SAMPLE_RATE).toInt()
                    .coerceIn(0, input.lastIndex)
                val remainder = (positionNumerator % VOICE_SAMPLE_RATE).toInt()
                val nextIndex = minOf(index + 1, input.lastIndex)
                val sample = if (remainder == 0 || nextIndex == index) {
                    input[index].toInt()
                } else {
                    val a = input[index].toLong()
                    val b = input[nextIndex].toLong()
                    ((a * (VOICE_SAMPLE_RATE - remainder) + b * remainder) /
                        VOICE_SAMPLE_RATE).toInt()
                }.coerceIn(-32768, 32767)
                val absSample = if (sample == -32768) 32768 else kotlin.math.abs(sample)
                if (absSample > peak) peak = absSample
                pcm[i * 2] = (sample and 0xff).toByte()
                pcm[i * 2 + 1] = ((sample ushr 8) and 0xff).toByte()
            }
            return EncodedVoiceFrame(pcm, peak)
        }

        private fun waitForVoiceRetry(generation: Long) {
            if (!voiceCaptureWanted.get() ||
                voiceCaptureGeneration.get() != generation) return
            try { Thread.sleep(180) } catch (_: InterruptedException) {}
        }

        private fun markVoiceCaptureFailure(error: Int, generation: Long) {
            if (!voiceCaptureWanted.get() ||
                voiceCaptureGeneration.get() != generation) return
            voiceCaptureRunning.set(false)
            voiceCaptureFault.set(true)
            voiceCaptureError.set(error)
        }

        @Synchronized
        private fun stopVoiceCapture() {
            voiceCaptureWanted.set(false)
            voiceCaptureGeneration.incrementAndGet()
            voiceCaptureRunning.set(false)
            voiceCapturePeak.set(0)
            voiceCaptureFault.set(false)
            voiceCaptureError.set(0)
            voiceCaptureSampleRate.set(0)
            voiceCaptureQueue.clear()
            voicePermissionDenied.set(false)
            val recorder = voiceRecord
            val thread = voiceCaptureThread
            voiceRecord = null
            voiceCaptureThread = null
            try { recorder?.stop() } catch (_: Exception) {}
            thread?.interrupt()
        }

        /** Queue one remote PCM packet without blocking the native render loop. */
        @JvmStatic
        fun playVoicePcm(activity: Activity, speakerId: String, pcm: ByteArray, gain: Float) {
            if (activity !is GameActivity || speakerId.isBlank() || pcm.size < 2) return
            ensureVoicePlaybackThread()
            val packet = VoicePacket(
                /* JNI already created a private ByteArray for this packet.
                 * A second copy here doubled voice allocation churn and made
                 * Android GC more likely to interrupt a gameplay frame. */
                speakerId.take(64), pcm, gain.coerceIn(0f, 1f)
            )
            if (!voicePlaybackQueue.offer(packet)) {
                voicePlaybackQueue.poll()
                voicePlaybackQueue.offer(packet)
            }
        }

        @Synchronized
        private fun ensureVoicePlaybackThread() {
            if (voicePlaybackRunning.get() && voicePlaybackThread?.isAlive == true) return
            val generation = voicePlaybackGeneration.incrementAndGet()
            voicePlaybackRunning.set(true)
            voicePlaybackThread = Thread({
                val tracks = HashMap<String, VoiceTrackHolder>()
                try {
                    while (voicePlaybackRunning.get() &&
                        voicePlaybackGeneration.get() == generation) {
                        val packet = try {
                            voicePlaybackQueue.poll(1, TimeUnit.SECONDS)
                        } catch (_: InterruptedException) {
                            if (voicePlaybackGeneration.get() != generation) break
                            null
                        }
                        if (packet == null) {
                            cleanupIdleVoiceTracks(tracks)
                            continue
                        }
                        var holder = tracks[packet.speakerId]
                        if (holder == null) {
                            if (tracks.size >= VOICE_MAX_SPEAKERS) continue
                            val track = createVoiceTrack() ?: continue
                            val created = VoiceTrackHolder(track, System.currentTimeMillis())
                            tracks[packet.speakerId] = created
                            holder = created
                        }
                        val activeHolder = holder
                        val scaled = scalePcm16(packet.pcm, packet.gain)
                        try {
                            val written = activeHolder.track.write(
                                scaled, 0, scaled.size, AudioTrack.WRITE_BLOCKING
                            )
                            if (written <= 0) {
                                markVoicePlaybackFailure()
                                tracks.remove(packet.speakerId)?.let {
                                    releaseVoiceTrack(it.track)
                                }
                                continue
                            }
                            voicePlaybackFault.set(false)
                            voiceNextPlaybackAttemptMs.set(0)
                            voiceLastPlaybackAtMs.set(SystemClock.elapsedRealtime())
                            voicePlaybackFrames.incrementAndGet()
                            activeHolder.lastUsedMs = System.currentTimeMillis()
                        } catch (_: Exception) {
                            markVoicePlaybackFailure()
                            tracks.remove(packet.speakerId)?.let { releaseVoiceTrack(it.track) }
                        }
                    }
                } finally {
                    for (holder in tracks.values) releaseVoiceTrack(holder.track)
                    tracks.clear()
                    if (voicePlaybackGeneration.get() == generation) {
                        voicePlaybackQueue.clear()
                        voicePlaybackRunning.set(false)
                        if (voicePlaybackThread === Thread.currentThread()) {
                            voicePlaybackThread = null
                        }
                    }
                }
            }, "VlitherVoicePlayback").also {
                it.priority = Thread.NORM_PRIORITY + 2
                it.start()
            }
        }

        private fun createVoiceTrack(): AudioTrack? {
            if (SystemClock.elapsedRealtime() < voiceNextPlaybackAttemptMs.get()) return null
            return try {
                val minBuffer = AudioTrack.getMinBufferSize(
                    VOICE_SAMPLE_RATE,
                    AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT
                )
                if (minBuffer <= 0) {
                    markVoicePlaybackFailure()
                    return null
                }
                val track = AudioTrack.Builder()
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                            .build()
                    )
                    .setAudioFormat(
                        AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(VOICE_SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build()
                    )
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(maxOf(minBuffer, VOICE_FRAME_BYTES * 8))
                    .build()
                if (track.state != AudioTrack.STATE_INITIALIZED) {
                    track.release()
                    markVoicePlaybackFailure()
                    return null
                }
                track.play()
                voicePlaybackFault.set(false)
                voiceNextPlaybackAttemptMs.set(0)
                track
            } catch (e: Exception) {
                markVoicePlaybackFailure()
                Log.w(TAG, "Could not create voice output: ${e.message}")
                null
            }
        }

        private fun markVoicePlaybackFailure() {
            voicePlaybackFault.set(true)
            voiceNextPlaybackAttemptMs.set(SystemClock.elapsedRealtime() + 750)
        }

        private fun scalePcm16(input: ByteArray, gain: Float): ByteArray {
            if (gain >= 0.995f) return input
            val out = input.copyOf()
            var i = 0
            while (i + 1 < out.size) {
                val raw = (out[i].toInt() and 0xff) or (out[i + 1].toInt() shl 8)
                val sample = raw.toShort().toInt()
                val scaled = (sample * gain).toInt().coerceIn(-32768, 32767)
                out[i] = (scaled and 0xff).toByte()
                out[i + 1] = ((scaled shr 8) and 0xff).toByte()
                i += 2
            }
            return out
        }

        private fun cleanupIdleVoiceTracks(tracks: MutableMap<String, VoiceTrackHolder>) {
            val now = System.currentTimeMillis()
            val iterator = tracks.entries.iterator()
            while (iterator.hasNext()) {
                val (_, holder) = iterator.next()
                if (now - holder.lastUsedMs > 10_000) {
                    iterator.remove()
                    releaseVoiceTrack(holder.track)
                }
            }
        }

        private fun releaseVoiceTrack(track: AudioTrack) {
            try { track.stop() } catch (_: Exception) {}
            try { track.flush() } catch (_: Exception) {}
            try { track.release() } catch (_: Exception) {}
        }

        @JvmStatic
        fun stopVoicePlayback(activity: Activity) {
            if (activity !is GameActivity) return
            voicePlaybackGeneration.incrementAndGet()
            voicePlaybackRunning.set(false)
            val thread = voicePlaybackThread
            voicePlaybackThread = null
            voicePlaybackQueue.clear()
            voicePlaybackFault.set(false)
            voiceNextPlaybackAttemptMs.set(0)
            voiceLastPlaybackAtMs.set(0)
            thread?.interrupt()
        }

        /**
         * Called from C via JNI (android_jni.c) when the first Vulkan frame
         * has been rendered. Fades out and removes the loading overlay.
         * Signature used in android_jni.c: (Landroid/app/Activity;)V
         */
        @JvmStatic
        fun notifyGameReady(activity: Activity) {
            activity.runOnUiThread {
                val overlay = overlayRef ?: return@runOnUiThread
                scanAnimator?.cancel()
                overlay.animate()
                    .alpha(0f)
                    .setDuration(600)
                    .setStartDelay(120)
                    .setInterpolator(AccelerateDecelerateInterpolator())
                    .setListener(object : AnimatorListenerAdapter() {
                        override fun onAnimationEnd(animation: Animator) {
                            (overlay.parent as? ViewGroup)?.removeView(overlay)
                            overlayRef  = null
                            scanAnimator = null
                        }
                    })
                    .start()
            }
        }

    }


    private var imeBridgeView: ImeBridgeView? = null
    private var textInputActive = false

    /**
     * A one-pixel Android text editor. It is visually hidden, but because it
     * exposes a genuine InputConnection, Gboard and other keyboards can send
     * commitText(), composing text, clipboard-history taps, emoji, deletion,
     * enter and hardware-key events. ImGui remains the visible text field.
     */
    private inner class ImeBridgeView(context: Context) : View(context) {
        private var composingCodePoints = 0

        init {
            isFocusable = true
            isFocusableInTouchMode = true
            isClickable = false
            alpha = 0.01f
            importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
        }

        override fun onCheckIsTextEditor(): Boolean = true

        private fun codePointCount(text: CharSequence): Int =
            Character.codePointCount(text, 0, text.length)

        private fun sendText(text: String) {
            enqueueImeText(text)
        }

        private fun sendKey(keyCode: Int, metaState: Int = 0) {
            enqueueImeKey(keyCode, KeyEvent.ACTION_DOWN, metaState)
            enqueueImeKey(keyCode, KeyEvent.ACTION_UP, metaState)
        }

        private fun sendShortcut(keyCode: Int) {
            enqueueImeKey(KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.ACTION_DOWN, 0)
            enqueueImeKey(keyCode, KeyEvent.ACTION_DOWN, KeyEvent.META_CTRL_ON)
            enqueueImeKey(keyCode, KeyEvent.ACTION_UP, KeyEvent.META_CTRL_ON)
            enqueueImeKey(KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.ACTION_UP, 0)
        }

        private fun forwardKeyEvent(event: KeyEvent): Boolean {
            if (event.action == KeyEvent.ACTION_MULTIPLE) {
                val chars = event.characters.orEmpty()
                if (chars.isNotEmpty()) sendText(chars)
                return true
            }

            if (event.action == KeyEvent.ACTION_DOWN &&
                !event.isCtrlPressed && !event.isAltPressed && event.isPrintingKey) {
                val codePoint = event.unicodeChar
                if (codePoint > 0) {
                    sendText(String(Character.toChars(codePoint)))
                    return true
                }
            }

            enqueueImeKey(event.keyCode, event.action, event.metaState)
            return true
        }

        fun resetComposition() {
            composingCodePoints = 0
        }

        override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or
                EditorInfo.IME_FLAG_NO_FULLSCREEN
            outAttrs.initialSelStart = 0
            outAttrs.initialSelEnd = 0

            return object : BaseInputConnection(this@ImeBridgeView, false) {
                override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                    val committed = text?.toString().orEmpty()
                    enqueueImeComposition(composingCodePoints, committed)
                    composingCodePoints = 0
                    return true
                }

                override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                    val composing = text?.toString().orEmpty()
                    enqueueImeComposition(composingCodePoints, composing)
                    composingCodePoints = codePointCount(composing)
                    return true
                }

                override fun finishComposingText(): Boolean {
                    composingCodePoints = 0
                    return true
                }

                override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                    composingCodePoints = 0
                    repeat(beforeLength.coerceIn(0, 4096)) { sendKey(KeyEvent.KEYCODE_DEL) }
                    repeat(afterLength.coerceIn(0, 4096)) { sendKey(KeyEvent.KEYCODE_FORWARD_DEL) }
                    return true
                }

                override fun deleteSurroundingTextInCodePoints(
                    beforeLength: Int,
                    afterLength: Int
                ): Boolean = deleteSurroundingText(beforeLength, afterLength)

                override fun sendKeyEvent(event: KeyEvent): Boolean =
                    forwardKeyEvent(event)

                override fun performEditorAction(actionCode: Int): Boolean {
                    sendKey(KeyEvent.KEYCODE_ENTER)
                    return true
                }

                override fun performContextMenuAction(id: Int): Boolean {
                    return when (id) {
                        android.R.id.paste, android.R.id.pasteAsPlainText -> {
                            val text = getClipboardText(this@GameActivity)
                            sendText(text)
                            true
                        }
                        android.R.id.selectAll -> {
                            sendShortcut(KeyEvent.KEYCODE_A)
                            true
                        }
                        android.R.id.copy -> {
                            sendShortcut(KeyEvent.KEYCODE_C)
                            true
                        }
                        android.R.id.cut -> {
                            sendShortcut(KeyEvent.KEYCODE_X)
                            true
                        }
                        else -> super.performContextMenuAction(id)
                    }
                }
            }
        }

        override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean =
            forwardKeyEvent(event)

        override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean =
            forwardKeyEvent(event)

        override fun onKeyMultiple(
            keyCode: Int,
            repeatCount: Int,
            event: KeyEvent
        ): Boolean = forwardKeyEvent(event)
    }

    private fun installImeBridge() {
        val decor = window.decorView as? ViewGroup ?: return
        val bridge = ImeBridgeView(this)
        val params = FrameLayout.LayoutParams(1, 1, Gravity.TOP or Gravity.START)
        decor.addView(bridge, params)
        imeBridgeView = bridge
    }

    private fun setTextInputActiveOnUi(active: Boolean) {
        runOnUiThread {
            val bridge = imeBridgeView ?: return@runOnUiThread
            if (textInputActive == active) return@runOnUiThread
            textInputActive = active

            val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
                ?: return@runOnUiThread

            if (active) {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                bridge.requestFocus()
                imm.restartInput(bridge)
                bridge.post {
                    bridge.requestFocus()
                    imm.showSoftInput(bridge, InputMethodManager.SHOW_IMPLICIT)
                }
            } else {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                imm.hideSoftInputFromWindow(bridge.windowToken, 0)
                bridge.clearFocus()
                window.decorView.requestFocus()
            }
        }
    }

    /* ── Loading overlay ───────────────────────────────────────────── */

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics
        ).toInt()

    private fun sp(value: Float): Float =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_SP, value, resources.displayMetrics
        )

    private fun buildLoadingOverlay(): FrameLayout {

        /* ── Root: full-screen dark background ── */
        val root = FrameLayout(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.parseColor("#0D0E14"))
            alpha = 0f   // start invisible; we fade it in below
        }

        /* ── Centre column ── */
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity     = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
            )
        }

        /* Line 1 – "Official Vlither by Ignite" */
        val line1 = TextView(this).apply {
            text    = "Official Vlither by Ignite"
            setTextColor(Color.parseColor("#5DCFCF"))   // muted cyan
            setTextSize(TypedValue.COMPLEX_UNIT_PX, sp(15f))
            typeface = Typeface.create("monospace", Typeface.NORMAL)
            gravity  = Gravity.CENTER
            letterSpacing = 0.12f
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(6f) }
        }

        /* Line 2 – "Mobile Vlither by Lucky" */
        val line2 = TextView(this).apply {
            text    = "Mobile Vlither by Lucky"
            setTextColor(Color.parseColor("#2BFF88"))   // bright neon green
            setTextSize(TypedValue.COMPLEX_UNIT_PX, sp(22f))
            typeface = Typeface.create("monospace", Typeface.BOLD)
            gravity  = Gravity.CENTER
            letterSpacing = 0.10f
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(36f) }
        }

        /* Loading bar track */
        val trackWidth = dp(260f)
        val track = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(trackWidth, dp(4f))
            setBackgroundColor(Color.parseColor("#1C2030"))  // dark track
            clipChildren = true
            clipToPadding = true
        }

        /* Scanning bar inside track */
        val scanBar = View(this).apply {
            layoutParams = FrameLayout.LayoutParams(dp(90f), dp(4f))
            setBackgroundColor(Color.parseColor("#00E5FF"))  // neon cyan
        }
        track.addView(scanBar)

        /* Animate scan bar: slides left → right, loops forever */
        val scanAnim = ObjectAnimator.ofFloat(
            scanBar, "translationX",
            -dp(90f).toFloat(),
            trackWidth.toFloat()
        ).apply {
            duration       = 1100L
            repeatCount    = ValueAnimator.INFINITE
            repeatMode     = ValueAnimator.RESTART
            interpolator   = LinearInterpolator()
        }
        scanAnimator = scanAnim

        col.addView(line1)
        col.addView(line2)
        col.addView(track)
        root.addView(col)

        /* Fade the whole overlay in */
        root.animate()
            .alpha(1f)
            .setDuration(700)
            .setInterpolator(AccelerateDecelerateInterpolator())
            .withEndAction { scanAnim.start() }
            .start()

        return root
    }

    /* ── System UI helpers ─────────────────────────────────────────── */

    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { c ->
                c.hide(android.view.WindowInsets.Type.systemBars())
                c.systemBarsBehavior =
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
        }
    }

    /* ── Lifecycle ─────────────────────────────────────────────────── */

    private fun copyFileIntoBackup(
        zip: ZipOutputStream,
        entryName: String,
        file: File,
        maximumBytes: Int
    ) {
        if (!file.isFile) return
        if (file.length() <= 0L || file.length() > maximumBytes.toLong())
            throw IllegalArgumentException("$entryName has an invalid size")
        zip.putNextEntry(ZipEntry(entryName))
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            var total = 0
            while (true) {
                val read = input.read(buffer)
                if (read <= 0) break
                total += read
                if (total > maximumBytes)
                    throw IllegalArgumentException("$entryName is too large")
                zip.write(buffer, 0, read)
            }
        }
        zip.closeEntry()
    }

    private fun readBackupEntry(zip: ZipInputStream, maximumBytes: Int): ByteArray {
        val output = ByteArrayOutputStream(minOf(maximumBytes, 64 * 1024))
        val buffer = ByteArray(64 * 1024)
        var total = 0
        while (true) {
            val read = zip.read(buffer)
            if (read <= 0) break
            total += read
            if (total > maximumBytes)
                throw IllegalArgumentException("Backup entry is too large")
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    private fun buildSettingsBackup(): ByteArray {
        val settings = filesDir.resolve(SETTINGS_FILE_NAME)
        if (!settings.isFile)
            throw IllegalStateException("Settings file is not ready")
        val bytes = ByteArrayOutputStream()
        ZipOutputStream(BufferedOutputStream(bytes)).use { zip ->
            zip.putNextEntry(ZipEntry(BACKUP_MANIFEST_ENTRY))
            zip.write(BACKUP_MANIFEST.toByteArray(Charsets.UTF_8))
            zip.closeEntry()
            copyFileIntoBackup(zip, BACKUP_SETTINGS_ENTRY, settings,
                MAX_SETTINGS_BACKUP_BYTES)
            copyFileIntoBackup(zip, BACKUP_ARROW_ENTRY,
                filesDir.resolve(CUSTOM_ARROW_FILE_NAME),
                MAX_ARROW_BACKUP_BYTES)
        }
        val backup = bytes.toByteArray()
        if (backup.size < MIN_BACKUP_BYTES)
            throw IllegalStateException("Backup data was not created")
        return backup
    }

    private fun writeSettingsBackup(uri: android.net.Uri, backup: ByteArray) {
        /* rwt requests a new, truncated document. Some file managers only
           commit the document once this stream is closed, so build and check
           all data before opening it and always close it after a full write. */
        val output = try {
            contentResolver.openOutputStream(uri, "rwt")
        } catch (_: Exception) {
            contentResolver.openOutputStream(uri, "wt")
        } ?: throw IllegalStateException("Could not open destination")
        BufferedOutputStream(output).use { stream ->
            stream.write(backup)
            stream.flush()
        }
        val savedSize = try {
            contentResolver.openAssetFileDescriptor(uri, "r")?.use {
                it.length
            } ?: -1L
        } catch (_: Exception) {
            // A few cloud document providers allow writing but cannot report
            // a readable size immediately. The closed write above is valid.
            -1L
        }
        if (savedSize >= 0L && savedSize != backup.size.toLong())
            throw IllegalStateException("Only $savedSize of ${backup.size} bytes were saved")
    }

    private fun exportSettingsBackup(uri: android.net.Uri) {
        Thread({
            try {
                val backup = buildSettingsBackup()
                writeSettingsBackup(uri, backup)
                runOnUiThread {
                    Toast.makeText(this,
                        "Backup saved (${backup.size / 1024 + 1} KB).",
                        Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Could not export Vlither backup", e)
                runOnUiThread {
                    Toast.makeText(this,
                        "Backup failed: ${e.message ?: "invalid destination"}",
                        Toast.LENGTH_LONG).show()
                }
            }
        }, "VlitherBackupExport").start()
    }

    private fun exportSettingsBackupToDownloads() {
        Thread({
            var destination: android.net.Uri? = null
            try {
                val backup = buildSettingsBackup()
                val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
                    .format(Date())
                val fileName = "Vlither-backup-$stamp.vlitherbackup"
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
                    put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                    put(MediaStore.MediaColumns.RELATIVE_PATH,
                        "${Environment.DIRECTORY_DOWNLOADS}/Vlither")
                    put(MediaStore.MediaColumns.IS_PENDING, 1)
                }
                val uri = contentResolver.insert(
                    MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                    ?: throw IllegalStateException("Could not create Downloads file")
                destination = uri
                writeSettingsBackup(uri, backup)
                values.clear()
                values.put(MediaStore.MediaColumns.IS_PENDING, 0)
                if (contentResolver.update(uri, values, null, null) <= 0)
                    throw IllegalStateException("Could not finish Downloads file")
                runOnUiThread {
                    Toast.makeText(this,
                        "Backup saved in Downloads/Vlither\n$fileName (${backup.size} bytes)",
                        Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                destination?.let {
                    try { contentResolver.delete(it, null, null) }
                    catch (_: Exception) { }
                }
                Log.e(TAG, "Could not save Vlither backup to Downloads", e)
                runOnUiThread {
                    Toast.makeText(this,
                        "Backup failed: ${e.message ?: "storage error"}",
                        Toast.LENGTH_LONG).show()
                }
            }
        }, "VlitherBackupDownloads").start()
    }

    private fun validSettingsPayload(bytes: ByteArray): Boolean {
        return bytes.size in 128..MAX_SETTINGS_BACKUP_BYTES &&
            bytes[0] == '2'.code.toByte() &&
            bytes[1] == '.'.code.toByte() &&
            bytes[2] == '0'.code.toByte() &&
            bytes[3] == 0.toByte()
    }

    private fun replaceFromTemp(temp: File, target: File) {
        if (target.exists() && !target.delete())
            throw IllegalStateException("Could not replace ${target.name}")
        if (!temp.renameTo(target)) {
            temp.copyTo(target, overwrite = true)
            if (!temp.delete()) temp.deleteOnExit()
        }
    }

    private fun installRestoredFiles(settingsBytes: ByteArray, arrowBytes: ByteArray?) {
        val settings = filesDir.resolve(SETTINGS_FILE_NAME)
        val arrow = filesDir.resolve(CUSTOM_ARROW_FILE_NAME)
        val settingsTemp = filesDir.resolve("$SETTINGS_FILE_NAME.restore")
        val arrowTemp = filesDir.resolve("$CUSTOM_ARROW_FILE_NAME.restore")
        val settingsBefore = filesDir.resolve("$SETTINGS_FILE_NAME.before_restore")
        val arrowBefore = filesDir.resolve("$CUSTOM_ARROW_FILE_NAME.before_restore")
        val hadSettings = settings.isFile
        val hadArrow = arrow.isFile

        settingsTemp.delete()
        arrowTemp.delete()
        settingsBefore.delete()
        arrowBefore.delete()
        if (hadSettings) settings.copyTo(settingsBefore, overwrite = true)
        if (hadArrow) arrow.copyTo(arrowBefore, overwrite = true)
        try {
            FileOutputStream(settingsTemp).use { output ->
                output.write(settingsBytes)
                output.fd.sync()
            }
            if (arrowBytes != null) {
                FileOutputStream(arrowTemp).use { output ->
                    output.write(arrowBytes)
                    output.fd.sync()
                }
            }
            replaceFromTemp(settingsTemp, settings)
            if (arrowBytes != null) replaceFromTemp(arrowTemp, arrow)
            else if (arrow.exists() && !arrow.delete())
                throw IllegalStateException("Could not replace custom arrow")
            settingsBefore.delete()
            arrowBefore.delete()
        } catch (e: Exception) {
            settingsTemp.delete()
            arrowTemp.delete()
            if (hadSettings && settingsBefore.isFile) {
                settings.delete()
                settingsBefore.copyTo(settings, overwrite = true)
            } else if (!hadSettings) settings.delete()
            if (hadArrow && arrowBefore.isFile) {
                arrow.delete()
                arrowBefore.copyTo(arrow, overwrite = true)
            } else if (!hadArrow) arrow.delete()
            settingsBefore.delete()
            arrowBefore.delete()
            throw e
        }
    }

    private fun restoreSettingsBackup(uri: android.net.Uri) {
        Thread({
            try {
                var manifest: ByteArray? = null
                var settings: ByteArray? = null
                var arrow: ByteArray? = null
                val input = contentResolver.openInputStream(uri)
                    ?: throw IllegalStateException("Could not open backup")
                ZipInputStream(input).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        if (entry.isDirectory)
                            throw IllegalArgumentException("Invalid backup folder")
                        when (entry.name) {
                            BACKUP_MANIFEST_ENTRY -> {
                                if (manifest != null)
                                    throw IllegalArgumentException("Duplicate manifest")
                                manifest = readBackupEntry(zip, 128)
                            }
                            BACKUP_SETTINGS_ENTRY -> {
                                if (settings != null)
                                    throw IllegalArgumentException("Duplicate settings")
                                settings = readBackupEntry(zip,
                                    MAX_SETTINGS_BACKUP_BYTES)
                            }
                            BACKUP_ARROW_ENTRY -> {
                                if (arrow != null)
                                    throw IllegalArgumentException("Duplicate custom arrow")
                                arrow = readBackupEntry(zip,
                                    MAX_ARROW_BACKUP_BYTES)
                            }
                            else -> throw IllegalArgumentException(
                                "Unknown backup entry")
                        }
                        zip.closeEntry()
                    }
                }
                if (manifest?.toString(Charsets.UTF_8) != BACKUP_MANIFEST)
                    throw IllegalArgumentException("Not a Vlither backup")
                val settingsPayload = settings
                    ?: throw IllegalArgumentException("Settings are missing")
                if (!validSettingsPayload(settingsPayload))
                    throw IllegalArgumentException("Settings backup is incompatible")
                if (arrow?.isEmpty() == true)
                    throw IllegalArgumentException("Custom arrow is invalid")
                installRestoredFiles(settingsPayload, arrow)
                runOnUiThread {
                    Toast.makeText(this,
                        "Backup loaded. Reopen Vlither to use restored settings.",
                        Toast.LENGTH_LONG).show()
                    window.decorView.postDelayed({
                        Process.killProcess(Process.myPid())
                    }, 1200L)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Could not restore Vlither backup", e)
                runOnUiThread {
                    Toast.makeText(this,
                        "Load failed: ${e.message ?: "invalid backup"}",
                        Toast.LENGTH_LONG).show()
                }
            }
        }, "VlitherBackupRestore").start()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        @Suppress("DEPRECATION")
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN or
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED or
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
            WindowManager.LayoutParams.FLAG_FULLSCREEN or
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED or
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        )
        enableHighPerformanceDisplay()
        hideSystemBars()
        installImeBridge()
        /* Add the overlay on top of NativeActivity's surface view */
        val overlay = buildLoadingOverlay()
        overlayRef  = overlay
        window.decorView.let {
            if (it is ViewGroup) it.addView(overlay)
        }
        Log.d(TAG, "GameActivity created – loading overlay shown")
    }

    @Deprecated("Activity result compatibility for NativeActivity")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == GameplayCaptureController.CAPTURE_REQUEST_CODE) {
            GameplayCaptureController.handleActivityResult(this, resultCode, data)
            return
        }
        if (requestCode == SETTINGS_BACKUP_CREATE_REQUEST) {
            if (resultCode == Activity.RESULT_OK)
                data?.data?.let { exportSettingsBackup(it) }
            return
        }
        if (requestCode == SETTINGS_BACKUP_OPEN_REQUEST) {
            if (resultCode == Activity.RESULT_OK)
                data?.data?.let { restoreSettingsBackup(it) }
            return
        }
        if (requestCode != CUSTOM_ARROW_REQUEST || resultCode != Activity.RESULT_OK)
            return
        val uri = data?.data ?: return
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(filesDir.resolve("custom_arrow_image")).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0
                    while (true) {
                        val read = input.read(buffer)
                        if (read <= 0) break
                        total += read
                        if (total > 12 * 1024 * 1024)
                            throw IllegalArgumentException("Image exceeds 12 MB")
                        output.write(buffer, 0, read)
                    }
                    output.flush()
                }
            }
        } catch (e: Exception) {
            filesDir.resolve("custom_arrow_image").delete()
            Log.e(TAG, "Could not save custom arrow", e)
        }
    }

    private fun enableHighPerformanceDisplay() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                window.setSustainedPerformanceMode(true)
            }

            val display = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                display
            } else {
                @Suppress("DEPRECATION")
                windowManager.defaultDisplay
            }

            val bestMode = display?.supportedModes?.maxByOrNull { it.refreshRate }
            if (bestMode != null) {
                val attrs = window.attributes
                attrs.preferredDisplayModeId = bestMode.modeId
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    attrs.preferredRefreshRate = bestMode.refreshRate
                }
                window.attributes = attrs

                Log.i(TAG, "Requested display mode ${bestMode.physicalWidth}x${bestMode.physicalHeight} @ ${bestMode.refreshRate}Hz")
            }
        } catch (e: Exception) {
            Log.w(TAG, "High-performance display request failed: ${e.message}")
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == VOICE_PERMISSION_REQUEST) {
            voicePermissionRequestPending.set(false)
            if (grantResults.isNotEmpty() &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED &&
                voiceCaptureWanted.get()) {
                voicePermissionDenied.set(false)
                startVoiceCapture()
            } else if (grantResults.isEmpty() ||
                grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                /* Remember a denial so open mic cannot reopen the permission
                   dialog in a loop. Toggling Voice off/on permits one fresh
                   request initiated by the user. */
                voicePermissionDenied.set(true)
                voiceCaptureWanted.set(false)
            }
        } else if (requestCode == EVENT_NOTIFICATION_PERMISSION_REQUEST) {
            eventNotificationPermissionRequestPending.set(false)
            val granted = grantResults.isNotEmpty() &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED
            val queued = pendingEventNotifications.values.toList()
            pendingEventNotifications.clear()
            if (granted) {
                for (event in queued) scheduleEventAlarm(this, event)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
    }

    override fun onPause() {
        /* Never leave the microphone or a streaming AudioTrack active while
           Vlither is in the background. Native open-mic state requests a fresh
           capture session after resume. */
        setVoiceCapture(this, false)
        stopVoicePlayback(this)
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
        }
    }

    override fun onDestroy() {
        GameplayCaptureController.onActivityDestroyed(this)
        setVoiceCapture(this, false)
        stopVoicePlayback(this)
        setTextInputActiveOnUi(false)
        clearImeEvents(this)
        imeBridgeView = null
        scanAnimator?.cancel()
        overlayRef  = null
        scanAnimator = null
        super.onDestroy()
        Log.d(TAG, "GameActivity destroyed")
    }
}
