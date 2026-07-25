package com.vlither

import android.app.Activity
import android.app.DownloadManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {

    companion object {
        private const val TAG               = "VlitherMain"
        private const val CURRENT_VERSION   = "4.1"
        private const val VERSION_URL       = "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/version.txt"
        private const val DOWNLOAD_URL_FILE = "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/download_url.txt"
        const val UNLOCK_FILENAME           = "vlither_unlock_expiry.txt"

        private const val PREFS_NAME         = "vlither_update_prefs"
        private const val PREF_PENDING_DL_ID = "pending_download_id"

        private const val ADSTERRA_UNLOCK_PAGE_URL = "https://vlither-ads.onrender.com"

        fun getUnlockRemainingMs(context: Context): Long {
            val file = File(context.filesDir, UNLOCK_FILENAME)
            if (file.exists()) {
                try {
                    val expiry = file.readText().trim().toLong()
                    val remaining = expiry - System.currentTimeMillis()
                    if (remaining > 0) return remaining
                } catch (e: Exception) { Log.w(TAG, "read error: ${e.message}") }
            }
            return -1L
        }

        @JvmStatic
        fun getUnlockRemainingMsStatic(context: Context): Long =
            getUnlockRemainingMs(context)

        fun saveUnlock(context: Context) {
            val expiryMs = System.currentTimeMillis() + 24L * 60L * 60L * 1000L
            try { File(context.filesDir, UNLOCK_FILENAME).writeText(expiryMs.toString()) }
            catch (e: Exception) { Log.e(TAG, "save error: ${e.message}") }
        }
    }

    private lateinit var btnPlay:       Button
    private lateinit var btnWatchAd:    Button
    private lateinit var tvTimer:       TextView
    private lateinit var btnChangelog:  Button
    private lateinit var layoutUpdate:  LinearLayout  // update panel, hidden by default
    private lateinit var tvUpdateMsg:   TextView
    private lateinit var btnDownload:   Button
    private lateinit var btnLater:      Button

    private var latestVersion:  String? = null
    private var apkDownloadUrl: String? = null
    private var pendingDownloadId: Long = -1L
    private var receiverRegistered = false

    private val executor    = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    /** Fires the moment DownloadManager finishes the update APK — auto-opens the install prompt. */
    private val downloadCompleteReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L)
            if (id == -1L || id != pendingDownloadId) return
            handleDownloadFinished(id)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        buildUi()
        registerDownloadReceiver()
        resumePendingDownloadIfAny()
        checkForUpdate()
        handleUnlockDeepLink(intent)
        refreshUnlockUi()
    }

    private fun registerDownloadReceiver() {
        if (receiverRegistered) return
        val filter = IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(downloadCompleteReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(downloadCompleteReceiver, filter)
        }
        receiverRegistered = true
    }

    /** Handles the case where the app was closed/killed while an update was still downloading. */
    private fun resumePendingDownloadIfAny() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedId = prefs.getLong(PREF_PENDING_DL_ID, -1L)
        if (savedId == -1L) return
        pendingDownloadId = savedId
        executor.execute { handleDownloadFinished(savedId) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleUnlockDeepLink(intent)
    }

    /** Catches vlither://unlock coming back from the browser after the ad page's timer finishes. */
    private fun handleUnlockDeepLink(intent: Intent?) {
        val data = intent?.data
        if (data != null && data.scheme == "vlither" && data.host == "unlock") {
            saveUnlock(this)
            android.widget.Toast.makeText(this, "Unlocked for 24 hours!", android.widget.Toast.LENGTH_SHORT).show()
            refreshUnlockUi()
        }
    }

    override fun onResume() {
        super.onResume()
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        refreshUnlockUi()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (receiverRegistered) {
            try { unregisterReceiver(downloadCompleteReceiver) } catch (e: Exception) { /* already gone */ }
            receiverRegistered = false
        }
        executor.shutdown()
    }

    // ── Update checker ─────────────────────────────────────────────────

    private fun checkForUpdate() {
        executor.execute {
            try {
                val latest = fetchText(VERSION_URL).trim()
                if (latest.isEmpty()) return@execute
                val dlUrl = fetchText(DOWNLOAD_URL_FILE).trim()
                Log.d(TAG, "Latest: $latest  Current: $CURRENT_VERSION")
                if (isNewerVersion(latest, CURRENT_VERSION)) {
                    latestVersion  = latest
                    apkDownloadUrl = dlUrl
                    mainHandler.post { showUpdatePanel(latest) }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Update check failed: ${e.message}")
            }
        }
    }

    private fun fetchText(urlStr: String): String {
        val conn = URL(urlStr).openConnection() as HttpURLConnection
        conn.connectTimeout = 5000
        conn.readTimeout    = 5000
        return try { conn.inputStream.bufferedReader().readText() }
        finally { conn.disconnect() }
    }

    private fun isNewerVersion(latest: String, current: String): Boolean {
        return try {
            val l = latest.split(".").map { it.toInt() }
            val c = current.split(".").map { it.toInt() }
            for (i in 0 until maxOf(l.size, c.size)) {
                val lv = l.getOrElse(i) { 0 }
                val cv = c.getOrElse(i) { 0 }
                if (lv > cv) return true
                if (lv < cv) return false
            }
            false
        } catch (e: Exception) { false }
    }

    private fun showUpdatePanel(version: String) {
        tvUpdateMsg.text = "New update available: v$version"
        layoutUpdate.visibility = View.VISIBLE
        btnChangelog.visibility = View.VISIBLE
        // Dim play button to hint user about update
        btnPlay.alpha = 0.6f
    }

    private fun showChangelog() {
        val message = """
Update 4.1

  • Improved arrow controls.
  • Controls can now be customized from a separate panel.
  • Various improvements and bug fixes.

   
  

Changes made by Lucky
        """.trimIndent()

        android.app.AlertDialog.Builder(this)
            .setTitle("What's New in v4.1")
            .setMessage(message)
            .setPositiveButton("Got it") { dialog, _ -> dialog.dismiss() }
            .show()
    }

    private fun onDownloadClicked() {
        val url = apkDownloadUrl
        if (url.isNullOrEmpty()) {
            android.widget.Toast.makeText(this,
                "Download link not ready yet.", android.widget.Toast.LENGTH_SHORT).show()
            return
        }
        try {
            val request = DownloadManager.Request(Uri.parse(url)).apply {
                setTitle("Vlither v$latestVersion")
                setDescription("Downloading update...")
                setMimeType("application/vnd.android.package-archive")
                setNotificationVisibility(
                    DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                setDestinationInExternalPublicDir(
                    Environment.DIRECTORY_DOWNLOADS, "Vlither-v$latestVersion.apk")
                setAllowedOverMetered(true)
                setAllowedOverRoaming(true)
            }
            val dm = getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
            val id = dm.enqueue(request)
            pendingDownloadId = id
            getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
                .putLong(PREF_PENDING_DL_ID, id).apply()

            btnDownload.text = "Downloading update..."
            btnDownload.isEnabled = false
            android.widget.Toast.makeText(this,
                "Downloading — the install prompt will open automatically when it's done",
                android.widget.Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Log.e(TAG, "Download failed: ${e.message}")
            // Fallback: open browser
            try { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            catch (e2: Exception) { Log.e(TAG, "Browser fallback failed: ${e2.message}") }
        }
    }

    /** Called once DownloadManager reports the update APK finished (or on relaunch, if it finished while we were gone). */
    private fun handleDownloadFinished(id: Long) {
        val dm = getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
        val cursor = dm.query(DownloadManager.Query().setFilterById(id))
        var status = -1
        cursor.use {
            if (it.moveToFirst()) {
                val idx = it.getColumnIndex(DownloadManager.COLUMN_STATUS)
                if (idx >= 0) status = it.getInt(idx)
            }
        }

        // Not in DownloadManager's table anymore, or still running/queued/paused —
        // this only happens on the relaunch-resume path, so just leave it be;
        // the broadcast receiver (already registered) will catch it once it truly finishes.
        if (status == DownloadManager.STATUS_RUNNING ||
            status == DownloadManager.STATUS_PENDING ||
            status == DownloadManager.STATUS_PAUSED) {
            return
        }

        // Terminal state (success or failure) — clear the persisted id either way.
        getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            .remove(PREF_PENDING_DL_ID).apply()
        pendingDownloadId = -1L

        if (status == DownloadManager.STATUS_SUCCESSFUL) {
            promptInstall(dm, id)
        } else {
            mainHandler.post {
                btnDownload.text = "⬇  Download Update"
                btnDownload.isEnabled = true
                android.widget.Toast.makeText(this,
                    "Update download failed. Please try again.",
                    android.widget.Toast.LENGTH_LONG).show()
            }
        }
    }

    /** Opens the system "Install app" screen for the just-downloaded APK. */
    private fun promptInstall(dm: DownloadManager, id: Long) {
        mainHandler.post {
            btnDownload.text = "⬇  Download Update"
            btnDownload.isEnabled = true
            try {
                val apkUri = dm.getUriForDownloadedFile(id)
                val installIntent = Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(apkUri, "application/vnd.android.package-archive")
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                startActivity(installIntent)
            } catch (e: Exception) {
                Log.e(TAG, "Install prompt failed: ${e.message}")
                android.widget.Toast.makeText(this,
                    "Downloaded, but couldn't open the installer automatically. " +
                    "Check your Downloads folder.",
                    android.widget.Toast.LENGTH_LONG).show()
            }
        }
    }

    // ── UI ─────────────────────────────────────────────────────────────

    private fun buildUi() {
        val root = FrameLayout(this)
        root.setBackgroundColor(0xFF0D0E14.toInt())

        val col = LinearLayout(this)
        col.orientation = LinearLayout.VERTICAL
        col.setPadding(64, 0, 64, 0)

        val colParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).also { it.gravity = android.view.Gravity.CENTER }

        // Title
        val tvTitle = TextView(this)
        tvTitle.text = "VLITHER"
        tvTitle.textSize = 40f
        tvTitle.setTextColor(0xFF2BAA60.toInt())
        tvTitle.typeface = android.graphics.Typeface.DEFAULT_BOLD
        tvTitle.gravity = android.view.Gravity.CENTER
        tvTitle.setPadding(0, 0, 0, 48)

        val btnParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).also { it.setMargins(0, 8, 0, 8) }

        // Watch ad to unlock — opens the Adsterra unlock page in the browser
        btnWatchAd = Button(this)
        btnWatchAd.text = "🌐  Watch Ad to Unlock"
        btnWatchAd.textSize = 15f
        btnWatchAd.setPadding(0, 20, 0, 20)
        btnWatchAd.layoutParams = btnParams
        btnWatchAd.isEnabled = true
        btnWatchAd.alpha = 1.0f
        btnWatchAd.setOnClickListener { onWatchAdClicked() }

        // Hidden timer placeholder
        tvTimer = TextView(this)
        tvTimer.visibility = View.GONE

        // Play button — gated behind an active unlock (must watch an ad first)
        btnPlay = Button(this)
        btnPlay.text = "▶  PLAY"
        btnPlay.textSize = 18f
        btnPlay.setPadding(0, 28, 0, 28)
        btnPlay.layoutParams = btnParams
        btnPlay.setOnClickListener { onPlayClicked() }

        // Changelog — always visible (was: hidden until an update is detected)
        btnChangelog = Button(this)
        btnChangelog.text = "📋  What's New"
        btnChangelog.textSize = 13f
        btnChangelog.setPadding(0, 16, 0, 16)
        btnChangelog.layoutParams = btnParams
        btnChangelog.setTextColor(0xFF5DADE2.toInt())
        btnChangelog.alpha = 0.85f
        btnChangelog.visibility = View.VISIBLE
        btnChangelog.setOnClickListener { showChangelog() }

        // ── Update panel — hidden until update found ──────────────────
        layoutUpdate = LinearLayout(this)
        layoutUpdate.orientation = LinearLayout.VERTICAL
        layoutUpdate.visibility = View.GONE
        layoutUpdate.setPadding(0, 16, 0, 0)

        // Divider line
        val divider = View(this)
        divider.setBackgroundColor(0xFF2BAA60.toInt())
        val divParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 2)
        divParams.setMargins(0, 8, 0, 16)
        divider.layoutParams = divParams

        tvUpdateMsg = TextView(this)
        tvUpdateMsg.textSize = 14f
        tvUpdateMsg.setTextColor(0xFF5DADE2.toInt())
        tvUpdateMsg.gravity = android.view.Gravity.CENTER
        tvUpdateMsg.setPadding(0, 0, 0, 12)

        btnDownload = Button(this)
        btnDownload.text = "⬇  Download Update"
        btnDownload.textSize = 14f
        btnDownload.setPadding(0, 20, 0, 20)
        btnDownload.layoutParams = btnParams
        btnDownload.setBackgroundColor(0xFF1A5276.toInt())
        btnDownload.setTextColor(0xFFADD8E6.toInt())
        btnDownload.setOnClickListener { onDownloadClicked() }

        btnLater = Button(this)
        btnLater.text = "Later — Play Current Version"
        btnLater.textSize = 13f
        btnLater.setPadding(0, 16, 0, 16)
        btnLater.layoutParams = btnParams
        btnLater.alpha = 0.6f
        btnLater.setOnClickListener {
            // Hide update panel and restore play button
            layoutUpdate.visibility = View.GONE
            btnPlay.alpha = 1.0f
        }

        layoutUpdate.addView(divider)
        layoutUpdate.addView(tvUpdateMsg)
        layoutUpdate.addView(btnDownload)
        layoutUpdate.addView(btnLater)

        col.addView(tvTitle)
        col.addView(btnWatchAd)
        col.addView(tvTimer)
        col.addView(btnPlay)
        col.addView(btnChangelog)
        col.addView(layoutUpdate)
        root.addView(col, colParams)
        setContentView(root)
    }

    private fun onPlayClicked() {
        if (getUnlockRemainingMs(this) > 0) {
            launchGame()
        } else {
            onWatchAdClicked()
        }
    }

    private fun launchGame() {
        try {
            val intent = Intent(this, GameActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            startActivity(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to launch GameActivity: ${e.message}")
            android.widget.Toast.makeText(this,
                "Failed to start game: ${e.message}",
                android.widget.Toast.LENGTH_LONG).show()
        }
    }

    // ── Ads (Adsterra, via browser + deep-link callback) ────────────────

    private fun onWatchAdClicked() {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(ADSTERRA_UNLOCK_PAGE_URL)))
        } catch (e: Exception) {
            Log.e(TAG, "Failed to open unlock page: ${e.message}")
            android.widget.Toast.makeText(this,
                "Couldn't open the browser: ${e.message}",
                android.widget.Toast.LENGTH_LONG).show()
        }
    }

    /** Reflects current unlock status on btnWatchAd / btnPlay / tvTimer. */
    private fun refreshUnlockUi() {
        val remaining = getUnlockRemainingMs(this)
        if (remaining > 0) {
            val totalMinutes = remaining / 60000L
            val hours = totalMinutes / 60
            val minutes = totalMinutes % 60
            btnWatchAd.text = "✓  Unlocked"
            btnWatchAd.isEnabled = false
            btnWatchAd.alpha = 0.6f
            tvTimer.visibility = View.VISIBLE
            tvTimer.text = "Unlocked — ${hours}h ${minutes}m left"
            btnPlay.text = "▶  PLAY"
            btnPlay.isEnabled = true
            btnPlay.alpha = 1.0f
        } else {
            btnWatchAd.text = "🌐  Watch Ad to Unlock"
            btnWatchAd.isEnabled = true
            btnWatchAd.alpha = 1.0f
            tvTimer.visibility = View.GONE
            btnPlay.text = "🔒  Watch Ad to Play"
            btnPlay.isEnabled = true
            btnPlay.alpha = 1.0f
        }
    }
}