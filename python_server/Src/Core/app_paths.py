from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent.parent.parent
CONFIGS_DIR = BASE_DIR / "configs"
CSV_DIR = BASE_DIR / "results"
CONFIGS_DIR.mkdir(exist_ok=True)