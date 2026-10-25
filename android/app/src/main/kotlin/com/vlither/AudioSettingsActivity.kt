package com.vlither

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.widget.Toast
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView

/**
 * "Sound/Song management" screen, opened from the homepage.
 * Music and sound effects are OFF by default. All values are stored through GameAudio.
 */
class AudioSettingsActivity : Activity() {

    private val bgColor = Color.parseColor("#14161C")
    private val cardColor = Color.parseColor("#1F232C")
    private val accent = Color.parseColor("#7C6CF0")
    private val dim = Color.parseColor("#9AA3B2")

    private lateinit var content: LinearLayout
    private var previewing: String? = null
    private val previewButtons = HashMap<String, Button>()

    private val sfxList = arrayOf(
        arrayOf(GameAudio.SFX_EAT, "Eat food sound", "Plays when you eat food (pitch follows food size)"),
        arrayOf(GameAudio.SFX_DEAD, "Dead snake sound", "Plays when a snake dies on your screen"),
        arrayOf(GameAudio.SFX_BOOST, "Boost sound", "Loop while you boost"),
        arrayOf(GameAudio.SFX_DIE, "Your death sound", "Needs sfx/die.ogg (or mp3/wav) in assets"),
        arrayOf(GameAudio.SFX_KILL, "Kill sound", "Needs sfx/kill.ogg (or mp3/wav) in assets")
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        GameAudio.prepare(this)

        val scroll = ScrollView(this).apply {
            setBackgroundColor(bgColor)
            isFillViewport = true
        }
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(28), dp(16), dp(28), dp(28))
        }
        scroll.addView(
            content,
            ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        )
        setContentView(scroll)
        buildContent()
    }

    private val reqPickSong = 4711

    private fun pickSongs() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "audio/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        }
        try {
            startActivityForResult(i, reqPickSong)
        } catch (e: Exception) {
            Toast.makeText(this, "No file picker found on this phone", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != reqPickSong || resultCode != RESULT_OK || data == null) return
        val uris = ArrayList<android.net.Uri>()
        data.data?.let { uris.add(it) }
        val clip = data.clipData
        if (clip != null) for (k in 0 until clip.itemCount) uris.add(clip.getItemAt(k).uri)
        if (uris.isEmpty()) return
        Thread {
            var added = 0
            var error: String? = null
            for (u in uris) {
                val r = GameAudio.importSong(this, u)
                if (r == null) added++ else error = r
            }
            runOnUiThread {
                val msg = when {
                    added > 0 && error == null -> "Added $added song(s)"
                    added > 0 -> "Added $added song(s). Some failed: $error"
                    else -> error ?: "Nothing added"
                }
                Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
                buildContent()
            }
        }.start()
    }

    override fun onPause() {
        super.onPause()
        GameAudio.stopPreview()
        previewing = null
    }

    // ------------------------------------------------------------ UI

    private fun buildContent() {
        content.removeAllViews()
        previewButtons.clear()
        previewing = null

        // header
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(Button(this).apply {
            text = "< Back"
            setOnClickListener { finish() }
        })
        header.addView(TextView(this).apply {
            text = "Sound / Song management"
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            setTypeface(typeface, Typeface.BOLD)
            setPadding(dp(14), 0, 0, 0)
        })
        content.addView(header)

        content.addView(note("Music and sound are OFF by default. Turn them on below."))

        // master switches
        content.addView(sectionTitle("Master"))
        val master = card()
        master.addView(switchRow("Music (songs while playing)", GameAudio.K_MUSIC_ON))
        master.addView(switchRow("Sound effects", GameAudio.K_SFX_ON))
        content.addView(master)

        // music
        content.addView(sectionTitle("Music"))
        val music = card()
        music.addView(sliderRow("Music volume", GameAudio.K_MUSIC_VOL, 300, "%", "100% = original loudness"))
        music.addView(sliderRow("Random start delay", GameAudio.K_MUSIC_DELAY, 30, " s",
            "0 = song starts as soon as you spawn; otherwise it starts after a random 0 to N seconds"))
        music.addView(label("Songs (ON = in the rotation, OFF = skipped)", dim, 13f))
        music.addView(Button(this).apply {
            text = "+ Add song from phone storage"
            setOnClickListener { pickSongs() }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(8)
        })
        music.addView(note("Added songs are saved inside the game and play in the rotation (mp3, ogg, m4a, wav, max 60 MB each)."))
        val songs = GameAudio.listSongs(this)
        if (songs.isEmpty()) {
            music.addView(note("No songs found. Tap \"Add song from phone storage\" above."))
        } else {
            for (name in songs) music.addView(songRow(name))
        }
        content.addView(music)

        // sound effects
        content.addView(sectionTitle("Sound effects"))
        for (s in sfxList) content.addView(sfxCard(s[0], s[1], s[2]))

        // reset
        content.addView(Button(this).apply {
            text = "Reset everything to default (all OFF)"
            setOnClickListener {
                GameAudio.stopPreview()
                GameAudio.resetConfig(this@AudioSettingsActivity)
                buildContent()
            }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(18)
        })
    }

    /** Big coloured ON/OFF button (works in every theme, unlike the Switch widget). */
    private fun toggleButton(get: () -> Boolean, set: (Boolean) -> Unit): Button {
        var on = get()
        val btn = Button(this)
        styleToggle(btn, on)
        btn.setOnClickListener {
            on = !on
            set(on)
            styleToggle(btn, on)
            GameAudio.onConfigChanged(this@AudioSettingsActivity)
        }
        return btn
    }

    private fun styleToggle(btn: Button, on: Boolean) {
        btn.text = if (on) "ON" else "OFF"
        btn.isAllCaps = false
        btn.setTextColor(Color.WHITE)
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
        btn.setTypeface(btn.typeface, Typeface.BOLD)
        btn.minWidth = dp(84)
        btn.minimumWidth = dp(84)
        btn.background = GradientDrawable().apply {
            setColor(if (on) Color.parseColor("#2E9E5B") else Color.parseColor("#7A3040"))
            cornerRadius = dp(10).toFloat()
        }
    }

    private fun switchRow(text: String, key: String): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(6), 0, dp(6))
        }
        row.addView(
            label(text, Color.WHITE, 16f),
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )
        row.addView(toggleButton(
            { GameAudio.getBool(this@AudioSettingsActivity, key) },
            { GameAudio.setBool(this@AudioSettingsActivity, key, it) }
        ))
        return row
    }

    private fun sliderRow(title: String, key: String, max: Int, unit: String, hint: String?): View {
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(6), 0, dp(6))
        }
        val value = GameAudio.getInt(this, key).coerceIn(0, max)
        val tv = label("$title: $value$unit", Color.WHITE, 15f)
        box.addView(tv)
        if (hint != null) box.addView(label(hint, dim, 12f))
        box.addView(SeekBar(this).apply {
            this.max = max
            progress = value
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                    tv.text = "$title: $p$unit"
                    if (fromUser) {
                        GameAudio.setInt(this@AudioSettingsActivity, key, p)
                    }
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) {
                    GameAudio.onConfigChanged(this@AudioSettingsActivity)
                }
            })
        })
        return box
    }

    private fun songRow(name: String): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        row.addView(
            label(GameAudio.songLabel(name), Color.WHITE, 15f),
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )
        row.addView(toggleButton(
            { GameAudio.isSongEnabled(this@AudioSettingsActivity, name) },
            { GameAudio.setSongEnabled(this@AudioSettingsActivity, name, it) }
        ))
        val btn = Button(this).apply {
            text = "Play"
            setOnClickListener { togglePreview(name) }
        }
        previewButtons[name] = btn
        row.addView(btn)
        if (GameAudio.isUserSong(name)) {
            row.addView(Button(this).apply {
                text = "Delete"
                setOnClickListener {
                    AlertDialog.Builder(this@AudioSettingsActivity)
                        .setTitle("Delete song?")
                        .setMessage(GameAudio.songLabel(name))
                        .setPositiveButton("Delete") { _, _ ->
                            GameAudio.stopPreview()
                            previewing = null
                            GameAudio.deleteUserSong(this@AudioSettingsActivity, name)
                            buildContent()
                        }
                        .setNegativeButton("Cancel", null)
                        .show()
                }
            })
        }
        return row
    }

    private fun togglePreview(name: String) {
        if (previewing == name) {
            GameAudio.stopPreview()
            previewing = null
        } else {
            GameAudio.stopPreview()
            previewing = name
            GameAudio.previewSong(this, name) {
                if (previewing == name) previewing = null
                refreshPreviewButtons()
            }
        }
        refreshPreviewButtons()
    }

    private fun refreshPreviewButtons() {
        for ((n, b) in previewButtons) b.text = if (n == previewing) "Stop" else "Play"
    }

    private fun sfxCard(key: String, title: String, hint: String): View {
        val c = card()
        val available = GameAudio.hasSfx(this, key)
        c.addView(switchRow(title, key + "_on"))
        c.addView(label(if (available) hint else "$hint  (file missing)", if (available) dim else Color.parseColor("#E8A04C"), 12f))
        c.addView(sliderRow("Volume", key + "_vol", 300, "%", null))
        c.addView(Button(this).apply {
            text = "Test"
            isEnabled = available
            setOnClickListener { GameAudio.previewSfx(this@AudioSettingsActivity, key) }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return c
    }

    // ------------------------------------------------------------ small helpers

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun label(text: String, color: Int, sp: Float): TextView = TextView(this).apply {
        this.text = text
        setTextColor(color)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
    }

    private fun note(text: String): TextView = label(text, dim, 13f).apply {
        setPadding(0, dp(8), 0, dp(4))
    }

    private fun sectionTitle(text: String): TextView = label(text, accent, 17f).apply {
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(18), 0, dp(6))
    }

    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(14), dp(10), dp(14), dp(12))
        background = GradientDrawable().apply {
            setColor(cardColor)
            cornerRadius = dp(12).toFloat()
        }
        layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { bottomMargin = dp(10) }
    }
}
