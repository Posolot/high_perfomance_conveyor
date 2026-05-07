#!/usr/bin/env python3
import os
import json
import sys

def get_physical_cores():
    try:
        import psutil
        return psutil.cpu_count(logical=False)
    except ImportError:
        pass
    try:
        with open('/proc/cpuinfo') as f:
            cores = set()
            for line in f:
                if line.startswith('core id'):
                    cores.add(line.split(':')[1].strip())
        if cores:
            return len(cores)
    except:
        pass
    import multiprocessing
    return multiprocessing.cpu_count() // 2 or 1

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_dir = os.path.join(script_dir, '..', 'configs')
    if not os.path.isdir(config_dir):
        print(f"Ошибка: папка '{config_dir}' не найдена", file=sys.stderr)
        sys.exit(1)

    phys = get_physical_cores()
    logical = phys * 2

    for fname in os.listdir(config_dir):
        if fname.endswith('.json'):
            path = os.path.join(config_dir, fname)
            with open(path, 'r') as f:
                cfg = json.load(f)
            if 'runtime' not in cfg:
                cfg['runtime'] = {}
            cfg['runtime']['physical_cores'] = phys
            cfg['runtime']['logical_cpus'] = logical
            cfg['runtime']['cpu_budget'] = logical
            with open(path, 'w') as f:
                json.dump(cfg, f, indent=2)

if __name__ == "__main__":
    main()