#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import time
from pathlib import Path

def run_benchmark(pipeline_bin, sender_bin, config_path, h5_path, sleep_ms, duration_sec):
    sender_proc = subprocess.Popen([str(sender_bin), str(h5_path), str(sleep_ms)])
    time.sleep(2)

    pipe_proc = subprocess.Popen([str(pipeline_bin), str(config_path), '-q'])
    time.sleep(1)

    try:
        time.sleep(duration_sec)
    finally:
        sender_proc.terminate()
        try:
            sender_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            sender_proc.kill()
        time.sleep(1)
        pipe_proc.terminate()
        try:
            pipe_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pipe_proc.kill()

def move_results(result_dir: Path):
    # 1. Находим самый свежий CSV в папке results/
    results_dir = Path('results')
    if not results_dir.is_dir():
        print('[WARN] results/ directory not found')
        return

    csv_files = list(results_dir.glob('*.csv'))
    if not csv_files:
        print('[WARN] No CSV files found in results/')
        return

    # Сортируем по времени модификации (новейший первым)
    csv_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    latest_csv = csv_files[0]
    shutil.move(str(latest_csv), str(result_dir / 'pipeline_metrics.csv'))

    # 2. Перемещаем order.txt из текущей директории
    order_txt = Path('order.txt')
    if order_txt.exists():
        shutil.move(str(order_txt), str(result_dir / 'order.txt'))
    else:
        print('[WARN] order.txt not found')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pipeline', required=True)
    parser.add_argument('--sender', required=True)
    parser.add_argument('--data-dir', required=True)
    parser.add_argument('--config-dir', required=True)
    parser.add_argument('--duration', type=int, default=5)
    args = parser.parse_args()

    change_script = Path(__file__).parent / 'change_runtime_configs.py'
    if change_script.exists():
        subprocess.run(['python3', str(change_script)], check=True)

    resolutions = ['640x480', '1920x1080', '2560x1440', '3840x2160']
    fps_map = {
        '640x480': [50, 100],
        '1920x1080': [40, 60, 80, 100],
        '2560x1440': [40, 60, 80, 100],
        '3840x2160': [20, 40, 60, 80, 100],
    }

    config_dir = Path(args.config_dir)
    data_dir = Path(args.data_dir)

    for res in resolutions:
        h5_path = data_dir / f'frames_{res}.h5'
        if not h5_path.exists():
            print(f'HDF5 file not found: {h5_path}')
            continue

        config_files = sorted(config_dir.glob(f'config_{res}_*.json'))
        if not config_files:
            print(f'No configs found for resolution: {res}')
            continue

        for config_path in config_files:
            for fps in fps_map[res]:
                sleep_ms = int(1000 / fps)
                run_benchmark(
                    args.pipeline,
                    args.sender,
                    config_path,
                    h5_path,
                    sleep_ms,
                    args.duration
                )

                # Целевая директория: results/<resolution>/<config_name>/<fps>fps
                result_dir = Path.cwd() / 'results' / res / config_path.stem / f'{fps}fps'
                result_dir.mkdir(parents=True, exist_ok=True)

                move_results(result_dir)

if __name__ == '__main__':
    main()