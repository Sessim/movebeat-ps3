package io.movebeat.assistant

fun main() {
    check(autoSelectDirectLookupKey("!bsr 54851", listOf("54851")) == "54851")
    check(autoSelectDirectLookupKey("54851", listOf("54851")) == "54851")
    check(autoSelectDirectLookupKey("abc123", listOf("abc123")) == "abc123")
    check(autoSelectDirectLookupKey("artist song", listOf("abc123")) == null)
    check(autoSelectDirectLookupKey("!bsr 54851", listOf("54851", "99999")) == null)
    check(autoSelectDirectLookupKey("!bsr", listOf("54851")) == null)
    check(mapperWithBeatSaverFallback("", "BeatSaver Mapper") == "BeatSaver Mapper")
    check(mapperWithBeatSaverFallback("Archive Mapper", " ") == "Archive Mapper")
    println("ANDROID_SEARCH_SELECTION=PASS")
}
