package io.movebeat.assistant

import java.io.File

fun main(args: Array<String>) {
    require(args.size in 2..4) {
        "usage: BundleSmoke <source.zip> <bundle.zip> [key] [mapper]"
    }
    val result = BundleConverter.convert(File(args[0]).readBytes(),
        if (args.size >= 3) args[2] else "", null,
        if (args.size == 4) args[3] else null)
    File(args[1]).writeBytes(result.bytes)
    println("title=${result.title}")
    println("charts=${result.charts}")
    println("bytes=${result.bytes.size}")
}
