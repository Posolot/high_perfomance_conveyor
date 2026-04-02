from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import json
from pathlib import Path
import subprocess
import psutil
from Src.Logics.Pipeline_parser import PipelineParser

app = FastAPI()

PIPELINE_EXECUTABLE = Path(__file__).parent.parent / "pipeline"
CONFIG_JSON_PATH = Path(__file__).parent.parent / "config.json"
LOG_PATH = Path(__file__).parent.parent / "pipeline.txt"

current_pipeline_proc: subprocess.Popen | None = None

class PipelineRequest(BaseModel):
    config: dict

@app.post("/new_schema")
def new_schema_pipeline(request: PipelineRequest):
    global current_pipeline_proc
    parser = PipelineParser()

    try:
        if not LOG_PATH.exists():
            LOG_PATH.touch()
        # Убийство конвейера прошлой конфигурации
        if current_pipeline_proc and psutil.pid_exists(current_pipeline_proc.pid):
            proc = psutil.Process(current_pipeline_proc.pid)
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except psutil.TimeoutExpired:
                proc.kill()
            current_pipeline_proc = None

        pipeline = parser.parse(json.dumps(request.config))
        cpp_ready = parser.pipeline_for_cpp(pipeline)

        with open(CONFIG_JSON_PATH, "w") as f:
            json.dump(cpp_ready, f, indent=1)
            f.flush()

        log_file = open(LOG_PATH, "a")

        current_pipeline_proc = subprocess.Popen(
            [str(PIPELINE_EXECUTABLE)],
            stdout=log_file,
            stderr=log_file,
            cwd=PIPELINE_EXECUTABLE.parent
        )


        alive = psutil.pid_exists(current_pipeline_proc.pid)

        return {
            "status": "new pipeline started" if alive else "failed to start pipeline",
            "file": str(CONFIG_JSON_PATH),
            "log": str(LOG_PATH),
            "pid": current_pipeline_proc.pid,
            "alive": alive
        }

    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))