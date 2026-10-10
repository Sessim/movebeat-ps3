package io.movebeat.assistant

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.text.InputType
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private val createZipRequest = 1001
    private val openZipRequest = 1002
    private val createBundleRequest = 1003
    private var pendingBytes: ByteArray? = null
    private var preparedBundle: PreparedBundle? = null
    private lateinit var query: EditText
    private lateinit var key: EditText
    private lateinit var output: TextView
    private lateinit var results: LinearLayout
    private lateinit var ftpHost: EditText
    private lateinit var ftpPort: EditText
    private lateinit var ftpUsername: EditText
    private lateinit var ftpPassword: EditText

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        title = "MoveBeat Track Assistant"
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }
        content.addView(TextView(this).apply {
            text = "MoveBeat Track Assistant — Android feasibility prototype"
            textSize = 22f
        })
        query = EditText(this).apply { hint = "Название, автор или ключ" }
        content.addView(query, fullWidth())
        content.addView(Button(this).apply {
            text = "Найти на BeatSaver"
            setOnClickListener { runTask { showSearch(BeatSaverClient.search(query.text.toString())) } }
        }, fullWidth())
        results = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        content.addView(results, fullWidth())
        key = EditText(this).apply { hint = "Ключ выбранной карты" }
        content.addView(key, fullWidth())
        content.addView(Button(this).apply {
            text = "Скачать и подготовить уровень для PS3"
            setOnClickListener { runTask {
                val download = BeatSaverClient.download(key.text.toString().trim())
                offerBundle(BundleConverter.convert(download.bytes, download.key,
                    download.summary.contentSha1, download.summary.mapper))
            } }
        }, fullWidth())
        content.addView(Button(this).apply {
            text = "Подготовить локальный ZIP"
            setOnClickListener { startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "application/zip"
            }, openZipRequest) }
        }, fullWidth())
        content.addView(Button(this).apply {
            text = "Скачать и сохранить исходный ZIP"
            setOnClickListener { runTask { saveVerified(BeatSaverClient.download(key.text.toString().trim())) } }
        }, fullWidth())
        content.addView(TextView(this).apply {
            text = "Отправка готового уровня на PS3 по локальной сети"
            textSize = 18f
            setPadding(0, 24, 0, 4)
        }, fullWidth())
        ftpHost = EditText(this).apply {
            hint = "Адрес PS3 в локальной сети"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        ftpPort = EditText(this).apply {
            hint = "FTP-порт (21)"
            setText("21")
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        ftpUsername = EditText(this).apply {
            hint = "Имя пользователя FTP (anonymous)"
            setText("anonymous")
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
        }
        ftpPassword = EditText(this).apply {
            hint = "Пароль FTP (не сохраняется)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        content.addView(ftpHost, fullWidth())
        content.addView(ftpPort, fullWidth())
        content.addView(ftpUsername, fullWidth())
        content.addView(ftpPassword, fullWidth())
        content.addView(TextView(this).apply {
            text = "Обычный FTP передаёт пароль и файлы без шифрования. Используйте только доверенную локальную сеть."
            textSize = 13f
        }, fullWidth())
        content.addView(Button(this).apply {
            text = "Отправить подготовленный уровень на PS3"
            setOnClickListener { confirmFtpUpload() }
        }, fullWidth())
        output = TextView(this).apply {
            text = "Поиск по BeatSaver, подготовка уровня PS3 и сохранение через системный выбор файла."
            setPadding(0, 24, 0, 0)
        }
        content.addView(output, fullWidth())
        setContentView(ScrollView(this).apply { addView(content) })
    }

    private fun showSearch(items: List<SearchItem>) = runOnUiThread {
        results.removeAllViews()
        if (items.isEmpty()) {
            output.text = "BeatSaver не нашёл подходящих карт."
            return@runOnUiThread
        }
        output.text = "Найдено карт: ${items.size}. Нажмите на нужную, затем скачайте её."
        items.forEach { item ->
            results.addView(Button(this).apply {
                text = "${item.title}\n${item.mapper} · ${item.key}"
                contentDescription = "Выбрать ${item.title}, автор ${item.mapper}, ID ${item.key}"
                setOnClickListener {
                    key.setText(item.key)
                    output.text = "Выбрана карта: ${item.title} · ${item.mapper} · ${item.key}"
                }
            }, fullWidth())
        }
        // A direct ID/key search has one unambiguous result. Text search leaves
        // the choice explicit so the first result is never downloaded silently.
        val directKey = autoSelectDirectLookupKey(
            query.text.toString(), items.map { it.key })
        if (directKey != null) {
            key.setText(directKey)
            output.text = "Выбрана карта по ID: ${items.single().title} · $directKey"
        }
    }

    private fun saveVerified(download: VerifiedDownload) = runOnUiThread {
        pendingBytes = download.bytes
        output.text = "Проверено: ${download.summary.title}\n${download.summary.contentSha1}\n" +
            download.summary.difficulties.joinToString(", ")
        startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, "${download.key}.zip")
        }, createZipRequest)
    }

    private fun offerBundle(bundle: PreparedBundle) = runOnUiThread {
        preparedBundle = bundle
        pendingBytes = bundle.bytes
        output.text = "Готово: ${bundle.title}; сложностей: ${bundle.charts}. " +
            "Сохраните bundle ZIP или отправьте его на PS3 по FTP."
        startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, bundle.filename)
        }, createBundleRequest)
    }

    @Deprecated("Deprecated by Android; retained for minSdk compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == openZipRequest && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            runTask {
                val bytes = contentResolver.openInputStream(uri)?.use { input ->
                    val buffer = ByteArray(64 * 1024)
                    val result = java.io.ByteArrayOutputStream()
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        result.write(buffer, 0, count)
                        require(result.size() <= BeatSaverArchive.MAX_ARCHIVE_BYTES) { "ZIP exceeds 20 MiB" }
                    }
                    result.toByteArray()
                } ?: error("Cannot read the selected ZIP")
                offerBundle(BundleConverter.convert(bytes))
            }
        } else if ((requestCode == createZipRequest || requestCode == createBundleRequest) && resultCode == RESULT_OK) {
            val value = pendingBytes ?: return
            val uri = data?.data ?: return
            runTask {
                contentResolver.openOutputStream(uri, "w")?.use { it.write(value) }
                    ?: error("Cannot open the selected destination")
                runOnUiThread { output.text = "Файл сохранён." }
            }
            pendingBytes = null
        }
    }

    private fun runTask(block: () -> Unit) {
        output.text = "Выполняется…"
        Thread {
            try {
                block()
            } catch (error: Exception) {
                runOnUiThread { output.text = "Ошибка: ${error.message}" }
            }
        }.start()
    }

    private fun confirmFtpUpload() {
        val bundle = preparedBundle
        if (bundle == null) {
            output.text = "Сначала подготовьте уровень из BeatSaver ID или локального ZIP."
            return
        }
        AlertDialog.Builder(this)
            .setTitle("FTP не шифрует передачу")
            .setMessage("Пароль и файлы передаются открытым текстом. Отправляйте только по доверенной локальной сети.")
            .setNegativeButton("Отмена", null)
            .setPositiveButton("Отправить") { _, _ ->
                val host = ftpHost.text.toString().trim()
                val port = ftpPort.text.toString().toIntOrNull()
                    ?: run { output.text = "Введите числовой FTP-порт."; return@setPositiveButton }
                val username = ftpUsername.text.toString().trim().ifBlank { "anonymous" }
                val password = ftpPassword.text.toString()
                var lastProgress = 0L
                var stageText = ""
                runTask {
                    val remote = MoveBeatFtpUpload.upload(bundle, host, port, username, password,
                        stage = { text ->
                            stageText = text
                            runOnUiThread { output.text = "FTP: $text" }
                        }) { done, total ->
                        if (done == total || done - lastProgress >= 128 * 1024) {
                            lastProgress = done
                            val kib = 1024L
                            runOnUiThread {
                                output.text = if (total > 0)
                                    "FTP: $stageText\n${done / kib} / ${total / kib} КиБ"
                                    else "Проверка FTP-передачи…"
                            }
                        }
                    }
                    runOnUiThread {
                        ftpPassword.setText("")
                        output.text = "Уровень загружен и проверен на FTP-сервере. Если это PS3, перезапустите игру для обновления списка.\n$remote"
                    }
                }
            }
            .show()
    }

    private fun fullWidth() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
}
