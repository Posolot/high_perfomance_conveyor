import json
from Src.Core.Pipeline import Pipeline
from Src.Core.Port import Port
from Src.Core.Stage import Stage
from Src.Core.Connection import Connection

class PipelineParser:
    def parse(self, json_str: str) -> Pipeline:
        data = json.loads(json_str)

        pipeline = Pipeline()

        # --- inputs ---
        for item in data.get("in", []):
            port = Port(
                broker=item["broker"],
                ip=item["ip"],
                port=int(item["port"])
            )
            pipeline.inputs.append(port)

        # --- outputs ---
        for item in data.get("out", []):
            port = Port(
                broker=item["broker"],
                ip=item["ip"],
                port=int(item["port"])
            )
            pipeline.outputs.append(port)

        # --- stages ---
        stages_data = data["stages"]
        pipeline.set_start(stages_data["start"])

        for s in stages_data["list"]:
            stage = Stage(
                name=s["name"],
                callable_name=s["callable"]
            )
            pipeline.add_stage(stage)

        # --- connections ---
        for c in data["connections"]["list"]:
            to_list = c["to"]
            if isinstance(to_list, str):
                to_list = [to_list]

            conn = Connection(
                from_stage=c["from"],
                to_stages=to_list
            )
            pipeline.add_connection(conn)

        return pipeline

    def pipeline_for_cpp(self,pipeline: Pipeline) -> dict:
        stages = [{"name": s.name, "callable": s.callable_name} for s in pipeline.stages.values()]

        connections = []
        for c in pipeline.connections:
            connections.append({
                "from": c.from_stage,
                "to": c.to_stages  # всегда list
            })

        return {
            "stages": stages,
            "connections": connections,
            "start_stage": pipeline.start_stage
        }