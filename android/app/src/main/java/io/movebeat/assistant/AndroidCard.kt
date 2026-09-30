package io.movebeat.assistant

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import kotlin.math.min

/** Android-only renderer for the 512x96 big-endian RGB565 PS3 library card. */
internal object AndroidCard {
    fun render(cover: ByteArray?, title: String, author: String, mapper: String): ByteArray {
        val bitmap = Bitmap.createBitmap(512, 96, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(bitmap)
            canvas.drawColor(Color.rgb(14, 16, 29))
            val paint = Paint(Paint.ANTI_ALIAS_FLAG)
            if (cover != null) {
                val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(cover, 0, cover.size, options)
                require(options.outWidth > 0 && options.outHeight > 0 &&
                    options.outWidth.toLong() * options.outHeight <= 16_000_000L) {
                    "Cover cannot be decoded safely"
                }
                options.inJustDecodeBounds = false
                options.inSampleSize = maxOf(1, min(options.outWidth, options.outHeight) / 192)
                val source = BitmapFactory.decodeByteArray(cover, 0, cover.size, options)
                    ?: error("Cover cannot be decoded")
                try {
                    val side = min(source.width, source.height)
                    val left = (source.width - side) / 2
                    val top = (source.height - side) / 2
                    canvas.drawBitmap(source, Rect(left, top, left + side, top + side),
                        Rect(0, 0, 96, 96), paint)
                } finally {
                    source.recycle()
                }
            } else {
                paint.color = Color.rgb(28, 47, 78)
                canvas.drawRect(0f, 0f, 96f, 96f, paint)
            }
            paint.color = Color.rgb(39, 51, 81)
            canvas.drawRect(96f, 0f, 101f, 96f, paint)
            fun label(value: String, baseline: Float, size: Float, color: Int) {
                paint.color = color
                paint.textSize = size
                var display = value.replace(Regex("[\\r\\n\\x00]"), " ").trim()
                while (display.isNotEmpty() && paint.measureText(display) > 390f)
                    display = display.dropLast(1)
                canvas.drawText(display, 114f, baseline, paint)
            }
            label(title, 32f, 21f, Color.rgb(242, 246, 255))
            label(author, 59f, 15f, Color.rgb(150, 179, 211))
            if (mapper.isNotBlank())
                label("Mapper: $mapper", 83f, 12f, Color.rgb(126, 145, 180))
            val pixels = IntArray(512 * 96)
            bitmap.getPixels(pixels, 0, 512, 0, 0, 512, 96)
            return LibraryCardCodec.encodeArgb8888(pixels)
        } finally {
            bitmap.recycle()
        }
    }
}
