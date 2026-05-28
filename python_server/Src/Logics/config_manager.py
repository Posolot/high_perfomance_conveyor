from threading import Lock

from Src.Core.validator import ConfigValidationError
from Src.Models.buffer_model import BufferSizeConfig
from Src.Models.frame_model import FrameConfig
from Src.Models.ip_config_model import IpConfig
from Src.Models.patch_model import ConfigPatch
from Src.Models.pipeline_model import PipelineConfig
from Src.Models.runtime_model import RuntimeConfig
from Src.Models.stage_model import StageConfig


class ConfigManager:
    def __init__(self):
        self.lock = Lock()
        self.frame = None
        self.buffer_size = None
        self.runtime = None
        self.ipconfig = None
        self.stages = None

    def set_frame(self, value: FrameConfig):
        with self.lock:
            self.frame = value

    def set_runtime(self, value: RuntimeConfig):
        with self.lock:
            self.runtime = value

    def set_ipconfig(self, value: IpConfig):
        with self.lock:
            self.ipconfig = value

    def set_buffer_size(self, value: BufferSizeConfig):
        with self.lock:
            self.buffer_size = value

    def set_stages(self, value: list[StageConfig]):
        with self.lock:
            self.stages = value

    def merge_patch(self, patch: ConfigPatch):
        with self.lock:
            if patch.frame is not None:
                self.frame = patch.frame
            if patch.runtime is not None:
                self.runtime = patch.runtime
            if patch.ipconfig is not None:
                self.ipconfig = patch.ipconfig
            if patch.buffer_size is not None:
                self.buffer_size = patch.buffer_size
            if patch.stages is not None:
                self.stages = patch.stages

    def reset(self):
        with self.lock:
            self.frame = None
            self.buffer_size = None
            self.runtime = None
            self.ipconfig = None
            self.stages = None

    def missing_sections(self):
        missing = []
        if self.frame is None:
            missing.append("frame")
        if self.buffer_size is None:
            missing.append("buffer_size")
        if self.runtime is None:
            missing.append("runtime")
        if self.ipconfig is None:
            missing.append("ipconfig")
        if self.stages is None:
            missing.append("stages")
        return missing

    def snapshot(self):
        with self.lock:
            return {
                "frame": self.frame.model_dump() if self.frame else None,
                "buffer_size": self.buffer_size.buffer_size if self.buffer_size else None,
                "runtime": self.runtime.model_dump() if self.runtime else None,
                "ipconfig": self.ipconfig.model_dump() if self.ipconfig else None,
                "stages": [stage.model_dump() for stage in self.stages] if self.stages else None,
            }

    def build(self):
        with self.lock:
            missing = self.missing_sections()
            if missing:
                raise ConfigValidationError(
                    message="Config is incomplete",
                    error_type="CONFIG_INCOMPLETE",
                    error_code=2001,
                    details={"missing_sections": missing},
                )

            assert self.frame is not None
            assert self.buffer_size is not None
            assert self.runtime is not None
            assert self.ipconfig is not None
            assert self.stages is not None

            return PipelineConfig(
                frame=self.frame,
                buffer_size=self.buffer_size.buffer_size,
                runtime=self.runtime,
                ipconfig=self.ipconfig,
                stages=self.stages,
            )

    def validate(self):
        config = self.build()

        if not config.stages:
            raise ConfigValidationError(
                message="stages list must not be empty",
                error_type="EMPTY_STAGE_LIST",
                error_code=2002,
            )

        stage_names = [stage.name for stage in config.stages]
        stage_name_set = set(stage_names)

        if len(stage_names) != len(stage_name_set):
            raise ConfigValidationError(
                message="Stage names must be unique",
                error_type="DUPLICATE_STAGE_NAME",
                error_code=2003,
                details={"stage_names": stage_names},
            )

        for stage in config.stages:
            for nxt in stage.next:
                if nxt not in stage_name_set:
                    raise ConfigValidationError(
                        message=f"Stage '{stage.name}' points to unknown next stage '{nxt}'",
                        error_type="UNKNOWN_NEXT_STAGE",
                        error_code=2004,
                        details={"stage": stage.name, "next": nxt},
                    )
                if nxt == stage.name:
                    raise ConfigValidationError(
                        message=f"Stage '{stage.name}' cannot point to itself",
                        error_type="SELF_REFERENCE_STAGE",
                        error_code=2005,
                        details={"stage": stage.name},
                    )

            if stage.max_workers < stage.min_workers:
                raise ConfigValidationError(
                    message=f"Stage '{stage.name}' has max_workers < min_workers",
                    error_type="INVALID_STAGE_WORKER_LIMITS",
                    error_code=2006,
                    details={"stage": stage.name},
                )

        self._validate_acyclic(config.stages)

    def _validate_acyclic(self, stages: list[StageConfig]):
        graph = {stage.name: stage.next for stage in stages}
        visited = {}

        def dfs(node: str):
            state = visited.get(node, 0)
            if state == 1:
                raise ConfigValidationError(
                    message="Stage graph contains a cycle",
                    error_type="CYCLIC_STAGE_GRAPH",
                    error_code=2007,
                    details={"node": node},
                )
            if state == 2:
                return

            visited[node] = 1
            for child in graph.get(node, []):
                dfs(child)
            visited[node] = 2

        for name in graph:
            if visited.get(name, 0) == 0:
                dfs(name)


config_manager = ConfigManager()