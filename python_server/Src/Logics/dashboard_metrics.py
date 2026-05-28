import csv
import io
import json
import re
from pathlib import Path

from Src.Core.app_paths import CSV_DIR

from Src.Core.app_paths import CONFIGS_DIR
TIMESTAMP_RE = re.compile(r"_(\d{8}_\d{6})$")


def find_latest_csv() -> Path | None:
    """Возвращает путь к самому свежему CSV-файлу в директории configs."""
    candidates = list(CSV_DIR.glob("*.csv"))
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def config_path_for_csv(csv_path: Path) -> Path | None:
    """По имени CSV-файла находит соответствующий JSON-конфиг (без временной метки)."""
    stem = csv_path.stem
    m = TIMESTAMP_RE.search(stem)
    if m:
        base_stem = stem[:m.start()]
    else:
        base_stem = stem
    candidate = CONFIGS_DIR / f"{base_stem}.json"
    return candidate if candidate.exists() else None


def load_buffer_size(config_path: Path | None) -> int | None:
    """Из JSON-конфига читает buffer_size."""
    if config_path is None or not config_path.exists():
        return None
    try:
        data = json.loads(config_path.read_text(encoding="utf-8"))
        value = data.get("buffer_size")
        return int(value) if value is not None else None
    except Exception:
        return None


def tail_csv_rows(csv_path: Path, limit: int = 20) -> tuple[list[dict[str, str]], list[str]]:
    """
    Читает последние строки CSV, игнорируя незавершённую последнюю строку.
    Возвращает (список словарей, список заголовков).
    """
    try:
        text = csv_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return [], []

    if not text.strip():
        return [], []

    lines = text.splitlines()
    # Если файл не заканчивается на перевод строки – последняя строка, возможно, неполная
    if not text.endswith("\n") and lines:
        lines = lines[:-1]

    if not lines:
        return [], []

    # Пробуем распарсить все строки
    try:
        reader = csv.DictReader(io.StringIO("\n".join(lines)))
        rows = list(reader)
        return rows[-limit:], reader.fieldnames or []
    except csv.Error:
        # fallback: отрезаем по одной строке, пока парсинг не удастся
        for cut in range(1, min(5, len(lines)) + 1):
            try_lines = lines[:-cut]
            if not try_lines:
                break
            try:
                reader = csv.DictReader(io.StringIO("\n".join(try_lines)))
                rows = list(reader)
                return rows[-limit:], reader.fieldnames or []
            except csv.Error:
                continue
    return [], []


def stage_names_from_headers(headers: list[str]) -> list[str]:
    """Из заголовков CSV извлекает имена стадий (по суффиксу '_queue_depth')."""
    names = []
    suffix = "_queue_depth"
    for h in headers:
        if h.endswith(suffix):
            names.append(h[: -len(suffix)])
    return names