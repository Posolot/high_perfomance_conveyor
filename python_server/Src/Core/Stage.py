class Stage:
    def __init__(self, name: str, callable_name: str):
        self.name = name
        self.callable_name = callable_name

    def __repr__(self):
        return f"Stage({self.name} -> {self.callable_name})"