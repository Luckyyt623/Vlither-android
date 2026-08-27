package com.vlither

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaRecorder
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.provider.MediaStore
import android.util.DisplayMetrics
import android.util.Log
import android.view.PixelCopy
import android.widget.Toast
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.min

/**
 * Bridges the native game state to Android's MediaProjection consent flow.
 * Recording is intentionally session-scoped: it may run only while a real
 * server session is active, and is finalized as soon as the homepage opens.
 */
object GameplayCaptureController {
    const val CAPTURE_REQUEST_CODE = 4404
    private const val TAG = "VlitherCapture"
    private const val SCREENSHOT_DELAY_MS = 150L

    private var recordingEnabled = false
    private var screenshotsEnabled = false
    private var sessionActive = false
    private var consentOpen = false
    private val imageWriter = Executors.newSingleThreadExecutor()

    fun sync(
        activity: GameActivity,
        enableRecording: Boolean,
        enableScreenshots: Boolean,
        active: Boolean
    ) {
        recordingEnabled = enableRecording
        screenshotsEnabled = enableScreenshots
        val activeChanged = sessionActive != active
        sessionActive = active

        if (!recordingEnabled || !sessionActive) {
            GameplayCaptureService.stop(activity)
            return
        }

        if (activeChanged || !GameplayCaptureService.isRecording.get()) {
            requestRecordingConsent(activity)
        }
    }

    private fun requestRecordingConsent(activity: GameActivity) {
        if (consentOpen || !recordingEnabled || !sessionActive ||
            GameplayCaptureService.isRecording.get()) return
        val manager = activity.getSystemService(Context.MEDIA_PROJECTION_SERVICE)
            as? MediaProjectionManager ?: return
        consentOpen = true
        try {
            activity.startActivityForResult(
                manager.createScreenCaptureIntent(), CAPTURE_REQUEST_CODE
            )
        } catch (e: Exception) {
            consentOpen = false
            Log.e(TAG, "Could not open screen-capture consent", e)
            Toast.makeText(activity, "Screen recording is unavailable", Toast.LENGTH_SHORT)
                .show()
        }
    }

    fun handleActivityResult(
        activity: GameActivity,
        resultCode: Int,
        data: Intent?
    ) {
        consentOpen = false
        if (resultCode != Activity.RESULT_OK || data == null) {
            Toast.makeText(activity, "Gameplay recording not started", Toast.LENGTH_SHORT)
                .show()
            return
        }
        if (!recordingEnabled || !sessionActive) return

        @Suppress("DEPRECATION")
        val metrics = DisplayMetrics().also {
            activity.windowManager.defaultDisplay.getRealMetrics(it)
        }
        var sourceWidth = metrics.widthPixels.coerceAtLeast(2)
        var sourceHeight = metrics.heightPixels.coerceAtLeast(2)
        val scale = min(1.0f, 1280.0f / maxOf(sourceWidth, sourceHeight).toFloat())
        sourceWidth = ((sourceWidth * scale).toInt().coerceAtLeast(2) / 2) * 2
        sourceHeight = ((sourceHeight * scale).toInt().coerceAtLeast(2) / 2) * 2

        GameplayCaptureService.start(
            activity, resultCode, data, sourceWidth, sourceHeight,
            metrics.densityDpi.coerceAtLeast(1)
        )
    }

    fun onConfirmedKill(activity: GameActivity) {
        if (!screenshotsEnabled || !sessionActive) return
        Handler(Looper.getMainLooper()).postDelayed({
            if (!screenshotsEnabled || !sessionActive || activity.isFinishing) return@postDelayed
            captureWindow(activity)
        }, SCREENSHOT_DELAY_MS)
    }

    private fun captureWindow(activity: GameActivity) {
        val decor = activity.window.decorView
        val width = decor.width
        val height = decor.height
        if (width <= 0 || height <= 0) return
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        try {
            PixelCopy.request(activity.window, bitmap, { result ->
                if (result != PixelCopy.SUCCESS) {
                    bitmap.recycle()
                    Log.w(TAG, "Kill screenshot PixelCopy failed: $result")
                    return@request
                }
                imageWriter.execute {
                    val saved = saveKillScreenshot(activity.applicationContext, bitmap)
                    bitmap.recycle()
                    if (saved) {
                        Handler(Looper.getMainLooper()).post {
                            Toast.makeText(
                                activity, "Kill screenshot saved", Toast.LENGTH_SHORT
                            ).show()
                        }
                    }
                }
            }, Handler(Looper.getMainLooper()))
        } catch (e: Exception) {
            bitmap.recycle()
            Log.e(TAG, "Could not capture kill screenshot", e)
        }
    }

