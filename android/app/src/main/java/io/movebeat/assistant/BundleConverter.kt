package io.movebeat.assistant

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.math.floor
import kotlin.math.min

data class PreparedBundle(val filename: String, val bytes: ByteArray, val title: String, val charts: Int)

/** Builds the same format-1 level files as the desktop importer, without filesystem permissions. */
object BundleConverter {
    private data class Difficulty(val characteristic: String, val name: String, val source: String,
                                  val noteJumpSpeed: Double?, val startBeatOffset: Double)
    private data class Chart(val difficulty: Difficulty, val name: String, val rows: List<String>,
                             val notes: Int, val walls: Int, val chains: Int, val arcs: Int, val skipped: Int)
    private data class Event(val time: Int, val row: String)
    private val directions = listOf("U", "D", "L", "R", "UL", "UR", "DL", "DR", "A")

    private fun normalizedAngle(angle: Double): Double {
        val wrapped = angle % 360.0
        return when {
            wrapped >= 180.0 -> wrapped - 360.0
            wrapped < -180.0 -> wrapped + 360.0
            else -> wrapped
        }
    }

    private fun angleText(angle: Double): String =
        String.format(Locale.ROOT, "%.6f", angle).trimEnd('0').trimEnd('.')

    private fun difficulty(characteristic: String, name: String, source: String,
                           entry: JSONObject, speedKey: String,
                           offsetKey: String): Difficulty {
        if (!entry.has(speedKey)) return Difficulty(characteristic, name, source, null, 0.0)
        val speed = entry.optDouble(speedKey, Double.NaN)
        val offset = entry.optDouble(offsetKey, 0.0)
        require(speed.isFinite() && speed in 0.1..40.0) {
            "Note jump speed for $name must be between 0.1 and 40"
        }
        require(offset.isFinite() && offset in -16.0..16.0) {
            "Note jump offset for $name must be between -16 and 16"
        }
        return Difficulty(characteristic, name, source, speed, offset)
    }

    fun convert(zipBytes: ByteArray, key: String = "", expectedHash: String? = null,
                mapperOverride: String? = null): PreparedBundle =
        convert(zipBytes, key, expectedHash, AndroidCard::render, mapperOverride)

