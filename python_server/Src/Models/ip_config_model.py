from pydantic import BaseModel, field_validator

from Src.Core.validator import ConfigValidationError


class IpConfig(BaseModel):
    ip: str
    port: int
    protocol: str = "tcp"
    socket_type: str = "PULL"

    @field_validator("ip")
    @classmethod
    def validate_ip(cls, value: str):
        if not value.strip():
            raise ConfigValidationError(
                message="IP address cannot be empty",
                error_type="INVALID_IP",
                error_code=1301,
            )
        return value

    @field_validator("port")
    @classmethod
    def validate_port(cls, value: int):
        if not (1 <= value <= 65535):
            raise ConfigValidationError(
                message="Port must be in range 1..65535",
                error_type="INVALID_PORT",
                error_code=1302,
                details={"port": value},
            )
        return value

    @field_validator("protocol")
    @classmethod
    def validate_protocol(cls, value: str):
        if value not in {"tcp", "ipc", "inproc"}:
            raise ConfigValidationError(
                message="Unsupported protocol",
                error_type="INVALID_PROTOCOL",
                error_code=1303,
                details={"protocol": value},
            )
        return value

    @field_validator("socket_type")
    @classmethod
    def validate_socket_type(cls, value: str):
        allowed = {"PULL", "PUSH", "PUB", "SUB", "REQ", "REP"}
        if value not in allowed:
            raise ConfigValidationError(
                message="Unsupported socket type",
                error_type="INVALID_SOCKET_TYPE",
                error_code=1304,
                details={"socket_type": value},
            )
        return value