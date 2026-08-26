# Build / Embed Scripts

Run all scripts from the **repository root** (the folder containing `DragonBurn.sln`).

| Script | Purpose |
|---|---|
| `build_all.bat [Debug\|Release]` | Full pipeline: builds projects in the correct order, embeds resources, builds Loader last. Defaults to **Release**. |
| `embed_driver.bat [cfg]` | Converts `x64/<cfg>/.../SharedMemorySpectre.sys` → `mapper/driver_resource.hpp`. |
| `embed_intel.bat [cfg]`  | Converts `bin/iqvw64e.sys` → `mapper/intel_driver_resource.hpp` (the vulnerable Intel NAL driver is embedded directly into Mapper.exe). |
| `embed_mapper.bat [cfg]` | Converts `x64/<cfg>/Mapper.exe` → `loader/mapper_resource.hpp`. |
| `embed_esp.bat [cfg]`    | Converts `x64/<cfg>/ExternalESP.exe` → `loader/esp_resource.hpp`. |
| `bin2h.ps1`              | Helper PowerShell module used by all embed scripts (binary → C++ byte array). |

## Before you build

1. Drop a copy of the vulnerable Intel NAL diagnostic driver into **`bin/iqvw64e.sys`**.
   Expected SHA256: `4429f32db1cc70567919d7d47b844a91cf1329a6cd116f582305f3b7b60cd60b`
   (Intel(R) Network Adapter Diagnostic Driver `iQVW64.SYS` v1.0.0.29, product-date 2013-11-14).
   Common sources:
   - Official Intel LAN Diagnostic driver package (circa 2013)
   - https://github.com/xjinGitty/BIOS-doc/blob/master/Orbitz/L86_0046/FPTW/iqvw64e.sys
   - loldrivers.io entry `1d2cdef1-de44-4849-80e5-e2fa288df681`

2. Build the whole pipeline from the repo root:
   ```cmd
   bat\build_all.bat Release
   ```
   If `bin/iqvw64e.sys` is missing, `embed_intel.bat` will abort with an error and
   the pipeline will stop so you don't end up with a Mapper that can't load the
   vulnerable driver. The mapper also searches next to itself for `iqvw64e.sys`
   at runtime as a last-resort fallback.

## Build order (enforced by `build_all.bat`)

1. **KernelDriver** → produces `SharedMemorySpectre.sys`
2. `embed_driver.bat` → embeds the `.sys` into `mapper/driver_resource.hpp`
3. `embed_intel.bat` → embeds `bin/iqvw64e.sys` into `mapper/intel_driver_resource.hpp`
4. **Mapper** → produces `Mapper.exe` (KM driver + Intel NAL driver both embedded)
5. `embed_mapper.bat` → embeds `Mapper.exe` into `loader/mapper_resource.hpp`
6. **ExternalESP** → produces `ExternalESP.exe`
7. `embed_esp.bat` → embeds `ExternalESP.exe` into `loader/esp_resource.hpp`
8. **Loader** → produced last; contains both mapper and ESP as byte arrays.

## How the Loader works at runtime

1. Checks admin rights; if not elevated, exits with an error.
2. Allocates a `Requests` struct in its own address space and writes its PID
   and pointer into `HKLM\oPid` / `HKLM\oAddr` (same protocol as the existing
   usermode client).
3. Extracts the embedded mapper to `%TEMP%`, runs it, and waits up to 15 s
   for the kernel to ack via the `uready` flag on the shared-memory struct.
4. If the driver is online, extracts the embedded ESP to `%TEMP%` and
   launches it; otherwise ESP is **not** started and the menu reports the
   failure.
5. Interactive menu:
   - `[1]` Reload / re-map driver
   - `[2]` Launch External ESP
   - `[3]` Unload driver & exit (sends `op_unmap`/`op_exit` over shared memory)
   - `[4]` Exit without unloading

## Notes

- `build_all.bat` auto-locates MSBuild via `vswhere` (VS 2019 / 2022 Community/
  Pro/Enterprise). If it cannot find MSBuild, run from a **Developer Command
  Prompt for Visual Studio** so that `msbuild` is on `PATH`.
- Kernel-driver compilation requires the **Windows Driver Kit (WDK)** for the
  `WindowsKernelModeDriver10.0` platform toolset.
- The resource headers ship with tiny 32-byte PE stubs / 1-byte placeholders;
  the embed scripts overwrite them with real binary data. Always build via
  `build_all.bat` to keep the embedded payloads in sync.

## Mapper changes vs the original kdmapper-style code

- Unicode (W) APIs everywhere (`CreateFileW`, `OpenSCManagerW`, `CreateServiceW`,
  `StartServiceW`, `DeleteFileW`) to avoid path/codepage issues.
- Uses `RtlAdjustPrivilege` (ntdll) **in addition to** `AdjustTokenPrivileges`
  to reliably enable `SeDebugPrivilege`, which is required to register/start
  kernel services even when running as admin.
- Random service name (`[a-zA-Z0-9]{16}`) and a random temp filename per run,
  so crashed previous runs won't leave stale services blocking new attempts.
- Marks the service for deletion (`DeleteService`) immediately after
  `StartService` returns, so it cleans itself up when the Intel driver
  unloads / the system reboots.
- Performs an MZ-sanity read of `ntoskrnl.exe` through the Intel driver
  after opening `\\.\Nal` to confirm arbitrary R/W actually works before
  proceeding (catches AV / VBS / HVCI interference early).
