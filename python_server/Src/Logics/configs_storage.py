from pathlib import Path
import json
import re

from Src.Core.app_paths import CONFIGS_DIR


class ConfigStorage:
    def __init__(self):
        self.configsDir = CONFIGS_DIR

    def saveConfig(self, config, filename: str) -> str:
        # Безопасность: запрещаем переходы по директориям
        if ".." in filename or "/" in filename or "\\" in filename:
            raise ValueError("Invalid filename: path traversal not allowed")

        # Добавляем расширение, если его нет
        if not filename.endswith('.json'):
            filename += '.json'

        filePath = self.configsDir / filename

        with open(filePath, "w", encoding="utf-8") as file:
            json.dump(
                config.model_dump(),
                file,
                indent=4,
                ensure_ascii=False
            )

        return filename

    def loadConfig(self, fileName: str) -> dict:
        filePath = self.configsDir / fileName
        if not filePath.exists():
            raise FileNotFoundError(f"Config file '{fileName}' not found")
        with open(filePath, "r", encoding="utf-8") as file:
            return json.load(file)

    def getAllConfigs(self) -> list[str]:
        """
        Возвращает список имён всех JSON-файлов в директории конфигов.
        """
        files = sorted(self.configsDir.glob("*.json"))
        return [file.name for file in files]

    def deleteConfig(self, filename: str) -> None:
        """
        Удаляет файл конфигурации.
        """
        # Проверка на path traversal
        if ".." in filename or "/" in filename or "\\" in filename:
            raise ValueError("Invalid filename: path traversal not allowed")

        if not filename.endswith('.json'):
            filename += '.json'

        filePath = self.configsDir / filename
        if not filePath.exists():
            raise FileNotFoundError(f"Config file '{filename}' not found")

        filePath.unlink()