    private fun saveKillScreenshot(context: Context, bitmap: Bitmap): Boolean {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.US).format(Date())
        val name = "Vlither-Kill-$stamp.png"
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val values = ContentValues().apply {
                    put(MediaStore.Images.Media.DISPLAY_NAME, name)
                    put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                    put(
                        MediaStore.Images.Media.RELATIVE_PATH,
                        "${Environment.DIRECTORY_PICTURES}/Vlither/Kills"
                    )
                    put(MediaStore.Images.Media.IS_PENDING, 1)
                }
                val resolver = context.contentResolver
                val uri = resolver.insert(
                    MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values
                ) ?: return false
                var ok = false
                try {
                    resolver.openOutputStream(uri, "w")?.use { stream ->
                        ok = bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
                    }
                    if (ok) {
                        values.clear()
                        values.put(MediaStore.Images.Media.IS_PENDING, 0)
                        resolver.update(uri, values, null, null)
                    } else {
                        resolver.delete(uri, null, null)
                    }
                } catch (e: Exception) {
                    resolver.delete(uri, null, null)
                    throw e
                }
                ok
            } else {
                val root = context.getExternalFilesDir(Environment.DIRECTORY_PICTURES)
                    ?: context.filesDir
                val dir = File(root, "Vlither/Kills").apply { mkdirs() }
                val file = File(dir, name)
                FileOutputStream(file).use {
                    if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) return false
                }
                MediaScannerConnection.scanFile(
                    context, arrayOf(file.absolutePath), arrayOf("image/png"), null
                )
                true
            }
        } catch (e: Exception) {
            Log.e(TAG, "Could not save kill screenshot", e)
            false
        }
    }

    fun onActivityDestroyed(activity: GameActivity) {
        consentOpen = false
        sessionActive = false
        GameplayCaptureService.stop(activity)
    }
}

