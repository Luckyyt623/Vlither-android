package com.vlither

import android.content.Context
import android.content.SharedPreferences
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.random.Random

/**
 * In-game audio, arranged like the original slither Android (SWF) game.
 * Music and sound effects are OFF by default; everything is configured from the
 * "Sound/Song management" screen (AudioSettingsActivity). Settings live in
 * SharedPreferences "vlither_audio".
 *
 * Folder: android/app/src/main/assets/
 *   songs/    1.mp3 .. 8.mp3  (+ gs_old.mp3, any other song works too)
 *   foods_2/  1.mp3 .. 80.mp3   eat sounds (pitch follows food size / eating streak)
 *   foods_1/  1.mp3 .. 11.mp3   old eat sounds (only used if foods_2 is missing)
 *   bloops/   0.mp3 .. 15.mp3   dead-snake sounds (small snake = high index)
 *   sfx/      boost.wav (loop), die.*, kill.*   (ogg / mp3 / wav)
 *
 * Game events arrive from native code through GameActivity.onAudioEvent().
 * Everything here runs on the main thread.
 */
object GameAudio {
    private const val TAG = "GameAudio"
    private const val PREFS = "vlither_audio"

    const val EV_EAT = 0
    const val EV_DIE = 1
    const val EV_KILL = 2
    const val EV_GAME_START = 3
    const val EV_GAME_STOP = 4
    const val EV_BOOST_ON = 5
    const val EV_BOOST_OFF = 6
    const val EV_SNAKE_DEAD = 7

    // preference keys ("<sfx>_on" / "<sfx>_vol" for each effect below)
    const val K_MUSIC_ON = "music_on"
    const val K_SFX_ON = "sfx_on"
    const val K_MUSIC_VOL = "music_vol"
    const val K_MUSIC_DELAY = "music_delay"
    const val SFX_EAT = "eat"
    const val SFX_DEAD = "dead"
    const val SFX_BOOST = "boost"
    const val SFX_DIE = "die"
    const val SFX_KILL = "kill"
    private const val K_SONGS_OFF = "songs_off"

    private val ALL_KEYS = arrayOf(
        K_MUSIC_ON, K_SFX_ON, K_MUSIC_VOL, K_MUSIC_DELAY,
        "eat_on", "eat_vol", "dead_on", "dead_vol", "boost_on", "boost_vol",
        "die_on", "die_vol", "kill_on", "kill_vol", K_SONGS_OFF
    )

    private const val MUSIC_GAP_MS = 0L
    private const val SONG_VOL_DEFAULT = 0.4f
    private const val SONG_VOL_OLD = 0.65f
    private val SONG_VOL = mapOf(3 to 0.31f, 5 to 0.35f)

    // Set to true only for debugging (shows what got loaded when a game starts).
    private const val DEBUG_TOAST = false

    private const val EAT_VOL_NEW = 0.18f
    private const val EAT_VOL_OLD = 0.16f
    private const val BOOST_QUIET = 0.001f
    private const val BOOST_LOUD = 0.115f
    private const val BOOST_UP_MS = 1500f
    private const val BOOST_DOWN_MS = 580f

    private val main = Handler(Looper.getMainLooper())
    private var app: Context? = null
    private var pool: SoundPool? = null
    private val named = HashMap<String, Int>()
    private var foodsNew = IntArray(0)
    private var foodsOld = IntArray(0)
    private var bloops = IntArray(0)

    private var songs: List<String> = emptyList()
    private var songsDir = "songs"
    private var songPlayed = LongArray(0)
    private var songIdx = -1
    private var currentSong: String? = null
    private var lastDurSec = 0f
    private var player: MediaPlayer? = null
    private var preview: MediaPlayer? = null

    private var inGame = false
    private var paused = false

    private var fk = 0f
    private var fkAt = 0L
    private var boostStream = 0
    private var bsV = 0f
    private var bsTarget = 0f

