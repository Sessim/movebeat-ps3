package io.movebeat.assistant

import java.io.File

fun main(args: Array<String>) {
    require(args.size == 1) { "usage: CardCodecSmoke <output.mblc>" }
    val pixels = IntArray(LibraryCardCodec.WIDTH * LibraryCardCodec.HEIGHT) { index ->
        val red = (index * 17) and 255
        val green = (index * 31) and 255
        val blue = (index * 47) and 255
        (0x80 shl 24) or (red shl 16) or (green shl 8) or blue
    }
    File(args[0]).writeBytes(LibraryCardCodec.encodeArgb8888(pixels))
}
