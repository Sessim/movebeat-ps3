package io.movebeat.assistant

import java.io.File

fun main(args: Array<String>) {
    require(args.size == 2) { "usage: VerifierSmoke <map.zip> <expected-content-sha1>" }
    val summary = BeatSaverArchive.inspect(File(args[0]).readBytes(), args[1])
    println("ANDROID_KOTLIN_HASH=PASS")
    println("title=${summary.title}")
    println("contentSha1=${summary.contentSha1}")
    println("difficulties=${summary.difficulties.joinToString(",")}")
}
