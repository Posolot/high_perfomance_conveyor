# Conveyor

Проект для тестирования конвейера обработки изображений с плагинами, OpenCV, ZeroMQ, Cap'n Proto и HDF5.

## Что должно лежать в `test_data/`

В папке `test_data/` должны находиться HDF5-файлы с кадрами для бенчмарка.

Ожидаемые файлы:

```text
test_data/frames_640x480.h5
test_data/frames_1920x1080.h5
test_data/frames_2560x1440.h5
test_data/frames_3840x2160.h5
```

## Сборка проекта

Сборка выполняется из папки `build/`:

```bash
mkdir -p build
cd build
cmake .. -DBUILD_BENCHMARK=ON
cmake --build .
```

Если бенчмарк не нужен:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Запуск бенчмарка

Бенчмарк запускается как отдельная цель CMake:

```bash
cd build
cmake --build . --target benchmark
```

Что делает эта команда:

1. Собирает `pipeline` и `sender`.
2. Запускает Python-скрипт `scripts/run_benchmark.py`.
3. Скрипт последовательно прогоняет разные конфигурации и скорости.
4. Результаты сохраняются в папку `results/`.

## Что нужно перед запуском

Перед запуском бенчмарка должны быть доступны:

- `configs/config_640x480_easy.json`
- `configs/config_640x480_hard.json`
- аналогичные конфиги для остальных разрешений
- файлы `.h5` в `test_data/`


## Структура проекта

```text
high_perfomance_conveyor/
├── configs/
├── plugins/
├── scripts/
├── src/
├── test_data/
├── README.md
└── build/
```

## Результаты бенчмарка

После выполнения бенчмарка в папке results появятся:

- CSV-файлы с метриками
- `order.txt`

Эти файлы обычно не коммитят в репозиторий.

## Примечание

Если после изменения CMake или Python-скриптов бенчмарк не запускается, выполните повторную конфигурацию:

```bash
cd build
cmake .. -DBUILD_BENCHMARK=ON
cmake --build . --target benchmark
```
