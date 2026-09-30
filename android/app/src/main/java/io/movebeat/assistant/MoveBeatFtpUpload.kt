package io.movebeat.assistant

import org.json.JSONObject
import java.io.BufferedReader
import java.io.ByteArrayInputStream
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.Socket
import java.security.MessageDigest
import java.util.Locale
import java.util.zip.ZipInputStream

/** FTP transfer for a locally prepared bundle. Credentials are never persisted. */
object MoveBeatFtpUpload {
    const val REMOTE_LIBRARY = "/dev_hdd0/game/MOVEBEAT1/USRDIR/levels"
    private const val TIMEOUT_MS = 20_000
    private val safeName = Regex("[A-Za-z0-9][A-Za-z0-9._+-]*")

    fun upload(bundle: PreparedBundle, host: String, port: Int, username: String,
               password: String, progress: (Long, Long) -> Unit = { _, _ -> }): String {
        require(host.isNotBlank() && host.none { it == '\r' || it == '\n' || it == '\u0000' }) {
            "Введите корректный адрес PS3"
        }
        require(port in 1..65535) { "FTP-порт должен быть от 1 до 65535" }
        require(username.none { it == '\r' || it == '\n' || it == '\u0000' } &&
            password.none { it == '\r' || it == '\n' || it == '\u0000' }) {
            "Имя пользователя или пароль содержит недопустимые символы"
        }
        val (levelName, files) = unpackAndVerify(bundle)
        var total = files.values.sumOf { it.size.toLong() } * 2L
        var done = 0L
        val stage = ".movebeat-upload-$levelName-${java.util.UUID.randomUUID().toString().take(12)}"
        val uploaded = mutableListOf<String>()

        FtpSession(host.trim(), port).use { ftp ->
            ftp.login(username.ifBlank { "anonymous" }, password)
            ftp.commandChecked("TYPE I", 200)
            ftp.commandChecked("CWD $REMOTE_LIBRARY", 250)
            val existing = ftp.listNames()
            require(existing.none { it.equals(levelName, ignoreCase = true) }) {
                "Уровень $levelName уже существует на PS3; файл не заменён"
            }
            ftp.commandChecked("MKD $stage", 257)
            var published = false
            try {
                ftp.commandChecked("CWD $stage", 250)
                for ((filename, bytes) in files) {
                    uploaded += filename
                    ftp.store(filename, bytes) { count ->
                        done += count
                        progress(done, total)
                    }
                }
                for ((filename, bytes) in files) {
                    val digest = MessageDigest.getInstance("SHA-256")
                    val remote = ftp.retrieve(filename) { chunk ->
                        digest.update(chunk)
                        done += chunk.size
                        progress(done, total)
                    }
                    require(remote == bytes.size.toLong() &&
                        digest.digest().contentEquals(MessageDigest.getInstance("SHA-256").digest(bytes))) {
                        "Проверка загруженного файла не прошла: $filename"
                    }
                }
                ftp.commandChecked("CWD $REMOTE_LIBRARY", 250)
                require(ftp.listNames().none { it.equals(levelName, ignoreCase = true) }) {
                    "Уровень $levelName появился на PS3 во время передачи; существующий файл не затронут"
                }
                ftp.commandChecked("RNFR $stage", 350)
                ftp.commandChecked("RNTO $levelName", 250)
                published = true
            } finally {
                if (!published) ftp.cleanupStage(stage, uploaded)
            }
        }
        total = total.coerceAtLeast(1L)
        progress(total, total)
        return "$REMOTE_LIBRARY/$levelName"
    }

