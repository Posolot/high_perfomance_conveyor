from pydantic import BaseModel, field_validator

from Src.Core.validator import ConfigValidationError


class FrameConfig(BaseModel):
    width: int
    height: int
    channels: int

    @field_validator("width", "height", "channels")
    @classmethod
    def validate_positive(cls, value: int):
        if value <= 0:
            raise ConfigValidationError(
                message="Frame fields must be positive",
                error_type="INVALID_FRAME_FIELD",
                error_code=1101,
                details={"value": value},
            )
        return value