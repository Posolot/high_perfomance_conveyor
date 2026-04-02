
class Port:
    def __init__(self, broker: str, ip: str, port: int):
        self.broker = broker
        self.ip = ip
        self.port = port

    def __repr__(self):
        return f"Port({self.broker}, {self.ip}, {self.port})"