    internal fun convert(zipBytes: ByteArray, key: String, expectedHash: String?,
                         cardRenderer: (ByteArray?, String, String, String) -> ByteArray,
                         mapperOverride: String? = null): PreparedBundle {
        val source = BeatSaverArchive.readFiles(zipBytes)
        val summary = BeatSaverArchive.summarize(source, expectedHash)
        val info = json(BeatSaverArchive.member(source, "Info.dat"))
        val v2 = info.has("_difficultyBeatmapSets")
        val audioInfo = info.optJSONObject("audio")
        val bpm = if (v2) info.optDouble("_beatsPerMinute") else audioInfo?.optDouble("bpm") ?: 0.0
        require(bpm.isFinite() && bpm > 0) { "BPM must be a positive finite number" }
        val offset = if (v2) info.optDouble("_songTimeOffset", 0.0) else 0.0
        val audioName = if (v2) info.optString("_songFilename") else audioInfo?.optString("songFilename").orEmpty()
        val audio = BeatSaverArchive.member(source, audioName)
        require(audio.size >= 4 && audio.copyOfRange(0, 4).contentEquals("OggS".toByteArray()) &&
            audio.take(512).toByteArray().indexOfSequence("\u0001vorbis".toByteArray()) >= 0) {
            "Audio is not Ogg/Vorbis; transcode it before PS3 export"
        }
        val coverName = if (v2) info.optString("_coverImageFilename") else info.optString("coverImageFilename")
        val cover = if (coverName.isNotBlank()) BeatSaverArchive.member(source, coverName) else null
        val mapper = mapperOverride?.trim()?.takeIf { it.isNotEmpty() } ?: summary.mapper
        val difficulties = mutableListOf<Difficulty>()
        if (v2) {
            val sets = info.optJSONArray("_difficultyBeatmapSets") ?: JSONArray()
            for (set in sets.objects()) {
                for (entry in (set.optJSONArray("_difficultyBeatmaps") ?: JSONArray()).objects()) {
                    difficulties += difficulty(set.optString("_beatmapCharacteristicName"),
                        entry.optString("_difficulty"), entry.optString("_beatmapFilename"),
                        entry, "_noteJumpMovementSpeed", "_noteJumpStartBeatOffset")
                }
            }
        } else {
            for (entry in (info.optJSONArray("difficultyBeatmaps") ?: JSONArray()).objects()) {
                difficulties += difficulty(entry.optString("characteristic"),
                    entry.optString("difficulty"), entry.optString("beatmapDataFilename"),
                    entry, "noteJumpMovementSpeed", "noteJumpStartBeatOffset")
            }
        }
        val selected = difficulties.filter { it.characteristic.equals("Standard", true) }
        require(selected.isNotEmpty()) { "No Standard difficulty available for PS3" }
        val files = linkedMapOf<String, ByteArray>()
        val audioExt = if (audioName.substringAfterLast('.', "").equals("egg", true)) "egg" else "ogg"
        val outputAudio = "audio.$audioExt"
        files[outputAudio] = audio
        val coverExt = coverName.substringAfterLast('.', "png").lowercase(Locale.ROOT)
        require(cover == null || coverExt in listOf("png", "jpg", "jpeg")) { "Unsupported cover format" }
        val outputCover = if (cover != null) "cover.$coverExt" else ""
        if (cover != null) files[outputCover] = cover
        val card = cardRenderer(cover, summary.title, summary.songAuthor, mapper)
        require(card.size == 8 + LibraryCardCodec.WIDTH * LibraryCardCodec.HEIGHT * 2 &&
            card.copyOfRange(0, 8).contentEquals(byteArrayOf(77, 66, 76, 67, 2, 0, 0, 96))) {
            "Library card is not a 512x96 RGB565 card"
        }
        files["library.mblc"] = card
        val charts = selected.map { diff ->
            val filename = safeName("${diff.characteristic}_${diff.name}") + ".mbchart"
            val chart = chart(json(BeatSaverArchive.member(source, diff.source)), diff, filename, bpm, offset)
            files[filename] = ("# notes: time_ms,lane,row,hand,direction\n" +
                "# walls: W,time_ms,duration_ms,lane,width,row,height\n" +
                "# chains: C,time_ms,lane,row,hand\n" +
                "# arcs: A,head_ms,tail_ms,head_lane,head_row,head_dir,head_mu," +
                "tail_lane,tail_row,tail_dir,tail_mu,mid_mode,hand\n" +
                chart.rows.joinToString("\n") + "\n").toByteArray(Charsets.UTF_8)
            chart
        }
        val contentHash = summary.contentSha1
        val archiveHash = digest("SHA-1", zipBytes)
        val manifest = mutableListOf("format=1", if (key.isBlank()) "source=beat_saber_zip" else "source=beatsaver",
            "source_key=${clean(key)}", "source_hash=$contentHash", "title=${clean(summary.title)}",
            "song_author=${clean(summary.songAuthor)}", "mapper=${clean(mapper)}",
            "bpm=${formatBpm(bpm)}", "audio=$outputAudio", "cover=$outputCover", "chart_count=${charts.size}")
        charts.forEachIndexed { i, c ->
            manifest += listOf(
                "chart${i}_characteristic=${clean(c.difficulty.characteristic)}",
                "chart${i}_difficulty=${clean(c.difficulty.name)}", "chart${i}_file=${c.name}",
                "chart${i}_notes=${c.notes}", "chart${i}_obstacles=${c.walls}",
                "chart${i}_chains=${c.chains}", "chart${i}_arcs=${c.arcs}", "chart${i}_skipped=${c.skipped}")
            c.difficulty.noteJumpSpeed?.let { speed ->
                manifest += "chart${i}_njs=${formatBpm(speed)}"
                manifest += "chart${i}_start_beat_offset=${formatBpm(c.difficulty.startBeatOffset)}"
            }
        }
        files["level.ini"] = (manifest.joinToString("\n") + "\n").toByteArray(Charsets.UTF_8)
        val chartReport = JSONArray()
        val warnings = JSONArray()
        for (c in charts) {
            chartReport.put(JSONObject().put("characteristic", c.difficulty.characteristic)
                .put("difficulty", c.difficulty.name).put("file", c.name).put("notes", c.notes)
                .put("obstacles", c.walls).put("chains", c.chains).put("arcs", c.arcs).put("skipped", c.skipped))
            if (c.skipped > 0) warnings.put("${c.difficulty.characteristic}/${c.difficulty.name}: skipped ${c.skipped} unsupported object(s)")
        }
        val entries = JSONArray()
        files.toSortedMap(String.CASE_INSENSITIVE_ORDER).forEach { (name, data) ->
            entries.put(JSONObject().put("path", name).put("size", data.size).put("sha256", digest("SHA-256", data)))
        }
        val report = JSONObject()
            .put("bundleFormat", 1)
            .put("converter", JSONObject().put("name", "MoveBeat Track Assistant Core").put("version", "0.5.0-android"))
            .put("source", JSONObject().put("type", if (key.isBlank()) "beat_saber_zip" else "beatsaver")
                .put("key", clean(key)).put("contentSha1", contentHash).put("archiveSha1", archiveHash))
            .put("level", JSONObject().put("title", clean(summary.title)).put("songAuthor", clean(summary.songAuthor))
                .put("mapper", clean(mapper)).put("bpm", bpm).put("audio", outputAudio)
                .put("audioContainer", "ogg").put("audioCodec", "vorbis")
                .put("cover", outputCover).put("libraryCard", "library.mblc"))
            .put("charts", chartReport).put("warnings", warnings).put("files", entries)
        files["conversion-report.json"] = (sortedJson(report) + "\n").toByteArray(Charsets.UTF_8)
        val folder = safeName(key.ifBlank { contentHash.take(12) })
        val zipped = ByteArrayOutputStream()
        ZipOutputStream(zipped).use { output ->
            for ((name, data) in files) {
                output.putNextEntry(ZipEntry("$folder/$name"))
                output.write(data)
                output.closeEntry()
            }
        }
        return PreparedBundle("$folder-movebeat.zip", zipped.toByteArray(), summary.title, charts.size)
    }

