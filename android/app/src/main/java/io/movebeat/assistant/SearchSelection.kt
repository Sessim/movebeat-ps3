package io.movebeat.assistant

/**
 * Select a search result automatically only when the query is an explicit
 * BeatSaver ID/key lookup and the response contains exactly one map.
 */
internal fun autoSelectDirectLookupKey(query: String, resultKeys: List<String>): String? {
    val directLookup = Regex("(?i)(?:!bsr\\s+)?[0-9a-f]+").matches(query.trim())
    return if (directLookup) resultKeys.singleOrNull() else null
}
