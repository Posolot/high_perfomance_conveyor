from fastapi import APIRouter
from fastapi.responses import FileResponse, JSONResponse
from pathlib import Path

from Src.Logics.dashboard_metrics import (
    find_latest_csv,
    config_path_for_csv,
    load_buffer_size,
    tail_csv_rows,
    stage_names_from_headers,
)

router = APIRouter(tags=["Dashboard"])


@router.get("/dashboard")
async def get_dashboard():
    """Возвращает HTML‑страницу дашборда."""
    # Путь к шаблону относительно корня проекта
    template_path = Path(__file__).resolve().parent.parent.parent / "templates" / "dashboard.html"
    if not template_path.exists():
        return JSONResponse(status_code=404, content={"error": "Dashboard template not found"})
    return FileResponse(template_path)


@router.get("/api/dashboard/metrics")
async def get_metrics():
    """Возвращает JSON с последними метриками для графиков."""
    csv_path = find_latest_csv()
    if csv_path is None:
        return {
            "ok": False,
            "message": "No CSV found yet",
            "csv": None,
            "buffer_size": None,
            "stage_names": [],
            "rows": [],
            "headers": [],
        }

    rows, headers = tail_csv_rows(csv_path, limit=20)
    config_path = config_path_for_csv(csv_path)
    buffer_size = load_buffer_size(config_path)
    stage_names = stage_names_from_headers(headers)

    return {
        "ok": True,
        "csv": csv_path.name,
        "config": config_path.name if config_path else None,
        "buffer_size": buffer_size,
        "stage_names": stage_names,
        "rows": rows,
        "headers": headers,
    }