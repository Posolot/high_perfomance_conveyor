from typing import Any, Dict, Optional

from pydantic import BaseModel, Field


class ApiResponse(BaseModel):
    ok: bool = True
    message: str


class ErrorResponse(BaseModel):
    ok: bool = False
    error_type: str
    error_code: int
    message: str
    details: Dict[str, Any] = Field(default_factory=dict)


class ConfigStateResponse(BaseModel):
    complete: bool
    missing_sections: list[str] = Field(default_factory=list)
    config: Optional[Dict[str, Any]] = None