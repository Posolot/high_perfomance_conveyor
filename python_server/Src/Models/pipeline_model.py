from typing import List

from pydantic import BaseModel

from Src.Models.frame_model import FrameConfig
from Src.Models.runtime_model import RuntimeConfig
from Src.Models.ip_config_model import IpConfig
from Src.Models.stage_model import StageConfig


class PipelineConfig(BaseModel):
    frame: FrameConfig
    buffer_size: int
    runtime: RuntimeConfig
    ipconfig: IpConfig
    stages: List[StageConfig]