#!/usr/bin/env python3
"""Graphical Windows frontend for preparing MoveBeat level bundles."""

from __future__ import annotations

import io
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import traceback
from typing import Callable

if not getattr(sys, "frozen", False):
    project_parent = Path(__file__).resolve().parent.parent
    if str(project_parent) not in sys.path:
        sys.path.insert(0, str(project_parent))

import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import Image, ImageOps, ImageTk, UnidentifiedImageError

from tools.beatsaver_import import (
    ImportError as MapImportError,
    PreparedImport,
    SourceMetadata,
    commit_prepared_import,
    discard_prepared_import,
    download_cover_preview,
    download_archive_size,
    download_map,
    inspect_archive,
    prepare_archive,
    read_archive_cover_preview,
    search_maps,
)
from tools.starter_set import import_starter_set
from tools.ui_language import language, set_language, tr
from tools.validate_bundle import validate_bundle
from tools.ftp_upload import TransferError, upload_bundle
from tools.usb_export import (
    UsbExportError, check_ps3_volume, export_bundle, export_bundles,
    library_bundles, list_removable_drives, read_volume,
)
from assistant.ftp_profile import (
    default_profile_path, load_ftp_profile, save_ftp_profile,
)


APP_VERSION = "0.4.0"
APP_NAME = f"MoveBeat Track Assistant {APP_VERSION}"
MAX_PREVIEW_PIXELS = 16_000_000
LANGUAGE_NAMES = {"en": "English", "ru": "Русский"}


def zip_size_label() -> str:
    return tr("Размер ZIP: ", "ZIP size: ")


def make_cover_preview(raw: bytes) -> bytes:
    """Decode an untrusted cover off the UI thread and return a tiny PNG."""
    try:
        with Image.open(io.BytesIO(raw)) as source:
            if source.width * source.height > MAX_PREVIEW_PIXELS:
                raise ValueError("cover image is too large")
            thumbnail = ImageOps.contain(
                source.convert("RGB"), (144, 144), Image.Resampling.LANCZOS)
            output = io.BytesIO()
            thumbnail.save(output, format="PNG")
            return output.getvalue()
    except (OSError, ValueError, Image.DecompressionBombError,
            UnidentifiedImageError) as exc:
        raise MapImportError(f"cannot decode cover preview: {exc}") from exc


def find_project_root() -> Path | None:
    candidates = [Path.cwd()]
    if not getattr(sys, "frozen", False):
        candidates.append(Path(__file__).resolve().parent.parent)
    else:
        candidates.append(Path(sys.executable).resolve().parent)
    for candidate in candidates:
        for directory in (candidate, *candidate.parents):
            if ((directory / "tools" / "build_ps3.sh").is_file() and
                    (directory / "ps3" / "Makefile").is_file()):
                return directory
    return None


def default_output_root(project_root: Path | None) -> Path:
    if project_root is not None:
        return project_root / "out" / "levels"
    return Path.home() / "Documents" / "MoveBeat" / "levels"


def wsl_path(path: Path) -> str:
    result = subprocess.run(
        ["wsl.exe", "wslpath", "-a", str(path.resolve())],
        check=True, capture_output=True, text=True, timeout=15,
    )
    value = result.stdout.strip()
    if not value.startswith("/"):
        raise RuntimeError("WSL returned an invalid project path")
    return value


class AssistantApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_NAME)
        self.geometry("980x900")
        self.minsize(820, 820)
        self.project_root = find_project_root()
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.cancel_event = threading.Event()
        self.busy = False
        self.can_cancel = False
        self.search_results = []
        self.local_zip: Path | None = None
        self.cover_request_id = 0
        self.archive_size_request_id = 0
        self.cover_photo = None

        self.query_var = tk.StringVar()
        self.key_var = tk.StringVar()
        self.source_var = tk.StringVar(value=tr("Источник: BeatSaver", "Source: BeatSaver"))
        self.details_var = tk.StringVar(value=tr(
            "Выберите карту, чтобы увидеть сведения о версии.",
            "Pick a map to see the details of its version."))
        self.characteristic_var = tk.StringVar(value="Standard")
        self.difficulty_var = tk.StringVar()
        self.output_var = tk.StringVar(value=str(default_output_root(self.project_root)))
        self.status_var = tk.StringVar(value=tr("Готово", "Ready"))
        self.progress_var = tk.DoubleVar(value=0)
        self.ftp_bundle_var = tk.StringVar()
        self.ftp_host_var = tk.StringVar()
        self.ftp_port_var = tk.StringVar(value="21")
        self.ftp_user_var = tk.StringVar(value="anonymous")
        self.ftp_password_var = tk.StringVar()
        self.usb_drive_var = tk.StringVar()
        self.usb_status_var = tk.StringVar(value=tr(
            "Вставьте флешку и нажмите «Обновить».", "Plug in a USB stick and press Refresh."))
        self.usb_drives: dict[str, Path] = {}
        self.ftp_profile_path = default_profile_path()
        try:
            profile = load_ftp_profile(self.ftp_profile_path)
            if profile is not None:
                host, port, user = profile
                self.ftp_host_var.set(host)
                self.ftp_port_var.set(str(port))
                self.ftp_user_var.set(user)
        except (OSError, ValueError, TypeError):
            # A corrupt optional profile must never prevent map preparation.
            pass

        self._build_ui()
        self.after(80, self._drain_events)

    def _build_ui(self) -> None:
        outer = ttk.Frame(self, padding=14)
        outer.pack(fill="both", expand=True)

        header = ttk.Frame(outer)
        header.pack(fill="x")
        ttk.Label(header, text=APP_NAME, font=("Segoe UI", 18, "bold")).pack(side="left")
        self.language_var = tk.StringVar(value=LANGUAGE_NAMES[language()])
        language_box = ttk.Combobox(
            header, textvariable=self.language_var, values=tuple(LANGUAGE_NAMES.values()),
            width=10, state="readonly")
        language_box.pack(side="right")
        language_box.bind("<<ComboboxSelected>>", lambda _event: self.choose_language())
        ttk.Label(
            outer,
            text=tr("Подготовка совместимых уровней из BeatSaver или локального Beat Saber ZIP",
                    "Prepares MoveBeat levels from BeatSaver or a local Beat Saber ZIP"),
        ).pack(anchor="w", pady=(0, 12))

        search = ttk.LabelFrame(outer, text="BeatSaver", padding=10)
        search.pack(fill="x")
        entry = ttk.Entry(search, textvariable=self.query_var)
        entry.pack(side="left", fill="x", expand=True)
        entry.bind("<Return>", lambda _event: self.start_search())
        self.search_button = ttk.Button(search, text=tr("Найти", "Search"), command=self.start_search)
        self.search_button.pack(side="left", padx=(8, 0))
        ttk.Button(search, text=tr("Открыть локальный ZIP", "Open local ZIP"),
                   command=self.choose_zip).pack(
            side="left", padx=(8, 0))

        results_frame = ttk.Frame(outer)
        results_frame.pack(fill="x", pady=10)
        table_frame = ttk.Frame(results_frame)
        table_frame.pack(side="left", fill="both", expand=True)
        columns = ("key", "title", "mapper", "difficulties")
        self.results = ttk.Treeview(
            table_frame, columns=columns, show="headings", height=5)
        headings = (("key", tr("Ключ", "Key"), 65), ("title", tr("Название", "Title"), 185),
                    ("mapper", tr("Маппер", "Mapper"), 110),
                    ("difficulties", tr("Сложности", "Difficulties"), 230))
        for name, label, width in headings:
            self.results.heading(name, text=label)
            self.results.column(name, width=width, minwidth=60)
        results_scroll = ttk.Scrollbar(
            table_frame, orient="vertical", command=self.results.yview)
        self.results.configure(yscrollcommand=results_scroll.set)
        self.results.pack(side="left", fill="both", expand=True)
        results_scroll.pack(side="right", fill="y")
        self.results.bind("<<TreeviewSelect>>", self._select_search_result)
        preview_frame = ttk.LabelFrame(results_frame, text=tr("Обложка", "Cover"), padding=4)
        preview_frame.configure(width=164, height=150)
        preview_frame.pack_propagate(False)
        preview_frame.pack(side="right", fill="y", padx=(8, 0))
        self.cover_label = ttk.Label(
            preview_frame, text=tr("Выберите карту", "Pick a map"), anchor="center",
            justify="center", wraplength=140)
        self.cover_label.pack(fill="both", expand=True)
        ttk.Label(outer, textvariable=self.details_var, wraplength=920,
                  justify="left").pack(fill="x", pady=(0, 8))

        options = ttk.LabelFrame(outer, text=tr("Экспорт", "Export"), padding=10)
        options.pack(fill="x")
        ttk.Label(options, textvariable=self.source_var).grid(row=0, column=0, columnspan=4, sticky="w")
        ttk.Label(options, text=tr("Ключ BeatSaver:", "BeatSaver key:")).grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(options, textvariable=self.key_var, width=18).grid(row=1, column=1, sticky="ew", pady=(8, 0))
        ttk.Label(options, text=tr("Характеристика:", "Characteristic:")).grid(row=1, column=2, sticky="e", padx=(12, 4), pady=(8, 0))
        self.characteristic = ttk.Combobox(
            options, textvariable=self.characteristic_var, values=("Standard", "NoArrows", "*"), width=16)
        self.characteristic.grid(row=1, column=3, sticky="ew", pady=(8, 0))
        ttk.Label(options, text=tr("Сложность:", "Difficulty:")).grid(row=2, column=0, sticky="w", pady=(8, 0))
        self.difficulty = ttk.Combobox(
            options, textvariable=self.difficulty_var,
            values=("", "Easy", "Normal", "Hard", "Expert", "ExpertPlus"))
        self.difficulty.grid(row=2, column=1, sticky="ew", pady=(8, 0))
        ttk.Label(options, text=tr("Каталог уровней:", "Levels folder:")).grid(row=2, column=2, sticky="e", padx=(12, 4), pady=(8, 0))
        output_row = ttk.Frame(options)
        output_row.grid(row=2, column=3, sticky="ew", pady=(8, 0))
        ttk.Entry(output_row, textvariable=self.output_var).pack(side="left", fill="x", expand=True)
        ttk.Button(output_row, text="…", width=3, command=self.choose_output).pack(side="left", padx=(4, 0))
        options.columnconfigure(1, weight=1)
        options.columnconfigure(3, weight=3)

        transfer = ttk.LabelFrame(
            outer, text=tr("Отправка уровня на PS3 по FTP", "Send a level to the PS3 over FTP"),
            padding=10)
        transfer.pack(fill="x", pady=(10, 0))
        ttk.Label(transfer, text=tr("Уровень:", "Level:")).grid(row=0, column=0, sticky="w")
        self.ftp_bundle = ttk.Combobox(
            transfer, textvariable=self.ftp_bundle_var, width=20, state="readonly")
        self.ftp_bundle.grid(row=0, column=1, sticky="ew", padx=(4, 8))
        ttk.Button(transfer, text=tr("Обновить", "Refresh"), command=self.refresh_bundles).grid(
            row=0, column=2, padx=(0, 12))
        ttk.Label(transfer, text=tr("Адрес PS3:", "PS3 address:")).grid(row=0, column=3)
        ttk.Entry(transfer, textvariable=self.ftp_host_var, width=19).grid(
            row=0, column=4, sticky="ew", padx=(4, 8))
        ttk.Label(transfer, text=tr("Порт:", "Port:")).grid(row=0, column=5)
        ttk.Entry(transfer, textvariable=self.ftp_port_var, width=6).grid(
            row=0, column=6, padx=(4, 0))
        ttk.Label(transfer, text=tr("Пользователь:", "User:")).grid(row=1, column=0, pady=(8, 0))
        ttk.Entry(transfer, textvariable=self.ftp_user_var).grid(
            row=1, column=1, sticky="ew", padx=(4, 8), pady=(8, 0))
        ttk.Label(transfer, text=tr("Пароль:", "Password:")).grid(row=1, column=2, pady=(8, 0))
        ttk.Entry(transfer, textvariable=self.ftp_password_var, show="*").grid(
            row=1, column=3, columnspan=2, sticky="ew", padx=(4, 8), pady=(8, 0))
        self.ftp_button = ttk.Button(
            transfer, text=tr("Отправить на PS3", "Send to PS3"), command=self.start_ftp_upload)
        self.ftp_button.grid(row=1, column=5, columnspan=2, padx=(4, 0), pady=(8, 0))
        ttk.Button(transfer, text=tr("Сохранить адрес PS3", "Save PS3 address"),
                   command=self.save_ftp_connection).grid(
            row=2, column=0, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Label(transfer,
                  text=tr("Пароль не сохраняется. Обычный FTP не шифрует пароль и файлы.",
                          "The password is not saved. Plain FTP does not encrypt "
                          "the password or the files.")).grid(
            row=2, column=2, columnspan=5, sticky="w", pady=(8, 0))
        transfer.columnconfigure(1, weight=2)
        transfer.columnconfigure(4, weight=2)

        usb_frame = ttk.LabelFrame(
            outer, text=tr("Флешка для PS3 (уровень выбирается выше, в поле «Уровень»)",
                           "USB stick for the PS3 (pick the level above, in the Level field)"),
            padding=10)
        usb_frame.pack(fill="x", pady=(10, 0))
        ttk.Label(usb_frame, text=tr("Носитель:", "Drive:")).grid(row=0, column=0, sticky="w")
        self.usb_drive = ttk.Combobox(
            usb_frame, textvariable=self.usb_drive_var, width=42, state="readonly")
        self.usb_drive.grid(row=0, column=1, sticky="ew", padx=(4, 8))
        self.usb_drive.bind("<<ComboboxSelected>>", lambda _event: self._show_usb_status())
        ttk.Button(usb_frame, text=tr("Обновить", "Refresh"),
                   command=self.refresh_usb_drives).grid(
            row=0, column=2, padx=(0, 12))
        self.usb_button = ttk.Button(
            usb_frame, text=tr("Уровень на флешку", "Level to USB"),
            command=self.start_usb_export)
        self.usb_button.grid(row=0, column=3)
        self.usb_all_button = ttk.Button(
            usb_frame, text=tr("Все уровни на флешку", "All levels to USB"),
            command=self.start_usb_export_all)
        self.usb_all_button.grid(row=0, column=4, padx=(8, 0))
        ttk.Label(usb_frame, textvariable=self.usb_status_var, wraplength=900,
                  justify="left").grid(row=1, column=0, columnspan=5, sticky="w", pady=(8, 0))
        usb_frame.columnconfigure(1, weight=1)

        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=10)
        self.import_button = ttk.Button(actions, text=tr("Подготовить уровень", "Prepare level"),
                                        command=self.start_import)
        self.import_button.pack(side="left")
        self.cancel_button = ttk.Button(actions, text=tr("Отмена", "Cancel"), command=self.cancel,
                                        state="disabled")
        self.cancel_button.pack(side="left", padx=8)
        self.starter_button = ttk.Button(actions, text=tr("Стартовый набор", "Starter set"),
                                         command=self.start_starter_set)
        self.starter_button.pack(side="left", padx=(0, 8))
        self.pkg_button = ttk.Button(actions, text=tr("Собрать персональный PKG", "Build personal PKG"),
                                     command=self.start_pkg_build)
        self.pkg_button.pack(side="left")
        ttk.Button(actions, text=tr("Открыть каталог", "Open folder"),
                   command=self.open_output).pack(side="right")

        self.progress = ttk.Progressbar(outer, variable=self.progress_var, maximum=100)
        self.progress.pack(fill="x")
        ttk.Label(outer, textvariable=self.status_var).pack(anchor="w", pady=(4, 8))
        self.log = tk.Text(outer, height=5, wrap="word", state="disabled")
        self.log.pack(fill="both", expand=True)

        if self.project_root is None or shutil.which("wsl.exe") is None:
            self.pkg_button.configure(state="disabled")
            self._append_log(tr(
                "Сборка PKG недоступна: запустите приложение из каталога проекта с WSL/PS3DEV.\n",
                "PKG builds are unavailable: run the app from the project folder with "
                "WSL/PS3DEV.\n"))
        else:
            self._append_log(tr(f"Проект: {self.project_root}\n", f"Project: {self.project_root}\n"))
        self._append_log(tr(
            "Загружайте карты только с разрешения правообладателей и по правилам BeatSaver.\n",
            "Download maps only with the rightsholders' permission and under BeatSaver's "
            "rules.\n"))
        self.refresh_bundles()
        self.refresh_usb_drives()

    def _append_log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _set_busy(self, value: bool, status: str = "") -> None:
        self.busy = value
        state = "disabled" if value else "normal"
        self.search_button.configure(state=state)
        self.import_button.configure(state=state)
        self.ftp_button.configure(state=state)
        self.usb_button.configure(state=state)
        self.usb_all_button.configure(state=state)
        self.starter_button.configure(state=state)
        if self.project_root is not None and shutil.which("wsl.exe") is not None:
            self.pkg_button.configure(state=state)
        self.cancel_button.configure(
            state="normal" if value and self.can_cancel else "disabled")
        if status:
            self.status_var.set(status)
        if not value:
            self.progress.stop()
            self.progress.configure(mode="determinate")

    def _run(self, label: str, operation: Callable[[], object], *,
             cancellable: bool = False) -> None:
        if self.busy:
            return
        self.cancel_event.clear()
        self.can_cancel = cancellable
        self._set_busy(True, label)
        self.progress.configure(mode="indeterminate")
        self.progress.start(12)

        def worker() -> None:
            try:
                self.events.put(("result", operation()))
            except Exception as exc:
                self.events.put(("error", (exc, traceback.format_exc())))

        threading.Thread(target=worker, daemon=True).start()

    def _drain_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                terminal_event = kind in (
                    "search", "imported", "package", "uploaded", "exported", "starter",
                    "result", "error")
                if kind == "progress":
                    done, total = payload
                    if total:
                        self.progress.stop()
                        self.progress.configure(mode="determinate")
                        self.progress_var.set(min(100, done * 100 / total))
                        self.status_var.set(tr("Загрузка", "Download") +
                                            f": {done / 1048576:.1f} / {total / 1048576:.1f} MiB")
                    else:
                        self.status_var.set(tr("Загрузка", "Download") +
                                            f": {done / 1048576:.1f} MiB")
                elif kind == "log":
                    self._append_log(str(payload) + "\n")
                elif kind == "starter":
                    bundles, report = payload
                    self.refresh_bundles(select=bundles[0].name if bundles else None)
                    lines = [tr(f"Стартовый набор: скачано {len(report.imported)}, "
                                f"уже было {len(report.existing)}, ошибок {len(report.failed)}",
                                f"Starter set: {len(report.imported)} downloaded, "
                                f"{len(report.existing)} already there, "
                                f"{len(report.failed)} failed")]
                    lines += [f"  {label}: {reason}" for label, reason in report.failed]
                    self._append_log("\n".join(lines) + "\n")
                    messagebox.showinfo(
                        APP_NAME, "\n".join(lines) + tr(
                            "\n\nТеперь «Все уровни на флешку» — и вставьте флешку в PS3.",
                            "\n\nNow press All levels to USB and plug the stick into the PS3."))
                elif kind == "search":
                    self._show_search_results(payload)
                elif kind == "cover_preview":
                    self._show_cover_preview(payload)
                elif kind == "archive_size":
                    self._show_archive_size(payload)
                elif kind == "imported":
                    destination = payload
                    self.refresh_bundles(select=destination.name)
                    self._append_log(tr(f"Готов bundle: {destination}\n",
                                        f"Bundle ready: {destination}\n"))
                    messagebox.showinfo(APP_NAME, tr("Уровень подготовлен и проверен:",
                                                     "Level prepared and checked:") +
                                        f"\n{destination}")
                elif kind == "transfer_progress":
                    done, total = payload
                    self.progress.stop()
                    self.progress.configure(mode="determinate")
                    self.progress_var.set(min(100, done * 100 / total) if total else 0)
                    self.status_var.set(f"FTP: {done / 1048576:.1f} / {total / 1048576:.1f} MiB")
                elif kind == "usb_progress":
                    done, total = payload
                    self.progress.stop()
                    self.progress.configure(mode="determinate")
                    self.progress_var.set(min(100, done * 100 / total) if total else 0)
                    self.status_var.set(tr("Флешка", "USB") +
                                        f": {done / 1048576:.1f} / {total / 1048576:.1f} MiB")
                elif kind == "usb_count":
                    done, total = payload
                    self.progress.stop()
                    self.progress.configure(mode="determinate")
                    self.progress_var.set(min(100, done * 100 / total) if total else 0)
                    self.status_var.set(tr(f"Флешка: уровень {done} из {total}",
                                           f"USB: level {done} of {total}"))
                elif kind == "exported":
                    self._append_log(str(payload) + "\n")
                    self._show_usb_status()
                    messagebox.showinfo(
                        APP_NAME, str(payload) + tr(
                            "\n\nВставьте флешку в PS3: игра скопирует уровни в библиотеку "
                            "(можно при открытом списке треков).",
                            "\n\nPlug the stick into the PS3: the game copies the levels into "
                            "its library (the track list may already be open)."))
                elif kind == "uploaded":
                    self._append_log(tr(f"Уровень отправлен: {payload}\n",
                                        f"Level sent: {payload}\n"))
                    messagebox.showinfo(
                        APP_NAME, tr("Уровень загружен и проверен на PS3.\n"
                                     "Перезапустите игру, чтобы обновить список треков.\n",
                                     "The level is on the PS3 and checked.\n"
                                     "Restart the game to refresh the track list.\n") +
                        str(payload))
                elif kind == "package":
                    package, output = payload
                    self._append_log(output + "\n")
                    messagebox.showinfo(APP_NAME, tr("Персональный PKG собран:",
                                                     "Personal PKG built:") + f"\n{package}")
                elif kind == "result":
                    if (isinstance(payload, tuple) and len(payload) == 2 and
                            isinstance(payload[0], PreparedImport)):
                        terminal_event = False
                        self._set_busy(False, tr("Проверьте результат конвертации",
                                                 "Review the conversion"))
                        self._review_import(payload[0], payload[1])
                elif kind == "error":
                    exc, trace = payload
                    if isinstance(exc, (MapImportError, TransferError, UsbExportError)):
                        self._append_log(tr("Ошибка", "Error") + f": {exc}\n")
                    else:
                        self._append_log(trace + "\n")
                    messagebox.showerror(APP_NAME, str(exc))
                if terminal_event:
                    self.can_cancel = False
                    self._set_busy(False, tr("Готово", "Ready") if kind != "error"
                                   else tr("Ошибка", "Error"))
        except queue.Empty:
            pass
        self.after(80, self._drain_events)

    def start_search(self) -> None:
        query = self.query_var.get().strip()
        if not query:
            messagebox.showwarning(APP_NAME, tr(
                "Введите название, автора, ID или команду !bsr.",
                "Enter a title, an author, an ID or a !bsr command."))
            return

        def operation() -> None:
            results = search_maps(query, 20)
            self.events.put(("search", results))

        self._run(tr("Поиск BeatSaver…", "Searching BeatSaver…"), operation)

    def _show_search_results(self, results: object) -> None:
        self.search_results = list(results)
        self.cover_request_id += 1
        self.archive_size_request_id += 1
        self.results.delete(*self.results.get_children())
        self._set_cover_placeholder(tr("Выберите карту", "Pick a map"))
        for index, result in enumerate(self.search_results):
            self.results.insert("", "end", iid=str(index), values=(
                result.key, result.title, result.mapper, ", ".join(result.difficulties)))
        if len(self.search_results) == 1:
            self.results.selection_set("0")
            self.results.focus("0")
            self._select_search_result()
        self.status_var.set(tr("Найдено", "Found") + f": {len(self.search_results)}")

    def _select_search_result(self, _event: object = None) -> None:
        selected = self.results.selection()
        if not selected:
            return
        result = self.search_results[int(selected[0])]
        self.local_zip = None
        self.key_var.set(result.key)
        self.source_var.set(tr("Источник", "Source") + f": BeatSaver {result.key} — {result.title}")
        details = []
        if result.song_author:
            details.append(tr("Исполнитель", "Artist") + f": {result.song_author}")
        if result.bpm is not None:
            details.append(f"BPM: {result.bpm:g}")
        if result.rating_percent is not None:
            vote_detail = (f"+{result.upvotes}/−{result.downvotes}"
                           if result.upvotes + result.downvotes
                           else tr(f"{result.rating_count} голосов",
                                   f"{result.rating_count} votes"))
            details.append(tr("Рейтинг", "Rating") +
                           f": {result.rating_percent:.1f}% ({vote_detail})")
        if result.downloads:
            details.append(tr("Скачиваний", "Downloads") +
                           f": {result.downloads:,}".replace(",", " "))
        if result.plays:
            details.append(tr("Проигрываний", "Plays") + f": {result.plays:,}".replace(",", " "))
        if result.updated_at:
            details.append(tr("Версия от", "Version of") + f": {result.updated_at[:10]}")
        if result.download_url:
            details.append(zip_size_label() + tr("проверяется…", "checking…"))
        self.details_var.set(" · ".join(details) or tr(
            "BeatSaver не сообщил дополнительные сведения о версии.",
            "BeatSaver gave no further details of this version."))
        self._request_cover_preview("beatsaver", result.cover_url)
        self._request_archive_size(result.download_url)
        pairs = [value.split(":", 1) for value in result.difficulties if ":" in value]
        characteristics = sorted({pair[0] for pair in pairs})
        difficulties = [pair[1] for pair in pairs]
        if characteristics:
            self.characteristic.configure(values=tuple(characteristics) + ("*",))
            self.characteristic_var.set(characteristics[0])
        self.difficulty.configure(values=("", *dict.fromkeys(difficulties)))
        self.difficulty_var.set("")

    def choose_zip(self) -> None:
        value = filedialog.askopenfilename(
            title=tr("Выберите Beat Saber ZIP", "Pick a Beat Saber ZIP"), filetypes=(("ZIP archive", "*.zip"), ("All files", "*.*")))
        if not value:
            return
        path = Path(value)
        try:
            inspection = inspect_archive(path)
        except (OSError, MapImportError) as exc:
            messagebox.showerror(APP_NAME, str(exc))
            return
        self.local_zip = path
        self.archive_size_request_id += 1
        self.key_var.set("")
        self.source_var.set(tr("Источник", "Source") + f": {path.name} — {inspection.title}")
        characteristics = sorted({item.characteristic for item in inspection.difficulties})
        names = [item.name for item in inspection.difficulties]
        self.characteristic.configure(values=tuple(characteristics) + ("*",))
        self.characteristic_var.set(characteristics[0])
        self.difficulty.configure(values=("", *dict.fromkeys(names)))
        self.difficulty_var.set("")
        unknown = tr("не указан", "not given")
        self.details_var.set(
            tr("Исполнитель", "Artist") + f": {inspection.song_author or unknown} · " +
            tr("Маппер", "Mapper") + f": {inspection.mapper or unknown} · "
            f"BPM: {inspection.bpm:g} · " +
            zip_size_label() + f"{path.stat().st_size / 1048576:.2f} MiB")
        self._request_cover_preview("local_zip", path)
        self._append_log(
            f"ZIP: {inspection.title}; " + tr("автор", "artist") +
            f" {inspection.song_author}; BPM {inspection.bpm:g}\n")

    def _set_cover_placeholder(self, text: str) -> None:
        self.cover_photo = None
        self.cover_label.configure(image="", text=text)

    def _request_cover_preview(self, source_kind: str, source: object) -> None:
        self.cover_request_id += 1
        request_id = self.cover_request_id
        self._set_cover_placeholder(tr("Загрузка обложки…", "Loading the cover…"))
        if source_kind == "beatsaver" and not source:
            self._set_cover_placeholder(tr("Обложка не указана", "No cover"))
            return

        def worker() -> None:
            try:
                if source_kind == "beatsaver":
                    raw = download_cover_preview(str(source))
                else:
                    raw = read_archive_cover_preview(Path(source))
                preview = make_cover_preview(raw) if raw else b""
                self.events.put(("cover_preview", (request_id, preview, "")))
            except Exception as exc:
                self.events.put(("cover_preview", (request_id, b"", str(exc))))

        threading.Thread(target=worker, daemon=True).start()

    def _request_archive_size(self, url: str) -> None:
        self.archive_size_request_id += 1
        request_id = self.archive_size_request_id
        if not url:
            return

        def worker() -> None:
            try:
                size = download_archive_size(url)
                self.events.put(("archive_size", (request_id, size, "")))
            except Exception as exc:
                self.events.put(("archive_size", (request_id, None, str(exc))))

        threading.Thread(target=worker, daemon=True).start()

    def _show_archive_size(self, payload: object) -> None:
        request_id, size, error = payload
        if request_id != self.archive_size_request_id:
            return
        details = [item for item in self.details_var.get().split(" · ")
                   if not item.startswith(zip_size_label())]
        if size is None:
            label = zip_size_label() + (tr("неизвестен", "unknown") if not error
                                        else tr("не удалось проверить", "could not check"))
        elif size > 20 * 1024 * 1024:
            label = zip_size_label() + f"{size / 1048576:.2f} MiB " + tr(
                "(выше лимита 20 МиБ)", "(over the 20 MiB limit)")
        else:
            label = zip_size_label() + f"{size / 1048576:.2f} MiB"
        details.append(label)
        self.details_var.set(" · ".join(details))

    def _show_cover_preview(self, payload: object) -> None:
        request_id, image_data, error = payload
        if request_id != self.cover_request_id:
            return
        if error:
            self._set_cover_placeholder(tr("Предпросмотр недоступен", "No preview"))
            return
        if not image_data:
            self._set_cover_placeholder(tr("Обложка не указана", "No cover"))
            return
        try:
            with Image.open(io.BytesIO(image_data)) as preview:
                self.cover_photo = ImageTk.PhotoImage(preview.copy())
        except (OSError, ValueError, Image.DecompressionBombError,
                UnidentifiedImageError):
            self._set_cover_placeholder(tr("Предпросмотр недоступен", "No preview"))
            return
        self.cover_label.configure(image=self.cover_photo, text="")

    def choose_output(self) -> None:
        value = filedialog.askdirectory(title=tr("Каталог библиотеки MoveBeat",
                                                 "MoveBeat library folder"))
        if value:
            self.output_var.set(value)
            self.refresh_bundles()

    def refresh_bundles(self, *, select: str | None = None) -> None:
        root = Path(self.output_var.get()).expanduser()
        names = sorted((path.name for path in root.iterdir()
                        if path.is_dir() and not path.name.startswith(".")),
                       key=str.casefold) if root.is_dir() else []
        self.ftp_bundle.configure(values=names)
        choice = select or self.ftp_bundle_var.get()
        self.ftp_bundle_var.set(choice if choice in names else (names[0] if names else ""))

    def start_ftp_upload(self) -> None:
        name = self.ftp_bundle_var.get().strip()
        host = self.ftp_host_var.get().strip()
        try:
            port = int(self.ftp_port_var.get().strip())
        except ValueError:
            messagebox.showwarning(APP_NAME, tr("Введите числовой FTP-порт.",
                                                "Enter a numeric FTP port."))
            return
        if not name or not host or not 1 <= port <= 65535:
            messagebox.showwarning(APP_NAME, tr(
                "Выберите уровень и укажите адрес и порт PS3.",
                "Pick a level and enter the PS3's address and port."))
            return
        directory = Path(self.output_var.get()).expanduser() / name
        user = self.ftp_user_var.get().strip() or "anonymous"
        password = self.ftp_password_var.get()
        if not messagebox.askyesno(
            APP_NAME,
            tr(f"Отправить {name} на {host}:{port}?\n\n"
               "Обычный FTP не шифрует пароль и содержимое передачи. "
               "Используйте только доверенную локальную сеть. "
               "Существующий уровень заменён не будет.",
               f"Send {name} to {host}:{port}?\n\n"
               "Plain FTP encrypts neither the password nor the files. "
               "Use a trusted local network only. "
               "A level already on the PS3 is not replaced."),
        ):
            return
        self.ftp_password_var.set("")

        def operation() -> None:
            remote = upload_bundle(
                directory, host, port, user, password,
                progress=lambda done, total: self.events.put(
                    ("transfer_progress", (done, total))))
            self.events.put(("uploaded", remote))

        self._run(tr("Отправка уровня на PS3…", "Sending the level to the PS3…"), operation)

    def refresh_usb_drives(self) -> None:
        self.usb_drives = {}
        for root in list_removable_drives():
            try:
                volume = read_volume(root)
                label = (f"{root}  {volume.label or tr('без метки', 'no label')}, "
                         f"{volume.filesystem}, " + tr("свободно", "free") +
                         f" {volume.free_bytes / 1073741824:.1f} GB")
            except (OSError, UsbExportError):
                label = f"{root}  " + tr("(не читается)", "(unreadable)")
            self.usb_drives[label] = root
        names = list(self.usb_drives)
        self.usb_drive.configure(values=names)
        current = self.usb_drive_var.get()
        self.usb_drive_var.set(current if current in names else (names[0] if names else ""))
        self._show_usb_status()

    def _show_usb_status(self) -> None:
        root = self.usb_drives.get(self.usb_drive_var.get())
        if root is None:
            self.usb_status_var.set(tr(
                "Съёмных носителей не найдено. Вставьте флешку и нажмите «Обновить».",
                "No removable drives found. Plug in a USB stick and press Refresh."))
            return
        try:
            volume = read_volume(root)
            check_ps3_volume(volume)
        except (OSError, UsbExportError) as exc:
            self.usb_status_var.set(str(exc))
            return
        library = root / "MOVEBEAT" / "levels"
        present = len(library_bundles(library))
        self.usb_status_var.set(tr(
            f"{volume.filesystem}: PS3 прочитает. Уровни кладутся в {library}"
            f" (сейчас там {present}). В PS3 игра сама скопирует их в библиотеку.",
            f"{volume.filesystem}: the PS3 can read it. Levels go to {library}"
            f" ({present} there now). On the PS3 the game copies them into its library."))

    def _usb_target(self) -> Path | None:
        root = self.usb_drives.get(self.usb_drive_var.get())
        if root is None:
            messagebox.showwarning(APP_NAME, tr("Вставьте флешку и нажмите «Обновить».",
                                                "Plug in a USB stick and press Refresh."))
        return root

    def start_usb_export(self) -> None:
        drive = self._usb_target()
        name = self.ftp_bundle_var.get().strip()
        if drive is None:
            return
        if not name:
            messagebox.showwarning(APP_NAME, tr("Выберите уровень в поле «Уровень».",
                                                "Pick a level in the Level field."))
            return
        directory = Path(self.output_var.get()).expanduser() / name

        def operation() -> None:
            destination = export_bundle(
                directory, drive,
                progress=lambda done, total: self.events.put(("usb_progress", (done, total))))
            self.events.put(("exported", tr("Уровень скопирован и проверен",
                                            "Level copied and checked") + f": {destination}"))

        self._run(tr("Копирование на флешку…", "Copying to the USB stick…"), operation)

    def start_usb_export_all(self) -> None:
        drive = self._usb_target()
        if drive is None:
            return
        bundles = library_bundles(Path(self.output_var.get()).expanduser())
        if not bundles:
            messagebox.showwarning(APP_NAME, tr("В каталоге уровней нет подготовленных уровней.",
                                                "The levels folder has no prepared levels."))
            return
        if not messagebox.askyesno(
                APP_NAME, tr(f"Скопировать на {drive} все уровни и плей-листы ({len(bundles)})?\n"
                             "Уже записанные на флешку пропускаются, ничего не заменяется.",
                             f"Copy all levels and playlists ({len(bundles)}) to {drive}?\n"
                             "Ones already on the stick are skipped; nothing is replaced.")):
            return

        def operation() -> None:
            report = export_bundles(
                bundles, drive,
                progress=lambda done, total: self.events.put(("usb_count", (done, total))))
            lines = [tr(f"Скопировано: {len(report.copied)}, пропущено: {len(report.skipped)}",
                        f"Copied: {len(report.copied)}, skipped: {len(report.skipped)}")]
            lines += [f"  {name}: {reason}" for name, reason in report.skipped[:12]]
            if len(report.skipped) > 12:
                lines.append(tr(f"  … и ещё {len(report.skipped) - 12}",
                                f"  … and {len(report.skipped) - 12} more"))
            self.events.put(("exported", "\n".join(lines)))

        self._run(tr("Копирование уровней на флешку…", "Copying levels to the USB stick…"),
                  operation)

    def start_starter_set(self) -> None:
        output_root = Path(self.output_var.get()).expanduser()

        def operation() -> None:
            bundles, report = import_starter_set(
                output_root, log=lambda line: self.events.put(("log", line)))
            self.events.put(("starter", (bundles, report)))

        self._run(tr("Скачивание стартового набора…", "Downloading the starter set…"), operation)

    def choose_language(self) -> None:
        names = {name: code for code, name in LANGUAGE_NAMES.items()}
        code = names.get(self.language_var.get())
        if code is None or code == language():
            return
        try:
            set_language(code)
        except OSError as exc:
            messagebox.showerror(APP_NAME, str(exc))
            return
        messagebox.showinfo(APP_NAME, tr("Перезапустите ассистент, чтобы сменить язык.",
                                         "Restart the assistant to switch the language."))

    def save_ftp_connection(self) -> None:
        try:
            port = int(self.ftp_port_var.get().strip())
            save_ftp_profile(self.ftp_profile_path, self.ftp_host_var.get(),
                             port, self.ftp_user_var.get())
        except (OSError, ValueError) as exc:
            messagebox.showerror(APP_NAME, str(exc))
            return
        self._append_log(tr(f"Адрес PS3 сохранён: {self.ftp_profile_path}; пароль не сохранён.\n",
                            f"PS3 address saved: {self.ftp_profile_path}; the password is not "
                            "saved.\n"))

    def start_import(self) -> None:
        output_root = Path(self.output_var.get()).expanduser()
        characteristic = None if self.characteristic_var.get() == "*" else self.characteristic_var.get().strip()
        difficulty = self.difficulty_var.get().strip() or None
        local_zip = self.local_zip
        key = self.key_var.get().strip()
        if local_zip is None and not key:
            messagebox.showwarning(APP_NAME, tr(
                "Выберите результат BeatSaver, введите ключ или откройте ZIP.",
                "Pick a BeatSaver result, enter a key or open a ZIP."))
            return

        def operation() -> tuple[PreparedImport, dict]:
            temporary: Path | None = None
            try:
                source = SourceMetadata()
                zip_path = local_zip
                if zip_path is None:
                    handle = tempfile.NamedTemporaryFile(prefix="movebeat-", suffix=".zip", delete=False)
                    handle.close()
                    temporary = Path(handle.name)
                    source = download_map(
                        key, temporary,
                        progress=lambda done, total: self.events.put(("progress", (done, total))),
                        cancelled=self.cancel_event.is_set,
                    )
                    zip_path = temporary
                if self.cancel_event.is_set():
                    raise MapImportError(tr("Операция отменена", "Cancelled"))
                prepared = prepare_archive(
                    zip_path, output_root, characteristic=characteristic,
                    difficulty_name=difficulty, source=source, force=False)
                try:
                    errors = validate_bundle(prepared.staging)
                    if errors:
                        raise MapImportError(
                            tr("Bundle не прошёл проверку: ", "The bundle failed its check: ") +
                            "; ".join(errors))
                    report_path = prepared.staging / "conversion-report.json"
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    if self.cancel_event.is_set():
                        raise MapImportError(tr("Операция отменена", "Cancelled"))
                    return prepared, report
                except Exception:
                    discard_prepared_import(prepared)
                    raise
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)

        self._run(tr("Подготовка уровня…", "Preparing the level…"), operation, cancellable=True)

    def _review_import(self, prepared: PreparedImport, report: dict) -> None:
        level = report.get("level", {})
        charts = report.get("charts", [])
        warnings = report.get("warnings", [])
        lines = [
            tr("Трек", "Track") + f": {level.get('title') or prepared.destination.name}",
            tr("Состав подготовленного набора:", "The prepared bundle:"),
        ]
        for chart in charts:
            lines.append(
                f"• {chart.get('characteristic', 'Standard')}/"
                f"{chart.get('difficulty', '?')}: "
                + tr(f"ноты {chart.get('notes', 0)}, стены {chart.get('obstacles', 0)}, "
                     f"цепочки {chart.get('chains', 0)}, дуги {chart.get('arcs', 0)}, "
                     f"пропущено {chart.get('skipped', 0)}",
                     f"notes {chart.get('notes', 0)}, walls {chart.get('obstacles', 0)}, "
                     f"chains {chart.get('chains', 0)}, arcs {chart.get('arcs', 0)}, "
                     f"skipped {chart.get('skipped', 0)}"))
        if warnings:
            lines.append(tr("\nПредупреждения:", "\nWarnings:"))
            lines.extend(f"• {warning}" for warning in warnings[:20])
            if len(warnings) > 20:
                lines.append(tr(f"…и ещё {len(warnings) - 20}.",
                                f"…and {len(warnings) - 20} more."))
        else:
            lines.append(tr("\nПредупреждений: нет.", "\nNo warnings."))
        lines.append(tr("\nСохранить проверенный набор в библиотеку уровней?",
                        "\nSave the checked bundle to the levels library?"))
        if not self._confirm_import_review("\n".join(lines)):
            try:
                discard_prepared_import(prepared)
                self._append_log(tr("Импорт отменён после просмотра результата.\n",
                                    "Import cancelled after the review.\n"))
                self.status_var.set(tr("Импорт отменён", "Import cancelled"))
            except OSError as exc:
                self._append_log(tr("Не удалось удалить временный набор",
                                    "Could not delete the temporary bundle") + f": {exc}\n")
                messagebox.showerror(APP_NAME, str(exc))
            return

        def operation() -> None:
            try:
                destination = commit_prepared_import(prepared)
            except Exception:
                discard_prepared_import(prepared)
                raise
            self.events.put(("imported", destination))

        self._run(tr("Сохранение проверенного набора…", "Saving the checked bundle…"), operation)

    def _confirm_import_review(self, summary: str) -> bool:
        dialog = tk.Toplevel(self)
        dialog.title(tr("Проверка конвертации", "Conversion review"))
        dialog.transient(self)
        dialog.geometry("760x520")
        dialog.minsize(620, 360)
        dialog.grab_set()
        decision = {"save": False}

        body = ttk.Frame(dialog, padding=12)
        body.pack(fill="both", expand=True)
        text = tk.Text(body, wrap="word", state="normal", height=18)
        scrollbar = ttk.Scrollbar(body, orient="vertical", command=text.yview)
        text.configure(yscrollcommand=scrollbar.set)
        text.insert("1.0", summary)
        text.configure(state="disabled")
        text.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        buttons = ttk.Frame(dialog, padding=(12, 0, 12, 12))
        buttons.pack(fill="x")
        ttk.Button(buttons, text=tr("Отменить импорт", "Cancel import"),
                   command=dialog.destroy).pack(side="right")

        def save() -> None:
            decision["save"] = True
            dialog.destroy()

        ttk.Button(buttons, text=tr("Сохранить набор", "Save bundle"),
                   command=save).pack(side="right", padx=(0, 8))
        dialog.protocol("WM_DELETE_WINDOW", dialog.destroy)
        dialog.wait_visibility()
        dialog.focus_set()
        self.wait_window(dialog)
        return decision["save"]

    def start_pkg_build(self) -> None:
        if self.project_root is None:
            return
        expected_library = (self.project_root / "out" / "levels").resolve()
        if Path(self.output_var.get()).expanduser().resolve() != expected_library:
            messagebox.showwarning(
                APP_NAME,
                tr("Для сборки PKG выберите каталог уровней проекта:",
                   "To build a PKG, pick the project's levels folder:") +
                f"\n{expected_library}")
            return

        def operation() -> None:
            linux_root = wsl_path(self.project_root)
            command = f"cd {subprocess.list2cmdline([linux_root])} && ./tools/build_ps3.sh --include-imported"
            result = subprocess.run(
                ["wsl.exe", "bash", "-lc", command], capture_output=True,
                text=True, timeout=30 * 60,
            )
            output = (result.stdout + "\n" + result.stderr).strip()
            if result.returncode != 0:
                raise RuntimeError(tr("Сборка PKG завершилась с кодом",
                                      "The PKG build exited with code") +
                                   f" {result.returncode}\n{output}")
            package = self.project_root / "out" / "ps3" / "movebeat.pkg"
            if not package.is_file() or package.stat().st_size == 0:
                raise RuntimeError(tr("Сборка завершилась без movebeat.pkg",
                                      "The build finished without movebeat.pkg"))
            self.events.put(("package", (package, output)))

        self._run(tr("Сборка персонального PKG…", "Building the personal PKG…"), operation)

    def cancel(self) -> None:
        self.cancel_event.set()
        self.status_var.set(tr("Отмена запрошена…", "Cancelling…"))

    def open_output(self) -> None:
        path = Path(self.output_var.get()).expanduser()
        path.mkdir(parents=True, exist_ok=True)
        os.startfile(path)  # type: ignore[attr-defined]


def main() -> int:
    app = AssistantApp()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
