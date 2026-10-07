package com.vlither

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import android.graphics.Typeface
import android.os.SystemClock
import android.util.AttributeSet
import android.view.View

/**
 * Neon "Vlither android" logo, drawn with Canvas (no image assets), one line:
 *  1. outline of "Vlither" draws itself (stroke draw-on)
 *  2. outline fades, green fill fades in
 *  3. "android" types out to the right with a blinking cursor
 */
class AnimatedLogoView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val green = 0xFF2BAA60.toInt()
    private val mint = 0xFF9BFFC4.toInt()

    private val word = "Vlither"
    private val sub = "android"

    private val density = resources.displayMetrics.density

    private val wordPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.LEFT
    }
    private val subPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create("sans-serif-light", Typeface.NORMAL)
        color = mint
        textAlign = Paint.Align.LEFT
    }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = mint
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = green
    }

    // Cached geometry (rebuilt when size changes)
    private val wordPath = Path()
    private val contours = ArrayList<PathMeasure>()
    private val seg = Path()
    private var wordW = 0f
    private var subW = 0f
    private var subSpacing = 0f
    private var gap = 0f
    private var originX = 0f
    private var baseY = 0f
    private var subSize = 0f

    private var startMs = 0L

    // Timeline (ms)
    private val drawEnd = 2000f
    private val fillStart = 1900f
    private val fillDur = 800f
    private val typeStart = 2300f
    private val typeStep = 110f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = if (MeasureSpec.getMode(widthMeasureSpec) == MeasureSpec.UNSPECIFIED)
            (320 * density).toInt() else MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, (90 * density).toInt())
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w <= 0) return
        // Fit "Vlither" + gap + "android" on ONE line within the view width.
        var size = 48f * density
        fun layout(s: Float) {
            wordPaint.textSize = s
            subSize = s * 0.28f
            subPaint.textSize = subSize
            subSpacing = subSize * 0.4f
            gap = s * 0.18f
            wordW = wordPaint.measureText(word)
            subW = sub.sumOf { subPaint.measureText(it.toString()).toDouble() }.toFloat() +
                subSpacing * (sub.length - 1)
        }
        layout(size)
        val avail = w * 0.96f
        val total = wordW + gap + subW
        if (total > avail) {
            size *= avail / total
            layout(size)
        }
        val tot = wordW + gap + subW
        originX = (w - tot) / 2f
        baseY = h * 0.62f

        wordPath.reset()
        wordPaint.getTextPath(word, 0, word.length, originX, baseY, wordPath)
        contours.clear()
        val pm = PathMeasure(wordPath, false)
        // Split into one PathMeasure per contour
        do {
            val p = Path()
            pm.getSegment(0f, pm.length, p, true)
            contours.add(PathMeasure(p, false))
        } while (pm.nextContour())
        strokePaint.strokeWidth = 2f * density
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        startMs = SystemClock.uptimeMillis()
        postInvalidateOnAnimation()
    }

    private fun clamp01(v: Float) = if (v < 0f) 0f else if (v > 1f) 1f else v

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val t = (SystemClock.uptimeMillis() - startMs).toFloat()

        // 1+2: outline draw-on (ease-out), then fade to fill
        val dp = clamp01(t / drawEnd)
        val ease = 1f - (1f - dp) * (1f - dp)
        val fp = clamp01((t - fillStart) / fillDur)

        if (fp > 0f) {
            fillPaint.alpha = (255 * fp).toInt()
            canvas.drawPath(wordPath, fillPaint)
        }
        if (fp < 1f) {
            strokePaint.alpha = (255 * (1f - fp)).toInt()
            for (pm in contours) {
                seg.reset()
                pm.getSegment(0f, pm.length * ease, seg, true)
                canvas.drawPath(seg, strokePaint)
            }
        }

        // 3: "android" typed to the right of "Vlither", same baseline
        if (t >= typeStart) {
            val shown = minOf(sub.length, ((t - typeStart) / typeStep).toInt() + 1)
            val done = shown >= sub.length && (t - typeStart) / typeStep >= sub.length
            var x = originX + wordW + gap
            for (i in 0 until shown) {
                val ch = sub[i].toString()
                canvas.drawText(ch, x, baseY, subPaint)
                x += subPaint.measureText(ch) + subSpacing
            }
            // cursor: solid while typing, then blink
            val cursorOn = if (!done) true else (((t - typeStart) / 400f).toInt() % 2 == 0)
            if (cursorOn) {
                canvas.drawRect(x, baseY - subSize * 0.12f, x + subSize * 0.55f, baseY, subPaint)
            }
        }

        postInvalidateOnAnimation()
    }
}
