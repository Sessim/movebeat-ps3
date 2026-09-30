package io.movebeat.assistant

/** Used only by the JVM parity test; the APK compiles AndroidCard.kt instead. */
internal object AndroidCard {
    fun render(cover: ByteArray?, title: String, author: String, mapper: String): ByteArray {
        return LibraryCardCodec.encodeArgb8888(IntArray(512 * 96))
    }
}
