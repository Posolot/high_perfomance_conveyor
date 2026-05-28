from datetime import datetime, timezone
from enum import Enum
from threading import Lock

from Src.Core.validator import ConfigValidationError
from Src.Logics.config_manager import config_manager


class PipelineStatus(str, Enum):
    STOPPED = "stopped"
    RUNNING = "running"
    RESTARTING = "restarting"


class PipelineController:
    def __init__(self):
        self.lock = Lock()
        self.state = PipelineStatus.STOPPED
        self.last_action = "initialized"
        self.updated_at = self._now()

    def _now(self):
        return datetime.now(timezone.utc).isoformat()

    def start(self):
        with self.lock:
            try:
                config_manager.validate()
            except ConfigValidationError:
                raise

            self.state = PipelineStatus.RUNNING
            self.last_action = "start"
            self.updated_at = self._now()

            return self.status()

    def stop(self):
        with self.lock:
            self.state = PipelineStatus.STOPPED
            self.last_action = "stop"
            self.updated_at = self._now()
            return self.status()

    def restart(self):
        with self.lock:
            try:
                config_manager.validate()
            except ConfigValidationError:
                raise

            self.state = PipelineStatus.RESTARTING
            self.last_action = "restart"
            self.updated_at = self._now()

        with self.lock:
            self.state = PipelineStatus.RUNNING
            self.updated_at = self._now()
            return self.status()

    def status(self):
        with self.lock:
            return {
                "state": self.state.value,
                "last_action": self.last_action,
                "updated_at": self.updated_at,
                "missing_sections": config_manager.missing_sections(),
            }


pipeline_controller = PipelineController()