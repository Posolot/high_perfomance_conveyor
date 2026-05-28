from pydantic import BaseModel, field_validator

from Src.Core.validator import ConfigValidationError


class BufferSizeConfig(BaseModel):
    buffer_size: int

    @field_validator("buffer_size")
    @classmethod
    def validate_size(cls, value: int):
        if value <= 0:
            raise ConfigValidationError(
                message="buffer_size must be positive",
                error_type="INVALID_BUFFER_SIZE",
                error_code=1401,
                details={"buffer_size": value},
            )
        return value