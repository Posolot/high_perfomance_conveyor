from typing import List, Optional

from pydantic import BaseModel

from Src.Models.buffer_model import BufferSizeConfig
from Src.Models.frame_model import FrameConfig
from Src.Models.ip_config_model import IpConfig
from Src.Models.runtime_model import RuntimeConfig
from Src.Models.stage_model import StageConfig


class ConfigPatch(BaseModel):
    frame: Optional[FrameConfig] = None
    buffer_size: Optional[BufferSizeConfig] = None
    runtime: Optional[RuntimeConfig] = None
    ipconfig: Optional[IpConfig] = None
    stages: Optional[List[StageConfig]] = None