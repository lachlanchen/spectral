# Local Vendor Material

The original package is sourced from:

```text
D:\BaiduNetdiskDownload\c12880光谱仪带数据存配套资料
```

The separately supplied Python source was copied from:

```text
D:\BaiduNetdiskDownload\高速CCD上位机源码
```

into the ignored local folder:

```text
references/vendor/raw/high-speed-ccd-source/
```

That source contains `playviewUSB11639save3` and `TCD1304`. It is useful for understanding the vendor's common serial architecture, but it is not C12880MA source: its active parser expects 2048 optical pixels and waits for at least 4004 bytes. It must not be copied directly into the 288-pixel C12880MA application.

`tools/import_vendor_package.py` copies the extracted package into the ignored `raw/c12880-controller/` folder and imports the two Excel workbooks into application resources. The original files include the controller manual, STEP model, calibration workbook, sample data, driver archive, icon, configuration, and unsigned `wave_main.exe`.

Observed executable SHA-256:

```text
606BEE250BA95E93491ABB6BF83B313F4CEF448AA43F9BB107CB331A1BC7684A
```

The executable is not digitally signed and is not required by Spectrum Studio. Keep the raw package local unless redistribution rights are confirmed.

## Reusable source findings

- `pyserial`, 1,500,000 baud, 8-N-1, 8000-byte receive buffer.
- Internal-trigger command begins with `FF AA`.
- Stored correction data is requested with `FF 09` before normal acquisition.
- Exposure changes begin with `FF FF` and use a four-byte big-endian value.
- The initial command includes CR/LF; later commands in this source are inconsistent about the terminator.

The source also contains broad exception handlers and blocking calibration reads. Spectrum Studio intentionally uses bounded reads and explicit diagnostics instead.
