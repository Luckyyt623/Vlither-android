package com.vlither

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.app.NativeActivity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
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
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicInteger

class GameActivity : NativeActivity() {

    companion object {
        private const val TAG = "VlitherGame"

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

    override fun onResume() {
        super.onResume()
        hideSystemBars()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun onDestroy() {
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
