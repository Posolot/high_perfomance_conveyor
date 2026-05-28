from fastapi import APIRouter

from Src.Logics.config_manager import config_manager
from Src.Models.buffer_model import BufferSizeConfig
from Src.Models.frame_model import FrameConfig
from Src.Models.ip_config_model import IpConfig
from Src.Models.runtime_model import RuntimeConfig
from Src.Models.stage_model import StageConfig

router = APIRouter(tags=["Sections"])


@router.get("/frame")
def get_frame():
    return {"frame": config_manager.snapshot()["frame"]}


@router.post("/frame")
def set_frame(payload: FrameConfig):
    config_manager.set_frame(payload)
    return {"ok": True, "frame": payload.model_dump()}


@router.get("/runtime")
def get_runtime():
    return {"runtime": config_manager.snapshot()["runtime"]}


@router.post("/runtime")
def set_runtime(payload: RuntimeConfig):
    config_manager.set_runtime(payload)
    return {"ok": True, "runtime": payload.model_dump()}


@router.get("/ipconfig")
def get_ipconfig():
    return {"ipconfig": config_manager.snapshot()["ipconfig"]}


@router.post("/ipconfig")
def set_ipconfig(payload: IpConfig):
    config_manager.set_ipconfig(payload)
    return {"ok": True, "ipconfig": payload.model_dump()}


@router.get("/buffer-size")
def get_buffer_size():
    return {"buffer_size": config_manager.snapshot()["buffer_size"]}


@router.post("/buffer-size")
def set_buffer_size(payload: BufferSizeConfig):
    config_manager.set_buffer_size(payload)
    return {"ok": True, "buffer_size": payload.buffer_size}


@router.get("/stages")
def get_stages():
    return {"stages": config_manager.snapshot()["stages"] or []}


@router.post("/stages")
def set_stages(payload: list[StageConfig]):
    config_manager.set_stages(payload)
    return {"ok": True, "stages": [stage.model_dump() for stage in payload]}