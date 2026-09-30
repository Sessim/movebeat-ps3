package io.movebeat.assistant

import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.security.MessageDigest
import java.util.Locale
import java.util.zip.ZipInputStream

data class ArchiveSummary(
    val title: String,
    val songAuthor: String,
    val mapper: String,
    val difficulties: List<String>,
    val contentSha1: String
)

object BeatSaverArchive {
    const val MAX_ARCHIVE_BYTES = 20 * 1024 * 1024
    const val MAX_MEMBER_BYTES = 64 * 1024 * 1024
    const val MAX_TOTAL_BYTES = 128 * 1024 * 1024

    fun inspect(bytes: ByteArray, expectedHash: String? = null): ArchiveSummary {
        val files = readFiles(bytes)
        return summarize(files, expectedHash)
    }

    internal fun readFiles(bytes: ByteArray): Map<String, ByteArray> {
        require(bytes.size <= MAX_ARCHIVE_BYTES) { "ZIP exceeds the 20 MiB limit" }
        val files = linkedMapOf<String, ByteArray>()
        var total = 0L
        ZipInputStream(ByteArrayInputStream(bytes)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                if (entry.isDirectory) continue
                val normalized = entry.name.replace('\\', '/')
                val parts = normalized.split('/')
                require(!normalized.startsWith('/') && parts.none { it == ".." }) {
                    "Unsafe ZIP path: ${entry.name}"
                }
                val leaf = parts.last()
                val key = leaf.lowercase(Locale.ROOT)
                require(!files.containsKey(key)) { "Ambiguous duplicate filename: $leaf" }
                val data = readLimited(zip, MAX_MEMBER_BYTES)
                total += data.size
                require(total <= MAX_TOTAL_BYTES) { "ZIP expands beyond the safety limit" }
                files[key] = data
            }
        }
        return files
    }

    internal fun summarize(files: Map<String, ByteArray>, expectedHash: String? = null): ArchiveSummary {
        val infoBytes = member(files, "Info.dat")
        val info = JSONObject(String(infoBytes, Charsets.UTF_8).removePrefix("\uFEFF"))
        val digest = MessageDigest.getInstance("SHA-1")
        digest.update(infoBytes)
        val difficulties = mutableListOf<String>()
        if (info.has("_difficultyBeatmapSets")) {
            val sets = info.getJSONArray("_difficultyBeatmapSets")
            for (setIndex in 0 until sets.length()) {
                val set = sets.getJSONObject(setIndex)
                val characteristic = set.optString("_beatmapCharacteristicName")
                val maps = set.getJSONArray("_difficultyBeatmaps")
                for (mapIndex in 0 until maps.length()) {
                    val map = maps.getJSONObject(mapIndex)
                    val difficulty = map.optString("_difficulty")
                    val filename = map.getString("_beatmapFilename")
                    digest.update(member(files, filename))
                    difficulties += "$characteristic:$difficulty"
                }
            }
        } else {
            val audio = info.optJSONObject("audio")
            val bpmFilename = audio?.optString("audioDataFilename").orEmpty()
            if (bpmFilename.isNotEmpty()) digest.update(member(files, bpmFilename))
            val maps = info.getJSONArray("difficultyBeatmaps")
            for (index in 0 until maps.length()) {
                val map = maps.getJSONObject(index)
                val characteristic = map.optString("characteristic")
                val difficulty = map.optString("difficulty")
                digest.update(member(files, map.getString("beatmapDataFilename")))
                val lights = map.optString("lightshowDataFilename")
                if (lights.isNotEmpty()) digest.update(member(files, lights))
                difficulties += "$characteristic:$difficulty"
            }
        }
        val hash = digest.digest().joinToString("") { "%02x".format(it) }
        if (!expectedHash.isNullOrBlank()) {
            require(hash.equals(expectedHash, ignoreCase = true)) {
                "Beat Saber content SHA-1 does not match BeatSaver metadata"
            }
        }
        val song = info.optJSONObject("song")
        return ArchiveSummary(
            title = if (song != null) song.optString("title") else info.optString("_songName"),
            songAuthor = if (song != null) song.optString("author") else info.optString("_songAuthorName"),
            mapper = info.optString("_levelAuthorName"),
            difficulties = difficulties,
            contentSha1 = hash
        )
    }

    internal fun member(files: Map<String, ByteArray>, filename: String): ByteArray =
        files[filename.replace('\\', '/').substringAfterLast('/').lowercase(Locale.ROOT)]
            ?: error("ZIP does not contain $filename")

    private fun readLimited(zip: ZipInputStream, limit: Int): ByteArray {
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(64 * 1024)
        while (true) {
            val count = zip.read(buffer)
            if (count < 0) break
            output.write(buffer, 0, count)
            require(output.size() <= limit) { "ZIP member exceeds the safety limit" }
        }
        return output.toByteArray()
    }
}