    private fun unpackAndVerify(bundle: PreparedBundle): Pair<String, LinkedHashMap<String, ByteArray>> {
        val extracted = linkedMapOf<String, ByteArray>()
        var root: String? = null
        var expanded = 0L
        ZipInputStream(ByteArrayInputStream(bundle.bytes)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                if (entry.isDirectory) continue
                val parts = entry.name.replace('\\', '/').split('/')
                require(parts.size == 2 && parts.all { safeName.matches(it) }) {
                    "В bundle обнаружен недопустимый путь"
                }
                if (root == null) root = parts[0]
                require(root == parts[0]) { "Bundle содержит несколько каталогов уровней" }
                require(!extracted.containsKey(parts[1])) { "Повторяющееся имя файла в bundle" }
                val out = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(64 * 1024)
                while (true) {
                    val count = zip.read(buffer)
                    if (count < 0) break
                    expanded += count
                    require(expanded <= 128L * 1024 * 1024) { "Bundle распаковывается более чем в 128 МиБ" }
                    out.write(buffer, 0, count)
                }
                extracted[parts[1]] = out.toByteArray()
            }
        }
        val name = root ?: error("Bundle пуст")
        require(safeName.matches(name) && extracted.containsKey("level.ini") &&
            extracted.containsKey("conversion-report.json")) { "Bundle не является уровнем MoveBeat" }
        val reportBytes = extracted.getValue("conversion-report.json")
        val report = JSONObject(String(reportBytes, Charsets.UTF_8))
        val expected = linkedMapOf<String, Pair<Long, String>>()
        val entries = report.optJSONArray("files") ?: error("В отчёте bundle нет списка файлов")
        for (i in 0 until entries.length()) {
            val item = entries.getJSONObject(i)
            val filename = item.getString("path")
            require(safeName.matches(filename) && filename != "conversion-report.json" &&
                !expected.containsKey(filename)) { "В отчёте bundle есть недопустимое имя файла" }
            expected[filename] = item.getLong("size") to item.getString("sha256")
        }
        require(extracted.keys == expected.keys + "conversion-report.json") {
            "Состав файлов bundle не совпадает с conversion-report.json"
        }
        for ((filename, item) in expected) {
            val bytes = extracted.getValue(filename)
            require(bytes.size.toLong() == item.first && sha256(bytes) == item.second.lowercase(Locale.ROOT)) {
                "Локальная проверка файла bundle не прошла: $filename"
            }
        }
        return name to extracted
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes).joinToString("") { "%02x".format(Locale.ROOT, it.toInt() and 0xff) }

    private class FtpSession(host: String, port: Int) : AutoCloseable {
        private val control = Socket()
        private val reader: BufferedReader
        private val writer: OutputStreamWriter
        private val serverHost = host

        init {
            control.connect(InetSocketAddress(host, port), TIMEOUT_MS)
            control.soTimeout = TIMEOUT_MS
            reader = BufferedReader(InputStreamReader(control.getInputStream(), Charsets.ISO_8859_1))
            writer = OutputStreamWriter(control.getOutputStream(), Charsets.ISO_8859_1)
            expect(readReply(), 220)
        }

        fun login(username: String, password: String) {
            val userReply = command("USER $username")
            if (userReply.code == 331) expect(command("PASS $password"), 230)
            else expect(userReply, 230)
        }

        fun commandChecked(line: String, expected: Int) = expect(command(line), expected)

        fun listNames(): Set<String> {
            val text = StringBuilder()
            try {
                dataInputCommand("NLST") { input ->
                    val dataReader = BufferedReader(InputStreamReader(input, Charsets.ISO_8859_1))
                    while (true) text.append(dataReader.readLine() ?: break).append('\n')
                }
            } catch (error: FtpReplyException) {
                // A number of PS3 FTP servers use 550 for an empty directory.
                if (error.code == 550) return emptySet()
                throw error
            }
            return text.lineSequence().map { it.trim().trimEnd('/') }
                .filter { it.isNotEmpty() }
                .map { it.substringAfterLast('/') }
                .toSet()
        }

        fun store(filename: String, bytes: ByteArray, onBytes: (Int) -> Unit) {
            dataSocketCommand("STOR $filename") { socket ->
                val output = socket.getOutputStream()
                var offset = 0
                while (offset < bytes.size) {
                    val count = minOf(64 * 1024, bytes.size - offset)
                    output.write(bytes, offset, count)
                    offset += count
                    onBytes(count)
                }
                output.flush()
            }
        }

        fun retrieve(filename: String, onBytes: (ByteArray) -> Unit): Long {
            var size = 0L
            dataInputCommand("RETR $filename") { input ->
                val buffer = ByteArray(64 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    size += count
                    onBytes(buffer.copyOf(count))
                }
            }
            return size
        }

        fun cleanupStage(stage: String, uploaded: List<String>) {
            try {
                commandChecked("CWD $REMOTE_LIBRARY", 250)
                for (filename in uploaded) {
                    try { commandChecked("DELE $stage/$filename", 250) } catch (_: Exception) { }
                }
                commandChecked("RMD $stage", 250)
            } catch (_: Exception) {
                // A lost control connection can leave only this hidden staging folder.
            }
        }

        private fun dataInputCommand(command: String, transfer: (java.io.InputStream) -> Unit) =
            dataSocketCommand(command) { socket -> transfer(socket.getInputStream()) }

        private fun dataSocketCommand(command: String, transfer: (Socket) -> Unit) {
            val data = passiveSocket()
            try {
                val preliminary = command(command)
                if (preliminary.code != 125 && preliminary.code != 150) {
                    throw FtpReplyException(preliminary.code,
                        "FTP-сервер отказал в передаче (код ${preliminary.code})")
                }
                data.use(transfer)
                val completion = readReply()
                if (completion.code != 226 && completion.code != 250) {
                    throw FtpReplyException(completion.code,
                        "FTP-сервер не подтвердил передачу (код ${completion.code})")
                }
            } catch (error: Exception) {
                try { data.close() } catch (_: Exception) { }
                throw error
            }
        }

        private fun passiveSocket(): Socket {
            val epsv = command("EPSV")
            if (epsv.code == 229) {
                val port = Regex("\\(\\|\\|\\|(\\d+)\\|\\)")
                    .find(epsv.message)?.groupValues?.get(1)?.toIntOrNull()
                    ?: error("Некорректный ответ EPSV от FTP-сервера")
                return openData(port)
            }
            val pasv = command("PASV")
            expect(pasv, 227)
            val values = Regex("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)")
                .find(pasv.message)?.groupValues?.drop(1)?.map { it.toInt() }
                ?: error("Некорректный ответ PASV от FTP-сервера")
            val port = values[4] * 256 + values[5]
            require(port in 1..65535) { "FTP-сервер вернул недопустимый порт данных" }
            // Ignore the PASV-advertised IP to avoid FTP bounce to another host.
            return openData(port)
        }

        private fun openData(port: Int): Socket = Socket().apply {
            soTimeout = TIMEOUT_MS
            connect(InetSocketAddress(serverHost, port), TIMEOUT_MS)
        }

        private data class Reply(val code: Int, val message: String)

        private fun command(line: String): Reply {
            require(line.none { it == '\r' || it == '\n' || it == '\u0000' }) {
                "Недопустимая команда FTP"
            }
            writer.write(line + "\r\n")
            writer.flush()
            return readReply()
        }

        private fun readReply(): Reply {
            val first = reader.readLine() ?: error("Соединение с FTP-сервером закрыто")
            require(first.length >= 3 && first.take(3).all { it.isDigit() }) {
                "FTP-сервер вернул некорректный ответ"
            }
            val code = first.take(3).toInt()
            val lines = mutableListOf(first)
            if (first.length > 3 && first[3] == '-') {
                while (true) {
                    val line = reader.readLine() ?: error("FTP-сервер закрыл многострочный ответ")
                    lines += line
                    if (line.startsWith("$code ")) break
                    require(lines.size < 100) { "Слишком длинный ответ FTP-сервера" }
                }
            }
            return Reply(code, lines.joinToString("\n"))
        }

        private fun expect(reply: Reply, expected: Int) {
            if (reply.code != expected) {
                throw FtpReplyException(reply.code,
                    "FTP-сервер ответил кодом ${reply.code}, ожидался $expected")
            }
        }

        private class FtpReplyException(val code: Int, message: String) :
            IllegalStateException(message)

        override fun close() {
            try { writer.write("QUIT\r\n"); writer.flush() } catch (_: Exception) { }
            try { control.close() } catch (_: Exception) { }
        }
    }
}
