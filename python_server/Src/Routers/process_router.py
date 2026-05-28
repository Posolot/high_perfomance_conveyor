import subprocess
import signal
import os
from pathlib import Path
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from Src.Core.app_paths import BASE_DIR, CONFIGS_DIR

router = APIRouter(tags=["Process Control"])

# Глобальное состояние (для демонстрации; в проде лучше сохранять PID в файл)
_pipeline_process = None

class StartRequest(BaseModel):
    config_name: str   # имя файла в папке configs, например "my_config.json"


def _get_pipeline_binary() -> Path:
    """Путь к исполняемому файлу конвейера (в корне проекта)."""
    return BASE_DIR / "pipeline"


@router.post("/pipeline/start")
def start_pipeline(req: StartRequest):
    global _pipeline_process
    if _pipeline_process is not None and _pipeline_process.poll() is None:
        raise HTTPException(status_code=400, detail="Pipeline already running")

    # Проверка существования конфига
    config_path = CONFIGS_DIR / req.config_name
    if not config_path.exists():
        raise HTTPException(status_code=404, detail=f"Config file {req.config_name} not found")

    # Проверка бинарника
    binary = _get_pipeline_binary()
    if not binary.exists():
        raise HTTPException(status_code=500, detail=f"Pipeline binary not found at {binary}")

    # Подготовка команды (можно добавить --quiet)
    cmd = [str(binary), str(config_path)]
    # cmd.append("--quiet")   # раскомментировать если нужен тихий режим

    # Создаём папку для логов
    log_dir = BASE_DIR / "logs"
    log_dir.mkdir(exist_ok=True)
    log_file = log_dir / "pipeline.log"

    try:
        # Открываем файл лога на добавление
        f = open(log_file, "a")
        # Запускаем процесс в новой сессии (чтобы можно было убить всю группу)
        _pipeline_process = subprocess.Popen(
            cmd,
            stdout=f,
            stderr=subprocess.STDOUT,
            cwd=str(BASE_DIR),
            start_new_session=True   # позволяет убить процесс и его детей через os.killpg
        )
        return {
            "status": "started",
            "pid": _pipeline_process.pid,
            "config": req.config_name,
            "log_file": str(log_file)
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to start pipeline: {str(e)}")


@router.post("/pipeline/stop")
def stop_pipeline():
    global _pipeline_process
    if _pipeline_process is None or _pipeline_process.poll() is not None:
        _pipeline_process = None
        raise HTTPException(status_code=400, detail="Pipeline is not running")

    try:
        # Посылаем SIGTERM всей группе процессов
        os.killpg(os.getpgid(_pipeline_process.pid), signal.SIGTERM)
        # Ждём завершения до 5 секунд
        try:
            _pipeline_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            # Принудительно SIGKILL
            os.killpg(os.getpgid(_pipeline_process.pid), signal.SIGKILL)
            _pipeline_process.wait()
        _pipeline_process = None
        return {"status": "stopped"}
    except Exception as e:
        _pipeline_process = None
        raise HTTPException(status_code=500, detail=f"Failed to stop pipeline: {str(e)}")


@router.post("/pipeline/restart")
def restart_pipeline(req: StartRequest):
    # Останавливаем, если запущен, игнорируя ошибку "не запущен"
    try:
        stop_pipeline()
    except HTTPException as e:
        if e.status_code != 400:
            raise
    # Запускаем заново
    return start_pipeline(req)


@router.get("/pipeline/status")
def pipeline_status():
    global _pipeline_process
    if _pipeline_process is None:
        return {"running": False, "pid": None}
    poll = _pipeline_process.poll()
    if poll is None:
        return {"running": True, "pid": _pipeline_process.pid}
    else:
        # Процесс завершился, сбрасываем
        _pipeline_process = None
        return {"running": False, "pid": None, "exit_code": poll}


@router.get("/pipeline/logs")
def get_pipeline_logs(lines: int = 100):
    """Возвращает последние строки лога конвейера."""
    log_file = BASE_DIR / "logs" / "pipeline.log"
    if not log_file.exists():
        return {"logs": "", "message": "Log file not found"}
    try:
        with open(log_file, "r", encoding="utf-8", errors="replace") as f:
            all_lines = f.readlines()
        last_lines = all_lines[-lines:] if lines > 0 else []
        return {"logs": "".join(last_lines), "total_lines": len(all_lines)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to read log: {str(e)}")