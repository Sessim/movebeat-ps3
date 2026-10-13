# MoveBeat Track Assistant для Android

Ранний прототип Android-клиента; в релизы он пока не входит. Умеет:

- искать карты через BeatSaver API (по тексту, ID или `!bsr`) и скачивать их
  только с HTTPS-доменов BeatSaver, проверяя content SHA-1;
- импортировать локальный ZIP или карту по ключу и сохранять готовый
  MoveBeat bundle ZIP через системный диалог файлов (внутри — папка уровня:
  `level.ini`, `.mbchart`, Ogg/Vorbis, обложка, `library.mblc`, отчёт);
- отправлять уровень на FTP-сервер PS3: через скрытый staging-каталог, с
  обратным чтением и сверкой SHA-256 перед публикацией. Существующий уровень
  не заменяется, пароль не сохраняется, зависшая передача прерывается через
  30 секунд с указанием этапа. Обычный FTP не шифрует передачу — только
  доверенная сеть. По Wi-Fi приставки большие файлы могут не дойти, флешка
  с Windows-ассистентом надёжнее.

Конвертер переносит ноты, бомбы, стены, цепочки, дуги и смены BPM из v2/v3/v4,
по умолчанию берёт сложности `Standard` и отказывает при аудио не в
Ogg/Vorbis. Результат совпадает с ПК-конвертером: `tests.test_android_parity`
компилирует Kotlin-ядро на JVM и сравнивает `level.ini`, `.mbchart`, отчёт и
карточку с ПК-версией (нужны Kotlin CLI, Java и JAR `org.json:json`, путь в
`ORG_JSON_JAR`).

## Сборка

Нужны JDK 17+, Android SDK Platform 36 и Build Tools 36.1.0 (Android Gradle
Plugin 9.4.0, Gradle 9.6.0, `minSdk=26`).

```powershell
$env:ANDROID_HOME="$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat :app:assembleDebug --no-daemon --non-interactive
.\gradlew.bat :app:testDebugUnitTest --no-daemon --non-interactive
```

Результат — `app/build/outputs/apk/debug/app-debug.apk` (подписан отладочным
ключом, пакет `io.movebeat.assistant`).
