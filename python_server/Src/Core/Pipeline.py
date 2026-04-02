from Src.Core.Stage import Stage
from Src.Core.Port import Port
from Src.Core.Connection import Connection

class Pipeline:
    def __init__(self):
        self.inputs: list[Port] = []
        self.outputs: list[Port] = []
        self.stages: dict[str, Stage] = {}
        self.connections: list[Connection] = []
        self.start_stage: str | None = None

    def add_stage(self, stage: Stage):
        self.stages[stage.name] = stage

    def add_connection(self, connection: Connection):
        self.connections.append(connection)

    def set_start(self, name: str):
        self.start_stage = name