    private fun chart(map: JSONObject, diff: Difficulty, name: String, bpm: Double, offset: Double): Chart {
        val events = mutableListOf<Pair<Double, Double>>()
        if (map.has("bpmEvents")) {
            for (e in (map.optJSONArray("bpmEvents") ?: JSONArray()).objects())
                events += e.optDouble("b") to e.optDouble("m")
        } else {
            for (e in (map.optJSONArray("_events") ?: JSONArray()).objects()) if (e.optInt("_type", -1) == 100)
                events += e.optDouble("_time") to e.optDouble("_floatValue")
        }
        val timeline = Timeline(bpm, offset, events)
        val rows = mutableListOf<Event>()
        var notes = 0; var walls = 0; var chains = 0; var arcs = 0; var skipped = 0
        fun note(beat: Double, x: Int, y: Int, color: Int, direction: Int,
                 angleOffset: Double = 0.0) {
            if (!angleOffset.isFinite() || x !in 0..3 || y !in 0..2 ||
                color !in listOf(0, 1, 3)) { skipped++; return }
            val cut = if (color == 3 || diff.characteristic.equals("NoArrows", true)) "A"
                else directions.getOrNull(direction)
            if (cut == null) { skipped++; return }
            val ms = timeline.ms(beat)
            val hand = if (color == 3) "B" else if (color == 0) "L" else "R"
            val normalized = normalizedAngle(angleOffset)
            val row = if (kotlin.math.abs(normalized) < 0.0000005 ||
                color == 3 || diff.characteristic.equals("NoArrows", true))
                "$ms,$x,$y,$hand,$cut"
            else "$ms,$x,$y,$hand,$cut,${angleText(normalized)}"
            rows += Event(ms, row)
            notes++
        }
        fun wall(beat: Double, duration: Double, x: Int, width: Int, y: Int, height: Int) {
            if (!beat.isFinite() || !duration.isFinite() || duration <= 0 || x !in 0..3 ||
                width < 1 || x + width > 4 || y !in 0..2 || height < 1) { skipped++; return }
            val ms = timeline.ms(beat); val length = timeline.ms(beat + duration) - ms
            if (length <= 0) { skipped++; return }
            rows += Event(ms, "W,$ms,$length,$x,$width,$y,${min(height, 3 - y)}")
            walls++
        }
        fun chain(hb: Double, tb: Double, x: Int, y: Int, color: Int,
                  tx: Int, ty: Int, slices: Int, squish: Double) {
            if (!hb.isFinite() || !tb.isFinite() || !squish.isFinite() || tb < hb ||
                x !in 0..3 || y !in 0..2 || color !in 0..1 || tx !in 0..3 || ty !in 0..2 ||
                slices !in 2..64 || squish <= 0) { skipped++; return }
            for (link in 1 until slices) {
                val fraction = link.toDouble() / (slices - 1)
                val cx = floor(x + (tx - x) * fraction * squish + 0.5).toInt()
                val cy = floor(y + (ty - y) * fraction * squish + 0.5).toInt()
                if (cx !in 0..3 || cy !in 0..2) { skipped++; continue }
                val ms = timeline.ms(hb + (tb - hb) * fraction)
                rows += Event(ms, "C,$ms,$cx,$cy,${if (color == 0) "L" else "R"}")
                chains++
            }
        }
        fun arc(hb: Double, tb: Double, x: Int, y: Int, direction: Int,
                multiplier: Double, tx: Int, ty: Int, tailDirection: Int,
                tailMultiplier: Double, midMode: Int, color: Int) {
            if (!hb.isFinite() || !tb.isFinite() || tb < hb || x !in 0..3 || y !in 0..2 ||
                tx !in 0..3 || ty !in 0..2 || color !in 0..1 ||
                direction !in 0..8 || tailDirection !in 0..8 ||
                !multiplier.isFinite() || multiplier !in 0.0..10.0 ||
                !tailMultiplier.isFinite() || tailMultiplier !in 0.0..10.0 ||
                midMode !in 0..2) { skipped++; return }
            val start = timeline.ms(hb); val end = timeline.ms(tb)
            if (end < start) { skipped++; return }
            rows += Event(start, "A,$start,$end,$x,$y,$direction,${formatArcMultiplier(multiplier)}," +
                "$tx,$ty,$tailDirection,${formatArcMultiplier(tailMultiplier)},$midMode," +
                if (color == 0) "L" else "R")
            arcs++
        }
        if (map.has("_notes")) {
            for (n in (map.optJSONArray("_notes") ?: JSONArray()).objects()) {
                val type = n.optInt("_type", -1)
                if (type in listOf(0, 1, 3)) note(n.optDouble("_time"), n.optInt("_lineIndex", -1),
                    n.optInt("_lineLayer", -1), type, n.optInt("_cutDirection", 8),
                    n.optDouble("_angleOffset", 0.0))
            }
        } else {
            val indexed = map.optString("version").startsWith("4") || map.has("colorNotesData")
            val noteData = map.optJSONArray("colorNotesData") ?: JSONArray()
            for (n in (map.optJSONArray("colorNotes") ?: JSONArray()).objects()) {
                val data = if (indexed) noteData.objectAt(n.optInt("i", -1)) ?: continue else n
                note(n.optDouble("b"), data.optInt("x", -1), data.optInt("y", -1),
                    data.optInt("c", -1), data.optInt("d", 8),
                    data.optDouble("a", 0.0))
            }
            val bombIndexed = map.optString("version").startsWith("4") || map.has("bombNotesData")
            val bombData = map.optJSONArray("bombNotesData") ?: JSONArray()
            for (n in (map.optJSONArray("bombNotes") ?: JSONArray()).objects()) {
                val data = if (bombIndexed) bombData.objectAt(n.optInt("i", -1)) ?: continue else n
                note(n.optDouble("b"), data.optInt("x", -1), data.optInt("y", -1), 3, 8)
            }
        }
        if (map.has("_obstacles")) for (w in (map.optJSONArray("_obstacles") ?: JSONArray()).objects()) {
            val type = w.optInt("_type")
            val y = if (type == 1) 2 else if (type == 2) w.optInt("_lineLayer", -1) else 0
            val height = if (type == 1) 3 else if (type == 2) w.optInt("_height", -1) else 5
            wall(w.optDouble("_time"), w.optDouble("_duration"), w.optInt("_lineIndex", -1),
                w.optInt("_width", -1), y, height)
        } else {
            val indexed = map.optString("version").startsWith("4") || map.has("obstaclesData")
            val data = map.optJSONArray("obstaclesData") ?: JSONArray()
            for (w in (map.optJSONArray("obstacles") ?: JSONArray()).objects()) {
                val fields = if (indexed) data.objectAt(w.optInt("i", -1)) ?: continue else w
                wall(w.optDouble("b"), fields.optDouble("d"), fields.optInt("x", -1),
                    fields.optInt("w", -1), fields.optInt("y", -1), fields.optInt("h", -1))
            }
        }
        if (map.optString("version").startsWith("4") || map.has("chainsData")) {
            val noteData = map.optJSONArray("colorNotesData") ?: JSONArray()
            val chainData = map.optJSONArray("chainsData") ?: JSONArray()
            for (c in (map.optJSONArray("chains") ?: JSONArray()).objects()) {
                val head = noteData.objectAt(c.optInt("i", -1)) ?: continue
                val tail = chainData.objectAt(c.optInt("ci", -1)) ?: continue
                chain(c.optDouble("hb"), c.optDouble("tb"), head.optInt("x", -1), head.optInt("y", -1),
                    head.optInt("c", -1), tail.optInt("tx", -1), tail.optInt("ty", -1),
                    tail.optInt("c", -1), tail.optDouble("s"))
            }
        } else for (c in (map.optJSONArray("burstSliders") ?: JSONArray()).objects())
            chain(c.optDouble("b"), c.optDouble("tb"), c.optInt("x", -1), c.optInt("y", -1),
                c.optInt("c", -1), c.optInt("tx", -1), c.optInt("ty", -1), c.optInt("sc", -1), c.optDouble("s"))
        if (map.has("_sliders")) {
            for (a in (map.optJSONArray("_sliders") ?: JSONArray()).objects())
                arc(a.optDouble("_headTime"), a.optDouble("_tailTime"), a.optInt("_headLineIndex", -1),
                    a.optInt("_headLineLayer", -1), a.optInt("_headCutDirection", 8),
                    a.optDouble("_headControlPointLengthMultiplier", 1.0),
                    a.optInt("_tailLineIndex", -1), a.optInt("_tailLineLayer", -1),
                    a.optInt("_tailCutDirection", 8),
                    a.optDouble("_tailControlPointLengthMultiplier", 1.0),
                    a.optInt("_sliderMidAnchorMode", 0), a.optInt("_colorType", -1))
        } else if (map.optString("version").startsWith("4") || map.has("arcsData")) {
            val data = map.optJSONArray("colorNotesData") ?: JSONArray()
            val arcData = map.optJSONArray("arcsData") ?: JSONArray()
            for (a in (map.optJSONArray("arcs") ?: JSONArray()).objects()) {
                val head = data.objectAt(a.optInt("hi", -1)) ?: continue
                val tail = data.objectAt(a.optInt("ti", -1)) ?: continue
                val metadata = arcData.objectAt(a.optInt("ai", -1)) ?: continue
                arc(a.optDouble("hb"), a.optDouble("tb"), head.optInt("x", -1),
                    head.optInt("y", -1), head.optInt("d", 8), metadata.optDouble("m", 1.0),
                    tail.optInt("x", -1), tail.optInt("y", -1), tail.optInt("d", 8),
                    metadata.optDouble("tm", 1.0), metadata.optInt("a", 0), head.optInt("c", -1))
            }
        } else {
            for (a in (map.optJSONArray("sliders") ?: JSONArray()).objects())
                arc(a.optDouble("b"), a.optDouble("tb"), a.optInt("x", -1), a.optInt("y", -1),
                    a.optInt("d", 8), a.optDouble("mu", 1.0), a.optInt("tx", -1),
                    a.optInt("ty", -1), a.optInt("tc", 8), a.optDouble("tmu", 1.0),
                    a.optInt("m", 0), a.optInt("c", -1))
        }
        return Chart(diff, name, rows.sortedBy { it.time }.map { it.row }, notes, walls, chains, arcs, skipped)
    }

