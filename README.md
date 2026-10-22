## Compiling and Running

### Download real datasets

Download  `FD.zip` in https://drive.google.com/drive/folders/1p_omfPGwQP-gIP7cf-NjWJoKYc2OU68E, then unzip to `<this_projetc_root>/data`.

Synthetic datasets will be generated according to configs defined in `src/python/experiments/scenarios.py`

### Compile the C++ implementations
```bash
cmake -B build -S .
cmake --build build
```

### Create a YAML file with the tests

Only real data:
```bash
python3 -m src.python.experiments.configs --fd --output configs/real.yaml
```

Only syn data:
```bash
python3 -m src.python.experiments.configs --synthetic --output configs/syn.yaml
```

Both:
```bash
python3 -m src.python.experiments.configs --fd --synthetic --output configs/complete.yaml
```

### Run experiment
```bash
python3 -m tools.runner configs/<config_name>.yaml
```