    private val startMusic = Runnable { playNextSong() }
    private val boostTick = object : Runnable {
        override fun run() {
            val step = 16f
            if (bsV < bsTarget) bsV = min(bsTarget, bsV + step / BOOST_UP_MS)
            else if (bsV > bsTarget) bsV = max(bsTarget, bsV - step / BOOST_DOWN_MS)
            applyBoostVolume()
            if (bsV != bsTarget) main.postDelayed(this, 16)
        }
    }

    // ------------------------------------------------------------ settings (prefs)

    private fun prefsOf(ctx: Context): SharedPreferences =
        ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun defBool(key: String): Boolean = !(key == K_MUSIC_ON || key == K_SFX_ON)
    private fun defInt(key: String): Int = if (key == K_MUSIC_DELAY) 0 else 100

    @JvmStatic fun getBool(ctx: Context, key: String): Boolean =
        prefsOf(ctx).getBoolean(key, defBool(key))

    @JvmStatic fun getInt(ctx: Context, key: String): Int =
        prefsOf(ctx).getInt(key, defInt(key))

    @JvmStatic fun setBool(ctx: Context, key: String, v: Boolean) {
        prefsOf(ctx).edit().putBoolean(key, v).apply()
    }

    @JvmStatic fun setInt(ctx: Context, key: String, v: Int) {
        prefsOf(ctx).edit().putInt(key, v).apply()
    }

    private fun b(key: String): Boolean {
        val c = app ?: return defBool(key)
        return getBool(c, key)
    }

    private fun i(key: String): Int {
        val c = app ?: return defInt(key)
        return getInt(c, key)
    }

    private fun scale(key: String): Float = i(key + "_vol") / 100f
    private fun sfxEnabled(key: String): Boolean = b(K_SFX_ON) && b(key + "_on")

    private fun songsOff(ctx: Context): Set<String> =
        HashSet(prefsOf(ctx).getStringSet(K_SONGS_OFF, emptySet()) ?: emptySet())

    @JvmStatic fun isSongEnabled(ctx: Context, name: String): Boolean = name !in songsOff(ctx)

    @JvmStatic fun setSongEnabled(ctx: Context, name: String, enabled: Boolean) {
        val s = HashSet(songsOff(ctx))
        if (enabled) s.remove(name) else s.add(name)
        prefsOf(ctx).edit().putStringSet(K_SONGS_OFF, s).apply()
    }

    /** Back to defaults: music and sound effects OFF, volumes 100%, all songs enabled. */
    @JvmStatic fun resetConfig(ctx: Context) {
        val e = prefsOf(ctx).edit()
        for (k in ALL_KEYS) e.remove(k)
        e.apply()
        onConfigChanged(ctx)
    }

    /** Call after any setting changed so a running game picks it up. */
    @JvmStatic fun onConfigChanged(ctx: Context) {
        main.post {
            ensureInit(ctx)
            if (!b(K_MUSIC_ON)) {
                main.removeCallbacks(startMusic)
                stopPlayer()
            } else {
                val mp = player
                val name = currentSong
                if (mp != null && name != null) {
                    val v = songVolume(name)
                    try { mp.setVolume(v, v) } catch (_: Exception) {}
                } else if (inGame && !paused) {
                    scheduleMusic(0L)
                }
            }
            if (!sfxEnabled(SFX_BOOST)) stopBoostLoop()
            else if (inGame && boostStream == 0) startBoostLoop()
            else applyBoostVolume()
        }
    }

    // ------------------------------------------------------------ settings screen helpers

    @JvmStatic fun prepare(ctx: Context) = ensureInit(ctx)

    // ------------------------------------------------------------ uploaded songs
    // Songs picked from phone storage are copied into the app's private folder
    // (filesDir/songs) and show up in the list as "user:<file name>".

    const val USER_PREFIX = "user:"
    private const val MAX_USER_SONG_BYTES = 60L * 1024 * 1024

    private fun userDir(c: Context): File = File(c.filesDir, "songs")