    private class Timeline(val initialBpm: Double, offset: Double, bpmEvents: List<Pair<Double, Double>>) {
        private val start = if (offset.isFinite()) offset else 0.0
        private val events = bpmEvents.filter { it.first.isFinite() && it.second.isFinite() && it.second > 0 }
            .sortedBy { it.first }
        fun ms(beat: Double): Int {
            require(beat.isFinite()) { "Note beat must be finite" }
            var cursor = 0.0; var seconds = start; var bpm = initialBpm
            for ((eventBeat, eventBpm) in events) {
                if (eventBeat <= cursor) { bpm = eventBpm; continue }
                if (eventBeat >= beat) break
                seconds += (eventBeat - cursor) * 60.0 / bpm
                cursor = eventBeat; bpm = eventBpm
            }
            seconds += (beat - cursor) * 60.0 / bpm
            return kotlin.math.max(0, Math.rint(seconds * 1000.0).toInt())
        }
    }

    private fun json(data: ByteArray): JSONObject {
        require(data.size <= 8 * 1024 * 1024) { "JSON member exceeds 8 MiB" }
        return JSONObject(String(data, Charsets.UTF_8).removePrefix("\uFEFF"))
    }
    private fun sortedJson(value: Any?): String = when (value) {
        null, JSONObject.NULL -> "null"
        is JSONObject -> value.keys().asSequence().toList().sorted().joinToString(",", "{", "}") { key ->
            "${JSONObject.quote(key)}:${sortedJson(value.get(key))}"
        }
        is JSONArray -> (0 until value.length()).joinToString(",", "[", "]") { sortedJson(value.get(it)) }
        is String -> JSONObject.quote(value)
        is Boolean, is Number -> value.toString()
        else -> error("Unsupported JSON value")
    }
    private fun JSONArray.objects() = (0 until length()).mapNotNull { optJSONObject(it) }
    private fun JSONArray.objectAt(index: Int) = if (index in 0 until length()) optJSONObject(index) else null
    private fun digest(algorithm: String, data: ByteArray) = MessageDigest.getInstance(algorithm).digest(data)
        .joinToString("") { "%02x".format(it) }
    private fun clean(value: String) = value.replace(Regex("[\\r\\n\\x00]"), " ").replace('=', '-').trim()
    private fun safeName(value: String) = value.replace(Regex("[^A-Za-z0-9_-]"), "_").take(64).ifEmpty { "imported_map" }
    private fun formatBpm(value: Double) = if (value == value.toLong().toDouble()) value.toLong().toString() else value.toString()
    private fun formatArcMultiplier(value: Double): String {
        val formatted = String.format(Locale.ROOT, "%.6g", value)
        val exponent = formatted.indexOf('e')
        val mantissa = if (exponent >= 0) formatted.substring(0, exponent) else formatted
        val suffix = if (exponent >= 0) formatted.substring(exponent) else ""
        return mantissa.trimEnd('0').trimEnd('.') + suffix
    }
    private fun ByteArray.indexOfSequence(pattern: ByteArray): Int {
        for (i in 0..size - pattern.size) if (pattern.indices.all { this[i + it] == pattern[it] }) return i
        return -1
    }
}
