from __future__ import annotations

from enum import Enum
from typing import Any

from pydantic import BaseModel, ConfigDict


class StrictBaseModel(BaseModel):
    model_config = ConfigDict(
        extra="forbid",
        str_strip_whitespace=True,
        validate_assignment=True,
    )


class PipelineStatus(str, Enum):
    STOPPED = "stopped"
    RUNNING = "running"
    RESTARTING = "restarting"


class ApiResponse(StrictBaseModel):
    ok: bool
    message: str


class CurrentConfigResponse(StrictBaseModel):
    complete: bool
    config: dict[str, Any] | None = None
    missing_sections: list[str] = []


class PipelineStatusResponse(StrictBaseModel):
    state: PipelineStatus
    last_action: str
    updated_at: str
    missing_sections: list[str]


class SectionStateResponse(StrictBaseModel):
    section: str
    complete: bool
    data: dict[str, Any] | None = None