    private fun assetSongs(c: Context): List<String> =
        (c.assets.list(songsDir) ?: emptyArray()).filter { isAudio(it) }
            .sortedWith(compareBy({ songNumber(it) ?: Int.MAX_VALUE }, { it }))

    private fun userSongs(c: Context): List<String> =
        (userDir(c).listFiles() ?: emptyArray())
            .filter { it.isFile && isAudio(it.name) }
            .map { it.name }.sorted().map { USER_PREFIX + it }

    @JvmStatic fun isUserSong(name: String): Boolean = name.startsWith(USER_PREFIX)

    @JvmStatic fun songLabel(name: String): String =
        if (isUserSong(name)) name.removePrefix(USER_PREFIX) else name

    /** Rebuilds the song list after an upload or delete. */
    @JvmStatic fun reloadSongs(ctx: Context) {
        ensureInit(ctx)
        val c = app ?: return
        songs = assetSongs(c) + userSongs(c)
        songPlayed = LongArray(songs.size)
        songIdx = -1
    }

    private fun displayName(c: Context, uri: Uri): String? = try {
        c.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    } catch (e: Exception) { null }

    /** Copies the picked audio file into the game. Returns null on success, else an error text. */
    @JvmStatic fun importSong(ctx: Context, uri: Uri): String? {
        val c = ctx.applicationContext
        ensureInit(c)
        var name = (displayName(c, uri) ?: uri.lastPathSegment ?: "song")
            .substringAfterLast('/')
            .replace(Regex("[^A-Za-z0-9._ -]"), "_")
        if (!isAudio(name)) return "Unsupported file: use mp3, ogg, m4a or wav"
        val dir = userDir(c)
        if (!dir.exists() && !dir.mkdirs()) return "Could not create the song folder"
        val base = name.substringBeforeLast('.')
        val ext = name.substringAfterLast('.')
        var out = File(dir, name)
        var n = 1
        while (out.exists()) { out = File(dir, "${base}_$n.$ext"); n++ }
        return try {
            var total = 0L
            c.contentResolver.openInputStream(uri)?.use { input ->
                out.outputStream().use { output ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val r = input.read(buf)
                        if (r < 0) break
                        total += r
                        if (total > MAX_USER_SONG_BYTES) throw java.io.IOException("too big")
                        output.write(buf, 0, r)
                    }
                }
            } ?: return "Could not open the file"
            reloadSongs(c)
            null
        } catch (e: Exception) {
            out.delete()
            if (e.message == "too big") "Song is too big (max 60 MB)" else "Could not copy the song"
        }
    }

    @JvmStatic fun deleteUserSong(ctx: Context, name: String) {
        if (!isUserSong(name)) return
        val c = ctx.applicationContext
        File(userDir(c), name.removePrefix(USER_PREFIX)).delete()
        val s = HashSet(songsOff(c)); s.remove(name)
        prefsOf(c).edit().putStringSet(K_SONGS_OFF, s).apply()
        reloadSongs(c)
    }

    /** Points a MediaPlayer at a bundled or uploaded song. */
    private fun setSongSource(mp: MediaPlayer, c: Context, name: String) {
        if (isUserSong(name)) {
            mp.setDataSource(File(userDir(c), name.removePrefix(USER_PREFIX)).absolutePath)
        } else {
            c.applicationContext.assets.openFd("$songsDir/$name").use { afd ->
                mp.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
            }
        }
    }

    @JvmStatic fun listSongs(ctx: Context): List<String> {
        ensureInit(ctx)
        return songs
    }

    @JvmStatic fun hasSfx(ctx: Context, key: String): Boolean {
        ensureInit(ctx)
        return when (key) {
            SFX_EAT -> foodsNew.isNotEmpty() || foodsOld.isNotEmpty()
            SFX_DEAD -> bloops.isNotEmpty()
            else -> named.containsKey(key)
        }
    }

    @JvmStatic fun previewSong(ctx: Context, name: String, onDone: (() -> Unit)?) {
        ensureInit(ctx)
        stopPreview()
        try {
            val mp = MediaPlayer()
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            setSongSource(mp, ctx, name)
            val v = (songVolume(name)).coerceIn(0f, 1f)
            mp.setVolume(v, v)
            mp.setOnPreparedListener { it.start() }
            mp.setOnCompletionListener { stopPreview(); onDone?.invoke() }
            mp.setOnErrorListener { _, _, _ -> stopPreview(); onDone?.invoke(); true }
            preview = mp
            mp.prepareAsync()
        } catch (e: Exception) {
            Log.w(TAG, "preview failed: $name", e)
            stopPreview()
            onDone?.invoke()
        }
    }

    @JvmStatic fun stopPreview() {
        val mp = preview ?: return
        preview = null
        try { mp.setOnCompletionListener(null); mp.setOnErrorListener(null) } catch (_: Exception) {}
        try { mp.stop() } catch (_: Exception) {}
        try { mp.release() } catch (_: Exception) {}
    }

    @JvmStatic fun previewSfx(ctx: Context, key: String) {
        ensureInit(ctx)
        val s = scale(key)
        when (key) {
            SFX_EAT -> {
                if (foodsNew.isNotEmpty()) {
                    val n = foodsNew.size
                    play(foodsNew[Random.nextInt(n / 4, max(n / 4 + 1, n * 3 / 4))], EAT_VOL_NEW * s)
                } else if (foodsOld.isNotEmpty()) {
                    play(foodsOld[Random.nextInt(foodsOld.size)], EAT_VOL_OLD * s)
                }
            }
            SFX_DEAD -> if (bloops.isNotEmpty()) play(bloops[Random.nextInt(bloops.size)], s)
            SFX_BOOST -> named["boost"]?.let { play(it, BOOST_LOUD * s + BOOST_QUIET) }
            else -> named[key]?.let { play(it, s) }
        }
    }

    // ------------------------------------------------------------ game API

    @JvmStatic
    fun event(ctx: Context, ev: Int, arg: Int) {
        main.post { handle(ctx, ev, arg) }
    }

    @JvmStatic
    fun pause() {
        main.post {
            paused = true
            main.removeCallbacks(startMusic)
            try { player?.pause() } catch (_: Exception) {}
            try { pool?.autoPause() } catch (_: Exception) {}
        }
    }

    @JvmStatic
    fun resume() {
        main.post {
            paused = false
            try { pool?.autoResume() } catch (_: Exception) {}
            if (!inGame || !b(K_MUSIC_ON)) return@post
            val mp = player
            if (mp == null) {
                scheduleMusic(1_000L)
            } else {
                try { mp.start() } catch (_: Exception) {
                    stopPlayer()
                    scheduleMusic(1_000L)
                }
            }
        }
    }

    @JvmStatic
    fun release() {
        main.post {
            stopGame()
            stopPreview()
            pool?.release()
            pool = null
            named.clear()
            foodsNew = IntArray(0); foodsOld = IntArray(0); bloops = IntArray(0)
            app = null
        }
    }

    // ------------------------------------------------------------ events

    private fun handle(ctx: Context, ev: Int, arg: Int) {
        ensureInit(ctx)
        when (ev) {
            EV_EAT -> if (sfxEnabled(SFX_EAT)) onEat(arg)
            EV_SNAKE_DEAD -> if (sfxEnabled(SFX_DEAD)) onSnakeDead(arg)
            EV_KILL -> if (sfxEnabled(SFX_KILL)) playNamed("kill", scale(SFX_KILL))
            EV_DIE -> { if (sfxEnabled(SFX_DIE)) playNamed("die", scale(SFX_DIE)); stopGame() }
            EV_GAME_START -> startGame()
            EV_GAME_STOP -> stopGame()
            EV_BOOST_ON -> setBoost(true)
            EV_BOOST_OFF -> setBoost(false)
        }
    }

    private fun ensureInit(ctx: Context) {
        if (app != null) return
        val c = ctx.applicationContext
        app = c

        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        try {
            val sp = SoundPool.Builder().setMaxStreams(10).setAudioAttributes(attrs).build()
            pool = sp
            val foods2 = findDir(c, arrayOf("foods_2"))
            val foods1 = findDir(c, arrayOf("foods_1"))
            val bloopDir = findDir(c, arrayOf("bloops"))
            val sfxDir = findDir(c, arrayOf("sfx"))
            foodsNew = if (foods2 != null) loadIndexed(c, sp, foods2, 1) else IntArray(0)
            foodsOld = if (foods1 != null) loadIndexed(c, sp, foods1, 1) else IntArray(0)
            bloops = if (bloopDir != null) loadIndexed(c, sp, bloopDir, 0) else IntArray(0)
            loadNamed(c, sp, sfxDir, "boost", arrayOf("wav", "ogg", "mp3"))
            loadNamed(c, sp, sfxDir, "die", arrayOf("ogg", "mp3", "wav"))
            loadNamed(c, sp, sfxDir, "kill", arrayOf("ogg", "mp3", "wav"))

            songsDir = findDir(c, arrayOf("songs", "music")) ?: "songs"
            songs = assetSongs(c) + userSongs(c)
            songPlayed = LongArray(songs.size)
        } catch (e: Exception) {
            Log.e(TAG, "init failed", e)
            toast("Audio init failed: ${e.javaClass.simpleName} ${e.message}")
        }
        Log.d(TAG, status())
    }

    private fun status(): String =
        "Audio: songs=${songs.size} foods_2=${foodsNew.size} foods_1=${foodsOld.size} " +
            "bloops=${bloops.size} boost=${named.containsKey("boost")}"

    private fun toast(msg: String) {
        if (!DEBUG_TOAST) return
        val c = app ?: return
        try { Toast.makeText(c, msg, Toast.LENGTH_LONG).show() } catch (_: Exception) {}
    }

    private fun hasAudio(c: Context, path: String): Boolean = try {
        (c.assets.list(path) ?: emptyArray()).any { isAudio(it) }
    } catch (e: Exception) {
        false
    }

    /** Finds a sound folder even if it was pasted nested (assets/assets/songs, assets/sounds/songs ...). */
    private fun findDir(c: Context, names: Array<String>): String? {
        for (n in names) if (hasAudio(c, n)) return n
        val top = try { c.assets.list("") } catch (e: Exception) { null } ?: return null
        for (a in top) {
            if (a.contains('.')) continue
            for (n in names) if (hasAudio(c, "$a/$n")) return "$a/$n"
            val sub = try { c.assets.list(a) } catch (e: Exception) { null } ?: continue
            for (b in sub) {
                if (b.contains('.')) continue
                for (n in names) if (hasAudio(c, "$a/$b/$n")) return "$a/$b/$n"
            }
        }
        return null
    }

    // ------------------------------------------------------------ loading

    private fun isAudio(name: String): Boolean {
        val n = name.lowercase()
        return n.endsWith(".mp3") || n.endsWith(".ogg") || n.endsWith(".m4a") || n.endsWith(".wav")
    }

    private fun songNumber(name: String): Int? = name.substringBeforeLast('.', "").toIntOrNull()

    private fun loadIndexed(c: Context, sp: SoundPool, dir: String, first: Int): IntArray {
        val names = try { c.assets.list(dir) } catch (e: Exception) { null } ?: return IntArray(0)
        val last = names.filter { isAudio(it) }
            .mapNotNull { songNumber(it) }.filter { it >= first }.maxOrNull() ?: return IntArray(0)
        val ids = IntArray(last - first + 1)
        for (n in names) {
            if (!isAudio(n)) continue
            val num = songNumber(n) ?: continue
            if (num < first) continue
            try {
                val sid = c.assets.openFd("$dir/$n").use { afd -> sp.load(afd, 1) }
                ids[num - first] = sid
            } catch (_: Exception) {
            }
        }
        return ids
    }

    private fun loadNamed(c: Context, sp: SoundPool, dir: String?, key: String, exts: Array<String>) {
        for (ext in exts) {
            val path = if (dir == null) "$key.$ext" else "$dir/$key.$ext"
            try {
                val sid = c.assets.openFd(path).use { afd -> sp.load(afd, 1) }
                named[key] = sid
                return
            } catch (_: Exception) {
            }
        }
    }

    private fun play(id: Int, vol: Float) {
        if (id == 0) return
        val v = vol.coerceIn(0f, 1f)
        pool?.play(id, v, v, 1, 0, 1f)
    }

    private fun playNamed(key: String, vol: Float) {
        val id = named[key] ?: return
        play(id, vol)
    }

    // ------------------------------------------------------------ eat / death sounds

    /** arg = 0..1000 food size factor (original: 0.15 + 0.85 * normalised size). */
    private fun onEat(arg: Int) {
        val s = scale(SFX_EAT)
        val t = (arg / 1000f).coerceIn(0f, 1f)
        if (foodsNew.isNotEmpty()) {
            val n = foodsNew.size
            val now = SystemClock.elapsedRealtime()
            if (fk > 0f) fk *= Math.pow(0.98, (now - fkAt) / 16.7).toFloat()
            fkAt = now
            fk += (n - 1 - fk) * 0.01f * t
            fk += 0.5f * t
            if (fk >= n) fk = (n - 1).toFloat()
            val idx = (t * n * 0.1f + fk + Random.nextFloat() * 9f - 5f).toInt().coerceIn(0, n - 1)
            play(foodsNew[idx], EAT_VOL_NEW * s)
        } else if (foodsOld.isNotEmpty()) {
            val n = foodsOld.size
            val tt = ((t - 0.15f) / 0.85f).coerceIn(0f, 1f)
            var idx = (n - ceil(n * tt).toInt()).coerceIn(0, n - 1)
            val r = Random.nextFloat()
            val half = n / 2f
            if (r < 0.08f) idx += if (idx < half) 2 else -2
            else if (r < 0.4f) idx += if (idx < half) 1 else -1
            play(foodsOld[idx.coerceIn(0, n - 1)], EAT_VOL_OLD * s)
        }
    }

    /** arg = 0..1000 snake length / max length. Small snake = high bloop index. */
    private fun onSnakeDead(arg: Int) {
        val n = bloops.size
        if (n == 0) return
        val frac = (arg / 1000f).coerceIn(0f, 1f)
        val idx = (n - ceil(n * frac).toInt()).coerceIn(0, n - 1)
        play(bloops[idx], scale(SFX_DEAD))
    }

    // ------------------------------------------------------------ boost loop

    private fun applyBoostVolume() {
        if (boostStream == 0) return
        val v = ((bsV - BOOST_QUIET) * BOOST_LOUD * scale(SFX_BOOST) + BOOST_QUIET).coerceIn(0f, 1f)
        pool?.setVolume(boostStream, v, v)
    }

    private fun startBoostLoop() {
        if (!sfxEnabled(SFX_BOOST)) return
        val id = named["boost"] ?: return
        stopBoostLoop()
        boostStream = pool?.play(id, BOOST_QUIET, BOOST_QUIET, 1, -1, 1f) ?: 0
    }

    private fun stopBoostLoop() {
        main.removeCallbacks(boostTick)
        if (boostStream != 0) pool?.stop(boostStream)
        boostStream = 0
        bsV = 0f
        bsTarget = 0f
    }

    private fun setBoost(on: Boolean) {
        if (!inGame || !sfxEnabled(SFX_BOOST)) return
        if (boostStream == 0) startBoostLoop()
        bsTarget = if (on) 1f else 0f
        main.removeCallbacks(boostTick)
        main.post(boostTick)
    }

    // ------------------------------------------------------------ game / music

    private fun startGame() {
        if (inGame) return
        inGame = true
        fk = 0f
        toast(status())
        startBoostLoop()
        if (b(K_MUSIC_ON)) {
            val maxMs = i(K_MUSIC_DELAY) * 1000L
            scheduleMusic(if (maxMs > 0L) Random.nextLong(0L, maxMs + 1) else 0L)
        }
    }

    private fun stopGame() {
        inGame = false
        main.removeCallbacks(startMusic)
        stopPlayer()
        stopBoostLoop()
    }

    private fun scheduleMusic(delayMs: Long) {
        main.removeCallbacks(startMusic)
        if (delayMs <= 0L) main.post(startMusic) else main.postDelayed(startMusic, delayMs)
    }

    private fun songVolume(name: String): Float {
        val base = name.substringBeforeLast('.', name)
        val b0 = if (base == "gs_old") SONG_VOL_OLD
        else songNumber(name)?.let { SONG_VOL[it] } ?: SONG_VOL_DEFAULT
        return (b0 * i(K_MUSIC_VOL) / 100f).coerceIn(0f, 1f)
    }

    /** Same picking idea as the original (only among songs that are enabled): after a long
     *  listen go to the next song or a random one not played in the last 10 min; after a
     *  short one (quick death) repeat it. Returns -1 if no song is enabled. */
    private fun pickSong(): Int {
        val off = app?.let { songsOff(it) } ?: emptySet()
        val cand = songs.indices.filter { songs[it] !in off }
        if (cand.isEmpty()) return -1
        val m = cand.size
        if (m == 1) return cand[0]
        val now = SystemClock.elapsedRealtime()
        var pos = cand.indexOf(songIdx)
        if (pos < 0) {
            pos = Random.nextInt(m)
        } else if (lastDurSec > 10f) {
            if (Random.nextBoolean()) {
                pos = (pos + 1) % m
            } else {
                val prev = pos
                var c = Random.nextInt(m)
                pos = c
                for (k in 0 until m) {
                    c = (c + 1) % m
                    val last = songPlayed[cand[c]]
                    if (c != prev && (last == 0L || now - last > 600_000L)) {
                        pos = c
                        break
                    }
                }
                if (pos == prev) pos = (pos + 1) % m
            }
        }
        return cand[pos]
    }

    private fun playNextSong() {
        if (!inGame || paused || !b(K_MUSIC_ON)) return
        val c = app ?: return
        val idx = pickSong()
        if (idx < 0) return
        songIdx = idx
        songPlayed[idx] = SystemClock.elapsedRealtime()
        val name = songs[idx]
        stopPlayer()
        try {
            val mp = MediaPlayer()
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            setSongSource(mp, c, name)
            val v = songVolume(name)
            mp.setVolume(v, v)
            mp.setOnPreparedListener { if (!paused && inGame && b(K_MUSIC_ON)) it.start() }
            mp.setOnCompletionListener {
                stopPlayer()
                if (inGame) scheduleMusic(MUSIC_GAP_MS)
            }
            mp.setOnErrorListener { _, what, extra ->
                toast("Song error $name what=$what extra=$extra")
                stopPlayer()
                if (inGame) scheduleMusic(5_000L)
                true
            }
            player = mp
            currentSong = name
            mp.prepareAsync()
        } catch (e: Exception) {
            Log.w(TAG, "song failed: $name", e)
            toast("Song failed: $name ${e.message}")
            stopPlayer()
            if (inGame) scheduleMusic(5_000L)
        }
    }

    private fun stopPlayer() {
        val mp = player ?: return
        player = null
        currentSong = null
        try { lastDurSec = mp.currentPosition / 1000f } catch (_: Exception) {}
        try { mp.setOnCompletionListener(null); mp.setOnErrorListener(null) } catch (_: Exception) {}
        try { mp.stop() } catch (_: Exception) {}
        try { mp.release() } catch (_: Exception) {}
    }
}
