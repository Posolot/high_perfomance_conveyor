class Connection:
    def __init__(self, from_stage: str, to_stages: list[str]):
        self.from_stage = from_stage
        self.to_stages = to_stages

    def __repr__(self):
        return f"{self.from_stage} -> {self.to_stages}"