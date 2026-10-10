package io.movebeat.assistant

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.BufferedReader
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class MoveBeatFtpUploadTest {
    @Test fun uploadsVerifiesAndPublishesDirectoryAtomically() {
        FakePs3Ftp().use { server ->
            var finalProgress = 0L to 0L
            val path = MoveBeatFtpUpload.upload(bundle(), "127.0.0.1", server.port,
                "test-user", "test-pass") { done, total -> finalProgress = done to total }
            assertEquals("${MoveBeatFtpUpload.REMOTE_LIBRARY}/testmap", path)
            assertTrue(server.levels.containsKey("testmap"))
            assertEquals(setOf("level.ini", "audio.ogg", "conversion-report.json"),
                server.levels.getValue("testmap").keys)
            assertEquals("test-pass", server.passwordSeen)
            assertEquals(finalProgress.second, finalProgress.first)
            assertTrue(finalProgress.second > 0)
            assertTrue(server.stagingNames.isEmpty())
        }
    }

    @Test fun conflictDoesNotReplaceExistingLevel() {
        val original = mapOf("level.ini" to "keep me".toByteArray())
        FakePs3Ftp(mapOf("testmap" to original)).use { server ->
            val error = try {
                MoveBeatFtpUpload.upload(bundle(), "127.0.0.1", server.port,
                    "test-user", "test-pass")
                null
            } catch (caught: IllegalArgumentException) { caught }
            assertTrue(error?.message.orEmpty().contains("уже существует"))
            assertEquals(original, server.levels.getValue("testmap"))
            assertTrue(server.stagingNames.isEmpty())
        }
    }

    @Test fun failedReadbackLeavesNoPublishedOrStagingDirectory() {
        FakePs3Ftp(corruptReadback = true).use { server ->
            val error = try {
                MoveBeatFtpUpload.upload(bundle(), "127.0.0.1", server.port,
                    "test-user", "test-pass")
                null
            } catch (caught: IllegalArgumentException) { caught }
            assertTrue(error?.message.orEmpty().contains("Проверка загруженного файла"))
            assertFalse(server.levels.containsKey("testmap"))
            assertTrue(server.stagingNames.isEmpty())
        }
    }

    @Test fun stalledServerAbortsWithTheStage() {
        FakePs3Ftp(stallStore = true).use { server ->
            val stages = mutableListOf<String>()
            val started = System.currentTimeMillis()
            val error = try {
                MoveBeatFtpUpload.upload(bundle(audioBytes = 8 * 1024 * 1024), "127.0.0.1",
                    server.port, "test-user", "test-pass", stallTimeoutMs = 1000,
                    stage = { stages += it })
                null
            } catch (caught: IllegalStateException) { caught }
            assertTrue(error?.message.orEmpty().contains("перестал принимать данные"))
            assertTrue(error?.message.orEmpty().contains("отправка audio.ogg"))
            assertTrue(System.currentTimeMillis() - started < 15_000)
            assertTrue(stages.any { it.startsWith("отправка audio.ogg") })
            assertFalse(server.levels.containsKey("testmap"))
        }
    }

    private fun bundle(audioBytes: Int = 4): PreparedBundle {
        val audio = ByteArray(4 + audioBytes).also {
            it[0] = 79; it[1] = 103; it[2] = 103; it[3] = 83
        }
        val files = linkedMapOf(
            "level.ini" to "format=1\ntitle=Test\n".toByteArray(),
            "audio.ogg" to audio
        )
        val entries = JSONArray()
        for ((name, bytes) in files) entries.put(JSONObject()
            .put("path", name).put("size", bytes.size).put("sha256", sha256(bytes)))
        files["conversion-report.json"] = JSONObject().put("files", entries)
            .toString().toByteArray()
        val archive = ByteArrayOutputStream()
        ZipOutputStream(archive).use { zip ->
            for ((name, bytes) in files) {
                zip.putNextEntry(ZipEntry("testmap/$name"))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return PreparedBundle("testmap-movebeat.zip", archive.toByteArray(), "Test", 1)
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes).joinToString("") { "%02x".format(it.toInt() and 0xff) }

    private class FakePs3Ftp(initial: Map<String, Map<String, ByteArray>> = emptyMap(),
                             private val corruptReadback: Boolean = false,
                             private val stallStore: Boolean = false) : AutoCloseable {
        private val listener = ServerSocket(0, 4, InetAddress.getLoopbackAddress())
        val port: Int get() = listener.localPort
        val levels = initial.mapValuesTo(linkedMapOf()) { it.value.toMutableMap() }
        val stagingNames = mutableSetOf<String>()
        @Volatile var passwordSeen = ""
            private set
        private val worker = Thread { serve() }.apply { isDaemon = true; start() }

        private fun serve() {
            listener.accept().use { control ->
                control.soTimeout = 5000
                val input = BufferedReader(InputStreamReader(control.getInputStream()))
                val output = OutputStreamWriter(control.getOutputStream())
                fun reply(text: String) { output.write(text + "\r\n"); output.flush() }
                var cwd = MoveBeatFtpUpload.REMOTE_LIBRARY
                var pendingRename = ""
                var passive: ServerSocket? = null
                reply("220 MoveBeat test FTP")
                while (true) {
                    val line = input.readLine() ?: break
                    val command = line.substringBefore(' ').uppercase()
                    val argument = line.substringAfter(' ', "")
                    when (command) {
                        "USER" -> reply("331 Password required")
                        "PASS" -> { passwordSeen = argument; reply("230 Logged in") }
                        "TYPE" -> reply("200 Binary")
                        "CWD" -> {
                            if (argument == MoveBeatFtpUpload.REMOTE_LIBRARY) {
                                cwd = MoveBeatFtpUpload.REMOTE_LIBRARY; reply("250 Directory changed")
                            } else if (stagingNames.contains(argument)) {
                                cwd = argument; reply("250 Directory changed")
                            } else reply("550 Not found")
                        }
                        "EPSV" -> {
                            passive?.close()
                            passive = ServerSocket(0, 1, InetAddress.getLoopbackAddress())
                            reply("229 Entering Extended Passive Mode (|||${passive!!.localPort}|)")
                        }
                        "NLST" -> {
                            val names = if (cwd == MoveBeatFtpUpload.REMOTE_LIBRARY)
                                levels.keys + stagingNames else emptySet()
                            if (names.isEmpty()) {
                                passive?.close(); passive = null
                                reply("550 Directory empty")
                            } else {
                                reply("150 Listing")
                                passive!!.accept().use { data ->
                                    data.getOutputStream().write(names.joinToString("\r\n").toByteArray() + "\r\n".toByteArray())
                                }
                                passive!!.close(); passive = null; reply("226 Listing complete")
                            }
                        }
                        "MKD" -> {
                            stagingNames += argument
                            levels[argument] = linkedMapOf()
                            reply("257 Directory created")
                        }
                        "STOR" -> {
                            reply("150 Send data")
                            if (stallStore && argument == "audio.ogg") {
                                // Accept the data connection, then never read it.
                                val data = passive!!.accept()
                                data.receiveBufferSize = 4096
                                Thread.sleep(4000)
                                data.close()
                                break
                            }
                            val bytes = passive!!.accept().use { it.getInputStream().readBytes() }
                            levels.getValue(cwd)[argument] = bytes
                            passive!!.close(); passive = null; reply("226 Stored")
                        }
                        "RETR" -> {
                            reply("150 Sending data")
                            val original = levels.getValue(cwd).getValue(argument)
                            val bytes = if (corruptReadback) original.copyOf().also {
                                if (it.isNotEmpty()) it[0] = (it[0].toInt() xor 0x1).toByte()
                            } else original
                            passive!!.accept().use { it.getOutputStream().write(bytes) }
                            passive!!.close(); passive = null; reply("226 Retrieved")
                        }
                        "RNFR" -> { pendingRename = argument; reply("350 Ready") }
                        "RNTO" -> {
                            levels[argument] = levels.remove(pendingRename)!!
                            stagingNames.remove(pendingRename)
                            reply("250 Renamed")
                        }
                        "DELE" -> {
                            val pieces = argument.split('/', limit = 2)
                            levels[pieces[0]]?.remove(pieces.getOrElse(1) { "" })
                            reply("250 Deleted")
                        }
                        "RMD" -> {
                            levels.remove(argument); stagingNames.remove(argument); reply("250 Removed")
                        }
                        "QUIT" -> { reply("221 Bye"); break }
                        else -> reply("502 Unsupported")
                    }
                }
                passive?.close()
            }
        }

        override fun close() {
            try { listener.close() } catch (_: Exception) { }
            worker.join(5000)
        }
    }
}
