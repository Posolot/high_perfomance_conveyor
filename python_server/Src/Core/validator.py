from typing import Any, Dict, Optional


class ConfigValidationError(Exception):
    def __init__(
        self,
        message: str,
        error_type: str = "CONFIG_VALIDATION_ERROR",
        error_code: int = 1000,
        details: Optional[Dict[str, Any]] = None,
    ):
        self.message = message
        self.error_type = error_type
        self.error_code = error_code
        self.details = details or {}
        super().__init__(message)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "ok": False,
            "error_type": self.error_type,
            "error_code": self.error_code,
            "message": self.message,
            "details": self.details,
        }