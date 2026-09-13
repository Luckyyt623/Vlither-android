package com.vlither

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.ProgressBar
import android.widget.TextView

/**
 * In-app browser used by the "Tags store" button on the title screen. Keeps
 * the player inside Vlither instead of bouncing them out to the system
 * browser, with a visible close control to return to the game.
 *
 * Launched from native code via android_jni_open_webview() (android_jni.c),
 * which passes the target URL as the "url" string extra.
 */
class TagsStoreActivity : Activity() {

    companion object {
        const val EXTRA_URL = "url"
        const val DEFAULT_URL = "https://vlitherandroid.onrender.com/store"
    }

    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val url = intent.getStringExtra(EXTRA_URL) ?: DEFAULT_URL

        val root = FrameLayout(this).apply {
            setBackgroundColor(Color.BLACK)
        }

        webView = WebView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.loadWithOverviewMode = true
            settings.useWideViewPort = true
        }

        val progressBar = ProgressBar(this).apply {
            isIndeterminate = true
            layoutParams = FrameLayout.LayoutParams(
                dp(28), dp(28),
            ).apply { gravity = Gravity.CENTER }
        }

        webView.webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(
                view: WebView,
                request: WebResourceRequest,
            ): Boolean {
                val requestUrl = request.url
                val host = requestUrl.host ?: ""
                // Keep the site itself inside the WebView; send everything
                // else (Discord invite, mailto:, GitHub release, etc.) out
                // to the system so it opens in the right app.
                return if (host.endsWith("onrender.com")) {
                    false
                } else {
                    try {
                        startActivity(Intent(Intent.ACTION_VIEW, requestUrl))
                    } catch (_: Exception) {
                    }
                    true
                }
            }

            override fun onPageFinished(view: WebView, url: String?) {
                progressBar.visibility = View.GONE
            }
        }

        val closeButton = TextView(this).apply {
            text = "\u2715" // ×
            setTextColor(Color.WHITE)
            textSize = 20f
            gravity = Gravity.CENTER
            setBackgroundColor(Color.argb(160, 20, 20, 20))
            val size = dp(40)
            layoutParams = FrameLayout.LayoutParams(size, size).apply {
                gravity = Gravity.TOP or Gravity.END
                topMargin = dp(16)
                rightMargin = dp(16)
            }
            setOnClickListener { finish() }
        }

        root.addView(webView)
        root.addView(progressBar)
        root.addView(closeButton)
        setContentView(root)

        webView.loadUrl(url)
    }

    private fun dp(value: Int): Int = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics,
    ).toInt()

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (webView.canGoBack()) {
                webView.goBack()
            } else {
                finish()
            }
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onDestroy() {
        webView.destroy()
        super.onDestroy()
    }
}