/** Foreground service required for MediaProjection recording on Android 10+. */
class GameplayCaptureService : Service() {
    companion object {
        private const val TAG = "VlitherCapture"
        private const val CHANNEL_ID = "vlither_gameplay_recording"
        private const val NOTIFICATION_ID = 4705
        private const val ACTION_START = "com.vlither.capture.START"
        private const val ACTION_STOP = "com.vlither.capture.STOP"
        private const val EXTRA_RESULT_CODE = "result_code"
        private const val EXTRA_RESULT_DATA = "result_data"
        private const val EXTRA_WIDTH = "width"
        private const val EXTRA_HEIGHT = "height"
        private const val EXTRA_DENSITY = "density"
        val isRecording = AtomicBoolean(false)
        private val captureRequested = AtomicBoolean(false)

        fun start(
            context: Context,
            resultCode: Int,
            resultData: Intent,
            width: Int,
            height: Int,
            density: Int
        ) {
            captureRequested.set(true)
            val intent = Intent(context, GameplayCaptureService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_RESULT_CODE, resultCode)
                putExtra(EXTRA_RESULT_DATA, resultData)
                putExtra(EXTRA_WIDTH, width)
                putExtra(EXTRA_HEIGHT, height)
                putExtra(EXTRA_DENSITY, density)
            }
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(intent)
                } else {
                    context.startService(intent)
                }
            } catch (e: Exception) {
                captureRequested.set(false)
                Log.e(TAG, "Could not launch capture service", e)
                Toast.makeText(context, "Gameplay recording failed", Toast.LENGTH_SHORT)
                    .show()
            }
        }

        fun stop(context: Context) {
            if (!captureRequested.getAndSet(false) && !isRecording.get()) return
            try {
                context.startService(
                    Intent(context, GameplayCaptureService::class.java).apply {
                        action = ACTION_STOP
                    }
                )
            } catch (e: Exception) {
                Log.w(TAG, "Could not send capture stop: ${e.message}")
                context.stopService(Intent(context, GameplayCaptureService::class.java))
            }
        }
    }

    private data class OutputTarget(
        val uri: Uri?,
        val file: File?,
        val descriptor: ParcelFileDescriptor?
    )

    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var recorder: MediaRecorder? = null
    private var output: OutputTarget? = null
    private var started = false
    private var stopping = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID, "Gameplay recording", NotificationManager.IMPORTANCE_LOW
                ).apply {
                    description = "Shows while Vlither records an active server session"
                    setShowBadge(false)
                }
            )
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> stopCapture(true)
            ACTION_START -> startCapture(intent)
            else -> stopSelf()
        }
        return START_NOT_STICKY
    }

    private fun foregroundNotification(): Notification {
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setSmallIcon(com.vlither.R.mipmap.ic_launcher)
            .setContentTitle("Vlither gameplay recording")
            .setContentText("Recording stops automatically on the homepage")
            .setOngoing(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .build()
    }

    @Suppress("DEPRECATION")
    private fun startCapture(intent: Intent) {
        if (started || stopping) return
        if (!captureRequested.get()) {
            stopSelf()
            return
        }
        val notification = foregroundNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED)
        val resultData = intent.getParcelableExtra<Intent>(EXTRA_RESULT_DATA)
        val width = intent.getIntExtra(EXTRA_WIDTH, 1280).coerceAtLeast(2)
        val height = intent.getIntExtra(EXTRA_HEIGHT, 720).coerceAtLeast(2)
        val density = intent.getIntExtra(EXTRA_DENSITY, 320).coerceAtLeast(1)
        if (resultCode != Activity.RESULT_OK || resultData == null) {
            stopCapture(false)
            return
        }

        try {
            output = createVideoOutput()
            val target = output ?: throw IllegalStateException("No video output")
            recorder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                MediaRecorder(this)
            } else {
                MediaRecorder()
            }.apply {
                setVideoSource(MediaRecorder.VideoSource.SURFACE)
                setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
                setVideoEncoder(MediaRecorder.VideoEncoder.H264)
                setVideoEncodingBitRate(6_000_000)
                setVideoFrameRate(30)
                setVideoSize(width, height)
                if (target.descriptor != null) {
                    setOutputFile(target.descriptor.fileDescriptor)
                } else {
                    setOutputFile(target.file!!.absolutePath)
                }
                prepare()
            }

            val manager = getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                as MediaProjectionManager
            projection = manager.getMediaProjection(resultCode, resultData).apply {
                registerCallback(object : MediaProjection.Callback() {
                    override fun onStop() {
                        stopCapture(true, stopProjection = false)
                    }
                }, Handler(Looper.getMainLooper()))
            }
            virtualDisplay = projection!!.createVirtualDisplay(
                "VlitherGameplay", width, height, density,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                recorder!!.surface, null, null
            )
            recorder!!.start()
            started = true
            isRecording.set(true)
            Toast.makeText(this, "Gameplay recording started", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Log.e(TAG, "Could not start gameplay recording", e)
            stopCapture(false)
            Toast.makeText(this, "Gameplay recording failed", Toast.LENGTH_SHORT).show()
        }
    }

    private fun createVideoOutput(): OutputTarget {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        val name = "Vlither-Gameplay-$stamp.mp4"
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, name)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(
                    MediaStore.Video.Media.RELATIVE_PATH,
                    "${Environment.DIRECTORY_MOVIES}/Vlither"
                )
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val uri = contentResolver.insert(
                MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values
            ) ?: throw IllegalStateException("Could not create MediaStore video")
            val descriptor = contentResolver.openFileDescriptor(uri, "rw")
                ?: run {
                    contentResolver.delete(uri, null, null)
                    throw IllegalStateException("Could not open MediaStore video")
                }
            OutputTarget(uri, null, descriptor)
        } else {
            val root = getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: filesDir
            val dir = File(root, "Vlither").apply { mkdirs() }
            OutputTarget(null, File(dir, name), null)
        }
    }

    private fun finishVideoOutput(valid: Boolean) {
        val target = output ?: return
        try {
            target.descriptor?.close()
        } catch (_: Exception) {
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && target.uri != null) {
            if (valid) {
                val values = ContentValues().apply {
                    put(MediaStore.Video.Media.IS_PENDING, 0)
                }
                contentResolver.update(target.uri, values, null, null)
            } else {
                contentResolver.delete(target.uri, null, null)
            }
        } else if (target.file != null) {
            if (valid) {
                MediaScannerConnection.scanFile(
                    this, arrayOf(target.file.absolutePath), arrayOf("video/mp4"), null
                )
            } else {
                target.file.delete()
            }
        }
        output = null
    }

    private fun stopCapture(showSaved: Boolean, stopProjection: Boolean = true) {
        if (stopping) return
        stopping = true
        val wasStarted = started
        var validOutput = wasStarted
        captureRequested.set(false)
        isRecording.set(false)
        try {
            if (started) recorder?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Recorder stop failed: ${e.message}")
            validOutput = false
        }
        try {
            virtualDisplay?.release()
        } catch (_: Exception) {
        }
        virtualDisplay = null
        try {
            recorder?.reset()
            recorder?.release()
        } catch (_: Exception) {
        }
        recorder = null
        if (stopProjection) {
            try {
                projection?.stop()
            } catch (_: Exception) {
            }
        }
        projection = null
        finishVideoOutput(validOutput)
        started = false

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
        if (showSaved && validOutput) {
            Toast.makeText(this, "Gameplay saved in Movies/Vlither", Toast.LENGTH_SHORT)
                .show()
        }
        stopSelf()
        stopping = false
    }

    override fun onDestroy() {
        if (!stopping && (started || output != null)) stopCapture(false)
        super.onDestroy()
    }
}
