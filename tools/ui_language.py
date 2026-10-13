"""The language of the track assistant's messages: English or Russian.

The choice comes from MOVEBEAT_LANG ("en" or "ru"), then the one saved from
the assistant's language menu, then the Windows display language (Russian
Windows: Russian; anything else: English). Messages are written in place as
pairs: tr("Готово", "Done").
"""

from __future__ import annotations

import os
from pathlib import Path
import sys

LANGUAGES = ("en", "ru")


def settings_path() -> Path:
    base = os.environ.get("APPDATA")
    root = Path(base) if base else Path.home() / ".config"
    return root / "MoveBeat" / "language.txt"


def _windows_language() -> str:
    if sys.platform != "win32":
        return ""
    try:
        import ctypes
        language_id = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    except (AttributeError, OSError):
        return ""
    # The primary language is the low ten bits; 0x19 is Russian.
    return "ru" if language_id & 0x3FF == 0x19 else "en"


def detect_language() -> str:
    value = os.environ.get("MOVEBEAT_LANG", "").strip().casefold()
    if value in LANGUAGES:
        return value
    try:
        value = settings_path().read_text(encoding="utf-8").strip().casefold()
        if value in LANGUAGES:
            return value
    except OSError:
        pass
    return _windows_language() or "en"


_language = detect_language()


def language() -> str:
    return _language


def set_language(code: str, *, save: bool = True) -> None:
    """Switches the language; saved, it is used from the next start on."""
    global _language
    if code not in LANGUAGES:
        raise ValueError(f"unknown language: {code}")
    _language = code
    if save:
        path = settings_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(code + "\n", encoding="utf-8")


def tr(russian: str, english: str) -> str:
    return russian if _language == "ru" else english
