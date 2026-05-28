from fastapi import APIRouter, HTTPException,Request

from Src.Core.validator import ConfigValidationError
from Src.Core.responces import ConfigStateResponse
from Src.Logics.config_manager import config_manager
from Src.Models.buffer_model import BufferSizeConfig
from Src.Models.frame_model import FrameConfig
from Src.Models.ip_config_model import IpConfig
from Src.Models.patch_model import ConfigPatch
from Src.Models.pipeline_model import PipelineConfig
from Src.Models.runtime_model import RuntimeConfig
from Src.Models.stage_model import StageConfig
from Src.Logics.configs_storage import ConfigStorage
from Src.Core.base_model import BaseModel

router = APIRouter(tags=["Config"])
config_storage = ConfigStorage()

class SaveConfigRequest(BaseModel):
    config_name: str

class LoadConfigRequest(BaseModel):
    filename: str

@router.get("/config/current", response_model=ConfigStateResponse)
def get_current_config():
    snapshot = config_manager.snapshot()
    missing = config_manager.missing_sections()

    try:
        config = config_manager.build().model_dump()
        return ConfigStateResponse(
            complete=True,
            missing_sections=[],
            config=config,
        )
    except Exception as exc:
        return ConfigStateResponse(
            complete=False,
            missing_sections=missing,
            config=snapshot,
        )


@router.post("/config/merge")
def merge_config(payload: ConfigPatch):
    config_manager.merge_patch(payload)
    return {
        "ok": True,
        "message": "Configuration patch merged",
        "missing_sections": config_manager.missing_sections(),
        "config": config_manager.snapshot(),
    }


@router.post("/config/import")
def import_full_config(payload: PipelineConfig):
    config_manager.set_frame(payload.frame)
    config_manager.set_buffer_size(BufferSizeConfig(buffer_size=payload.buffer_size))
    config_manager.set_runtime(payload.runtime)
    config_manager.set_ipconfig(payload.ipconfig)
    config_manager.set_stages(payload.stages)

    return {
        "ok": True,
        "message": "Full configuration imported",
        "config": payload.model_dump(),
    }


@router.get("/config/export")
def export_full_config():
    try:
        return {
            "ok": True,
            "config": config_manager.build().model_dump(),
        }
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@router.post("/config/validate")
def validate_config():
    config_manager.validate()
    return {
        "ok": True,
        "message": "Configuration is valid",
    }


@router.delete("/config/reset")
def reset_config():
    config_manager.reset()
    return {
        "ok": True,
        "message": "Configuration reset",
    }


@router.post("/save")
def save_current_config(payload: SaveConfigRequest):
    try:
        full_config = config_manager.build()
    except ConfigValidationError as e:
        raise HTTPException(status_code=400, detail=str(e))

    try:
        config_str = ConfigStorage()
        filename = config_str.saveConfig(full_config, payload.config_name)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Save error: {str(e)}")

    return {
        "message": "Config saved successfully",
        "file": filename
    }

@router.get("/config/example")
def example_config():
    return {
        "frame": {
            "width": 640,
            "height": 480,
            "channels": 1
        },
        "buffer_size": 250,
        "runtime": {
            "physical_cores": 4,
            "logical_cpus": 8,
            "cpu_budget": 8
        },
        "ipconfig": {
            "ip": "127.0.0.1",
            "port": 5558,
            "protocol": "tcp",
            "socket_type": "PULL"
        },
        "stages": [
            {
                "name": "gray_stage",
                "callable": "rgb2gray",
                "next": ["erode_stage", "blur_stage", "gaussian_stage"],
                "initial_workers": 1,
                "min_workers": 1,
                "max_workers": 10
            },
            {
                "name": "erode_stage",
                "callable": "erode",
                "next": ["merge_stage"],
                "initial_workers": 1,
                "min_workers": 1,
                "max_workers": 10
            },
            {
                "name": "blur_stage",
                "callable": "blur",
                "next": ["merge_stage"],
                "initial_workers": 1,
                "min_workers": 1,
                "max_workers": 10
            },
            {
                "name": "gaussian_stage",
                "callable": "blur",
                "next": ["merge_stage"],
                "initial_workers": 1,
                "min_workers": 1,
                "max_workers": 10
            },
            {
                "name": "merge_stage",
                "callable": "merge",
                "next": ["fft_stage"],
                "initial_workers": 1,
                "min_workers": 1,
                "max_workers": 10
            },
            {
                "name": "fft_stage",
                "callable": "fft",
                "next": [],
                "initial_workers": 2,
                "min_workers": 2,
                "max_workers": 10
            }
        ]
    }


@router.get("/config/list")
def list_configs():
    """
    Возвращает список всех JSON-файлов конфигурации в папке configs.
    """
    try:
        files = config_storage.getAllConfigs()
        return {
            "ok": True,
            "configs": files,
            "count": len(files)
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to list configs: {str(e)}")


@router.post("/config/load")
def load_config(payload: LoadConfigRequest):
    """
    Загружает указанный конфиг из файла и устанавливает его как текущий в config_manager.
    """
    filename = payload.filename.strip()
    filename = filename if filename.endswith(".json") else filename + ".json"
    if not filename:
        raise HTTPException(status_code=400, detail="Filename cannot be empty")

    # 1. Загружаем JSON из файла
    try:
        config_data = config_storage.loadConfig(filename)
    except FileNotFoundError:
        raise HTTPException(status_code=404, detail=f"Config file '{filename}' not found")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to load config: {str(e)}")

    # 2. Преобразуем JSON в объекты Pydantic-моделей (PipelineConfig)
    try:
        from Src.Models.pipeline_model import PipelineConfig
        pipeline_config = PipelineConfig.model_validate(config_data)
    except Exception as e:
        raise HTTPException(status_code=422, detail=f"Invalid config structure: {str(e)}")

    # 3. Устанавливаем каждый раздел в config_manager
    try:
        config_manager.set_frame(pipeline_config.frame)
        config_manager.set_buffer_size(
            BufferSizeConfig(buffer_size=pipeline_config.buffer_size)
        )
        config_manager.set_runtime(pipeline_config.runtime)
        config_manager.set_ipconfig(pipeline_config.ipconfig)
        config_manager.set_stages(pipeline_config.stages)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to apply config: {str(e)}")

    # 4. Опционально: проверяем, что конфиг валидный (целостность графа, циклы и т.д.)
    try:
        config_manager.validate()
    except ConfigValidationError as e:
        # Если невалидный, можно либо откатить, либо вернуть ошибку
        raise HTTPException(status_code=400, detail=f"Config validation failed: {str(e)}")

    return {
        "ok": True,
        "message": f"Config '{filename}' loaded and set as current",
        "config": config_manager.snapshot()
    }