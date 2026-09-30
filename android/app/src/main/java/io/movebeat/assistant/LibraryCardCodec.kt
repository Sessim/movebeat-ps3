package io.movebeat.assistant

/** MBLC v1: 512×96 big-endian RGB565 pixels after an eight-byte header. */
internal object LibraryCardCodec {
    const val WIDTH = 512
    const val HEIGHT = 96
    private val header = byteArrayOf(77, 66, 76, 67, 2, 0, 0, 96)

    fun encodeArgb8888(pixels: IntArray): ByteArray {
        require(pixels.size == WIDTH * HEIGHT) { "Library card has wrong pixel count" }
        val output = ByteArray(header.size + pixels.size * 2)
        header.copyInto(output)
        for (index in pixels.indices) {
            val color = pixels[index]
            val rgb565 = (((color ushr 16) and 255) ushr 3 shl 11) or
                ((((color ushr 8) and 255) ushr 2) shl 5) or
                ((color and 255) ushr 3)
            output[8 + index * 2] = (rgb565 ushr 8).toByte()
            output[9 + index * 2] = rgb565.toByte()
        }
        return output
    }
}
