# bin/

Place the vulnerable Intel Network Adapter diagnostic driver here so that
`bat\embed_intel.bat` (and by extension `bat\build_all.bat`) can embed it
directly into `Mapper.exe`:

- File name: **`iqvw64e.sys`**
- Expected SHA256: `4429f32db1cc70567919d7d47b844a91cf1329a6cd116f582305f3b7b60cd60b`
- Product: Intel(R) Network Adapter Diagnostic Driver (`iQVW64.SYS`) v1.0.0.29
- Source: official Intel LAN diagnostic driver package (circa 2013), also
  catalogued at [loldrivers.io](https://www.loldrivers.io/drivers/1d2cdef1-de44-4849-80e5-e2fa288df681/)

This file is intentionally **not** committed to the repo because it is a
signed, copyrighted Intel binary and redistributing it directly is not
permitted. Drop your own copy in this folder before running the build
pipeline.

If you do not embed the driver, the mapper will still work as long as
`iqvw64e.sys` sits next to `Mapper.exe` (or is already installed on the
system under `%SystemRoot%\System32\drivers\`). It searches:

1. `.\iqvw64e.sys`
2. `.\bin\iqvw64e.sys`
3. `.\drivers\iqvw64e.sys`
4. `%SystemRoot%\System32\drivers\iqvw64e.sys`
5. `%TEMP%\iqvw64e.sys` (left over from a previous run)
6. Embedded bytes (populated by `bat\embed_intel.bat`)
