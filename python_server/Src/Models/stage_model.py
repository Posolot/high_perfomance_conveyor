from typing import List

from pydantic import BaseModel, Field, field_validator

from Src.Core.validator import ConfigValidationError


class StageConfig(BaseModel):
    name: str
    callable: str
    next: List[str] = Field(default_factory=list)

    initial_workers: int = 1
    min_workers: int = 1
    max_workers: int = 1

    @field_validator("name", "callable")
    @classmethod
    def validate_non_empty(cls, value: str):
        if not value.strip():
            raise ConfigValidationError(
                message="Stage name/callable cannot be empty",
                error_type="INVALID_STAGE_FIELD",
                error_code=1501,
            )
        return value

    @field_validator("initial_workers", "min_workers", "max_workers")
    @classmethod
    def validate_workers(cls, value: int):
        if value < 0:
            raise ConfigValidationError(
                message="Worker count cannot be negative",
                error_type="INVALID_WORKER_COUNT",
                error_code=1502,
                details={"value": value},
            )
        return value

    @field_validator("max_workers")
    @classmethod
    def validate_worker_bounds(cls, value: int, info):
        min_workers = info.data.get("min_workers")
        initial_workers = info.data.get("initial_workers")

        if min_workers is not None and value < min_workers:
            raise ConfigValidationError(
                message="max_workers cannot be less than min_workers",
                error_type="INVALID_WORKER_BOUNDS",
                error_code=1503,
                details={"max_workers": value, "min_workers": min_workers},
            )

        if initial_workers is not None and value < initial_workers:
            raise ConfigValidationError(
                message="max_workers cannot be less than initial_workers",
                error_type="INVALID_WORKER_BOUNDS",
                error_code=1504,
                details={"max_workers": value, "initial_workers": initial_workers},
            )

        return value