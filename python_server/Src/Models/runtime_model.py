from pydantic import BaseModel, field_validator

from Src.Core.validator import ConfigValidationError


class RuntimeConfig(BaseModel):
    physical_cores: int
    logical_cpus: int
    cpu_budget: int

    @field_validator("physical_cores", "logical_cpus", "cpu_budget")
    @classmethod
    def validate_positive(cls, value: int):
        if value <= 0:
            raise ConfigValidationError(
                message="Runtime fields must be positive",
                error_type="INVALID_RUNTIME_FIELD",
                error_code=1201,
                details={"value": value},
            )
        return value

    @field_validator("cpu_budget")
    @classmethod
    def validate_budget(cls, value: int, info):
        logical_cpus = info.data.get("logical_cpus")
        if logical_cpus is not None and value > logical_cpus:
            raise ConfigValidationError(
                message="cpu_budget cannot exceed logical_cpus",
                error_type="INVALID_CPU_BUDGET",
                error_code=1202,
                details={"cpu_budget": value, "logical_cpus": logical_cpus},
            )
        return value