package io.movebeat.assistant

fun main(args: Array<String>) {
    require(args.size == 2) { "usage: ClientSmoke <query> <expected-key>" }
    val results = BeatSaverClient.search(args[0])
    require(results.any { it.key.equals(args[1], ignoreCase = true) }) {
        "Expected key ${args[1]} was not returned by search"
    }
    val download = BeatSaverClient.download(args[1])
    println("ANDROID_KOTLIN_NETWORK=PASS")
    println("key=${download.key}")
    println("title=${download.summary.title}")
    println("mapper=${download.summary.mapper}")
    println("contentSha1=${download.summary.contentSha1}")
    println("bytes=${download.bytes.size}")
}
