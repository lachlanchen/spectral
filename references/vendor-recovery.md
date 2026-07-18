# Vendor Python Recovery

`wave_main.exe` is a Python 3.11 PyInstaller application, not a native C/C++ program. The faithful recovery path is therefore:

```text
PyInstaller EXE -> embedded PYZ/PYC modules -> Python 3.11 decompiler -> recovered Python
```

Recovered proprietary source is stored locally under the ignored directory:

```text
references/vendor/raw/c12880-wave-main-recovered/python
```

The recovery job targets these vendor-authored modules: `wave_main`, `comread_data`, `find_com`, `com_config`, `Auto_exposure`, `data_processing`, `global_vlaue`, configuration helpers, recording helpers, and generated Qt UI modules. Third-party Python, Qt, numpy, pyserial, and plotting dependencies are not duplicated.

Use `scripts/recover_vendor_source.ps1` to resume per-module recovery. The script skips completed files, logs each module, continues after individual failures, and is suitable for a hidden Windows background process. The recovered files are reference material only; independent application code remains under `src/spectral`.
