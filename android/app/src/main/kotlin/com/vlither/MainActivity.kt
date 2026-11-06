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
import android.widget.ProgressBar
import android.widget.TextView
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {

    companion object {
        private const val TAG               = "VlitherMain"
        private const val CURRENT_VERSION   = "4.8.1"
        private const val VERSION_URL       = "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/version.txt"
        private const val DOWNLOAD_URL_FILE = "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/download_url.txt"

        private const val PREFS_NAME         = "vlither_update_prefs"
        private const val PREF_PENDING_DL_ID = "pending_download_id"
    }

    private lateinit var btnPlay:       Button
    private lateinit var btnChangelog:  Button
    private lateinit var layoutUpdate:  LinearLayout
    private lateinit var tvUpdateMsg:   TextView
    private lateinit var btnDownload:   Button
    private lateinit var btnLater:      Button
    private lateinit var progressUpdate: ProgressBar
    private lateinit var tvProgress:    TextView
    private var updateAvailable = false
    private var progressPolling = false

    private var latestVersion:  String? = null
    private var apkDownloadUrl: String? = null
    @Volatile private var pendingDownloadId: Long = -1L
    private var receiverRegistered = false

    private val executor    = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    private val downloadCompleteReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L)
            if (id == -1L || id != pendingDownloadId) return
            handleDownloadFinished(id)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        GameActivity.applySavedOrientation(this)
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

    private fun resumePendingDownloadIfAny() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedId = prefs.getLong(PREF_PENDING_DL_ID, -1L)
        if (savedId == -1L) return
        pendingDownloadId = savedId
        startProgressPolling(savedId)
        executor.execute { handleDownloadFinished(savedId) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (receiverRegistered) {
            try { unregisterReceiver(downloadCompleteReceiver) } catch (e: Exception) {  }
            receiverRegistered = false
        }
        executor.shutdown()
    }

    private fun checkForUpdate() {
        if (pendingDownloadId == -1L) {
            tvUpdateMsg.text = "Checking for updates…"
            btnDownload.isEnabled = false
        }
        executor.execute {
            try {
                val latest = fetchText(VERSION_URL).trim()
                if (latest.isEmpty()) {
                    mainHandler.post { showCheckFailed() }
                    return@execute
                }
                val dlUrl = fetchText(DOWNLOAD_URL_FILE).trim()
                Log.d(TAG, "Latest: $latest  Current: $CURRENT_VERSION")
                if (isNewerVersion(latest, CURRENT_VERSION)) {
                    latestVersion  = latest
                    apkDownloadUrl = dlUrl
                    mainHandler.post { showUpdatePanel(latest) }
                } else {
                    mainHandler.post { showUpToDate() }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Update check failed: ${e.message}")
                mainHandler.post { showCheckFailed() }
            }
        }
    }

    private fun showUpToDate() {
        updateAvailable = false
        if (pendingDownloadId != -1L) return
        tvUpdateMsg.text = "You're up to date (v$CURRENT_VERSION)"
        btnDownload.text = "🔄  Check for Update"
        btnDownload.isEnabled = true
        btnLater.visibility = View.GONE
    }

    private fun showCheckFailed() {
        updateAvailable = false
        if (pendingDownloadId != -1L) return
        tvUpdateMsg.text = "Couldn't check for updates"
        btnDownload.text = "🔄  Check for Update"
        btnDownload.isEnabled = true
        btnLater.visibility = View.GONE
    }

    private fun startProgressPolling(id: Long) {
        if (progressPolling) return
        progressPolling = true
        progressUpdate.visibility = View.VISIBLE
        tvProgress.visibility = View.VISIBLE
        btnDownload.text = "Downloading update…"
        btnDownload.isEnabled = false
        val poll = object : Runnable {
            override fun run() {
                if (!progressPolling || pendingDownloadId != id) { hideProgress(); return }
                val dm = getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
                var status = -1; var got = 0L; var total = -1L
                dm.query(DownloadManager.Query().setFilterById(id)).use { c ->
                    if (c.moveToFirst()) {
                        status = c.getInt(c.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS))
                        got    = c.getLong(c.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR))
                        total  = c.getLong(c.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES))
                    }
                }
                when (status) {
                    DownloadManager.STATUS_PENDING -> {
                        progressUpdate.isIndeterminate = true
                        tvProgress.text = "Starting download…"
                    }
                    DownloadManager.STATUS_PAUSED -> {
                        tvProgress.text = "Download paused — waiting for network…"
                    }
                    DownloadManager.STATUS_RUNNING -> {
                        if (total > 0) {
                            val pct = (got * 100 / total).toInt().coerceIn(0, 100)
                            progressUpdate.isIndeterminate = false
                            progressUpdate.progress = pct
                            tvProgress.text = "Downloading $pct%  (${fmtMb(got)} / ${fmtMb(total)})"
                        } else {
                            progressUpdate.isIndeterminate = true
                            tvProgress.text = "Downloading… ${fmtMb(got)}"
                        }
                    }
                    DownloadManager.STATUS_SUCCESSFUL, DownloadManager.STATUS_FAILED, -1 -> {
                        progressPolling = false
                        executor.execute { handleDownloadFinished(id) }
                        return
                    }
                }
                mainHandler.postDelayed(this, 400)
            }
        }
        mainHandler.post(poll)
    }

    private fun hideProgress() {
        progressPolling = false
        progressUpdate.visibility = View.GONE
        tvProgress.visibility = View.GONE
    }

    private fun fmtMb(b: Long): String = String.format("%.1f MB", b / 1048576.0)

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
        updateAvailable = true
        layoutUpdate.visibility = View.VISIBLE
        btnLater.visibility = View.VISIBLE
        if (pendingDownloadId == -1L) {
            tvUpdateMsg.text = "New update available: v$version"
            btnDownload.text = "⬇  Download Update"
            btnDownload.isEnabled = true
        }
        btnChangelog.visibility = View.VISIBLE
        btnPlay.alpha = 0.6f
    }

    private fun showChangelog() {
        val message = """
Vlither 4.8.1
• Tags are fixable 
• NTL private tags are load only on your side neither NTL players nor Vlither android players can see that
• Only older NTL tags are supported 
• More NTL public tags are added
• Changes made in event page.

Changes by Lucky
        """.trimIndent()

        android.app.AlertDialog.Builder(this)
            .setTitle("What's New in v4.8.1")
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

            startProgressPolling(id)
            android.widget.Toast.makeText(this,
                "Downloading — the install prompt will open automatically when it's done",
                android.widget.Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Log.e(TAG, "Download failed: ${e.message}")
            try { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            catch (e2: Exception) { Log.e(TAG, "Browser fallback failed: ${e2.message}") }
        }
    }

    @Synchronized
    private fun handleDownloadFinished(id: Long) {
        if (pendingDownloadId != id) return
        val dm = getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
        val cursor = dm.query(DownloadManager.Query().setFilterById(id))
        var status = -1
        cursor.use {
            if (it.moveToFirst()) {
                val idx = it.getColumnIndex(DownloadManager.COLUMN_STATUS)
                if (idx >= 0) status = it.getInt(idx)
            }
        }

        if (status == DownloadManager.STATUS_RUNNING ||
            status == DownloadManager.STATUS_PENDING ||
            status == DownloadManager.STATUS_PAUSED) {
            return
        }

        getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            .remove(PREF_PENDING_DL_ID).apply()
        pendingDownloadId = -1L

        if (status == DownloadManager.STATUS_SUCCESSFUL) {
            promptInstall(dm, id)
        } else {
            mainHandler.post {
                hideProgress()
                btnDownload.text = "⬇  Download Update"
                btnDownload.isEnabled = true
                android.widget.Toast.makeText(this,
                    "Update download failed. Please try again.",
                    android.widget.Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun promptInstall(dm: DownloadManager, id: Long) {
        mainHandler.post {
            hideProgress()
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

        val tvTitle = AnimatedLogoView(this)
        tvTitle.setPadding(0, 0, 0, 48)

        val btnParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).also { it.setMargins(0, 8, 0, 8) }

        btnPlay = Button(this)
        btnPlay.text = "▶  PLAY"
        btnPlay.textSize = 18f
        btnPlay.setPadding(0, 28, 0, 28)
        btnPlay.layoutParams = btnParams
        btnPlay.setOnClickListener { launchGame() }

        btnChangelog = Button(this)
        btnChangelog.text = "📋  What's New"
        btnChangelog.textSize = 13f
        btnChangelog.setPadding(0, 16, 0, 16)
        btnChangelog.layoutParams = btnParams
        btnChangelog.setTextColor(0xFF5DADE2.toInt())
        btnChangelog.alpha = 0.85f
        btnChangelog.visibility = View.VISIBLE
        btnChangelog.setOnClickListener { showChangelog() }

        layoutUpdate = LinearLayout(this)
        layoutUpdate.orientation = LinearLayout.VERTICAL
        layoutUpdate.visibility = View.VISIBLE
        layoutUpdate.setPadding(0, 16, 0, 0)

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
        tvUpdateMsg.text = "Checking for updates…"

        progressUpdate = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal)
        progressUpdate.max = 100
        progressUpdate.visibility = View.GONE
        progressUpdate.layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 24).also { it.setMargins(0, 4, 0, 4) }

        tvProgress = TextView(this)
        tvProgress.textSize = 13f
        tvProgress.setTextColor(0xFFADD8E6.toInt())
        tvProgress.gravity = android.view.Gravity.CENTER
        tvProgress.visibility = View.GONE

        btnDownload = Button(this)
        btnDownload.text = "🔄  Check for Update"
        btnDownload.isEnabled = false
        btnDownload.textSize = 14f
        btnDownload.setPadding(0, 20, 0, 20)
        btnDownload.layoutParams = btnParams
        btnDownload.setBackgroundColor(0xFF1A5276.toInt())
        btnDownload.setTextColor(0xFFADD8E6.toInt())
        btnDownload.setOnClickListener {
            if (updateAvailable) onDownloadClicked() else checkForUpdate()
        }

        btnLater = Button(this)
        btnLater.text = "Later — Play Current Version"
        btnLater.textSize = 13f
        btnLater.setPadding(0, 16, 0, 16)
        btnLater.layoutParams = btnParams
        btnLater.alpha = 0.6f
        btnLater.visibility = View.GONE
        btnLater.setOnClickListener {
            btnLater.visibility = View.GONE
            btnPlay.alpha = 1.0f
        }

        layoutUpdate.addView(divider)
        layoutUpdate.addView(tvUpdateMsg)
        layoutUpdate.addView(progressUpdate)
        layoutUpdate.addView(tvProgress)
        layoutUpdate.addView(btnDownload)
        layoutUpdate.addView(btnLater)

        col.addView(tvTitle)
        col.addView(btnPlay)
        col.addView(btnChangelog)
        col.addView(layoutUpdate)
        col.gravity = android.view.Gravity.CENTER_VERTICAL
        val scroll = android.widget.ScrollView(this)
        scroll.isFillViewport = true
        scroll.isVerticalScrollBarEnabled = false
        scroll.addView(col, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.WRAP_CONTENT))
        root.addView(scroll, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT))
        setContentView(root)
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
}
