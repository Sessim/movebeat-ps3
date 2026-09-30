package io.movebeat.assistant

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder

data class SearchItem(val key: String, val title: String, val mapper: String)
data class VerifiedDownload(val key: String, val bytes: ByteArray, val summary: ArchiveSummary)

internal fun mapperWithBeatSaverFallback(archiveMapper: String, uploader: String): String =
    uploader.trim().ifBlank { archiveMapper.trim() }

object BeatSaverClient {
    private const val API = "https://api.beatsaver.com"
    private const val USER_AGENT = "MoveBeat-Android/0.1"

    fun search(query: String): List<SearchItem> {
        require(query.isNotBlank()) { "Search query cannot be empty" }
        val directKey = query.trim().replace(Regex("^!bsr\\s+", RegexOption.IGNORE_CASE), "")
        if (directKey.matches(Regex("[0-9a-fA-F]+"))) {
            val item = JSONObject(get("$API/maps/id/$directKey"))
            return listOf(SearchItem(item.getString("id"), item.optString("name", "Untitled"),
                item.optJSONObject("uploader")?.optString("name", "unknown") ?: "unknown"))
        }
        val encoded = URLEncoder.encode(query.trim(), "UTF-8")
        val root = JSONObject(get("$API/search/text/0?q=$encoded&order=Relevance&pageSize=20"))
        val docs = root.getJSONArray("docs")
        return (0 until docs.length()).map { index ->
            val item = docs.getJSONObject(index)
            SearchItem(
                item.getString("id"), item.optString("name", "Untitled"),
                item.optJSONObject("uploader")?.optString("name", "unknown") ?: "unknown"
            )
        }
    }

    fun download(key: String): VerifiedDownload {
        val normalized = key.trim().replace(Regex("^!bsr\\s+", RegexOption.IGNORE_CASE), "")
        require(normalized.matches(Regex("[0-9a-fA-F]+"))) { "Enter a BeatSaver key, for example !bsr 54851" }
        val metadata = JSONObject(get("$API/maps/id/${URLEncoder.encode(normalized, "UTF-8")}"))
        val versions = metadata.getJSONArray("versions")
        val candidates = (0 until versions.length()).map { versions.getJSONObject(it) }
        val published = candidates.filter { it.optString("state").equals("Published", true) }
        val version = (published.ifEmpty { candidates }).maxByOrNull { it.optString("createdAt") }
            ?: error("Map has no downloadable version")
        val url = version.getString("downloadURL")
        val uri = URI(url)
        require(uri.scheme == "https" &&
            (uri.host == "beatsaver.com" || uri.host.endsWith(".beatsaver.com"))) {
            "BeatSaver returned a download outside its HTTPS domains"
        }
        val bytes = getBytes(url, BeatSaverArchive.MAX_ARCHIVE_BYTES)
        val archiveSummary = BeatSaverArchive.inspect(bytes, version.optString("hash"))
        val uploader = metadata.optJSONObject("uploader")?.optString("name").orEmpty()
        val summary = archiveSummary.copy(
            mapper = mapperWithBeatSaverFallback(archiveSummary.mapper, uploader))
        return VerifiedDownload(metadata.optString("id", normalized), bytes, summary)
    }

    private fun get(url: String): String = String(getBytes(url, 2 * 1024 * 1024), Charsets.UTF_8)

    private fun getBytes(url: String, limit: Int): ByteArray {
        var target = URI(url)
        repeat(4) { redirects ->
            require(target.scheme == "https" &&
                (target.host == "beatsaver.com" || target.host?.endsWith(".beatsaver.com") == true)) {
                "Download left BeatSaver HTTPS domains"
            }
            val connection = target.toURL().openConnection() as HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 30_000
            connection.readTimeout = 60_000
            connection.setRequestProperty("User-Agent", USER_AGENT)
            connection.setRequestProperty("Accept", "application/json, application/zip")
            try {
                val status = connection.responseCode
                if (status in 300..399) {
                    require(redirects < 3) { "Too many redirects" }
                    val location = connection.getHeaderField("Location") ?: error("Redirect has no location")
                    target = target.resolve(location)
                    return@repeat
                }
                require(status in 200..299) { "HTTP $status" }
                val output = java.io.ByteArrayOutputStream()
                connection.inputStream.use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                        require(output.size() <= limit) { "Download exceeds the safety limit" }
                    }
                }
                return output.toByteArray()
            } finally {
                connection.disconnect()
            }
        }
        error("Too many redirects")
    }
}
