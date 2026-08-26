<#
.SYNOPSIS
  Convert a binary file into a C++ byte-array header (embed helper).
.DESCRIPTION
  Generates a header like:
    #pragma once
    #include <stdint.h>
    namespace <namespace> {
        inline const uint8_t <name>[] = { 0x4D,0x5A,... };
        inline const size_t   <name>_size = ...;
    }
.PARAMETER InputPath
  Source binary to encode.
.PARAMETER OutputPath
  Destination .hpp/.h file.
.PARAMETER Namespace
  C++ namespace wrapping the array.
.PARAMETER ArrayName
  Identifier of the byte array. An *_size constant is also emitted.
#>
param(
    [Parameter(Mandatory=$true)][string]$InputPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [Parameter(Mandatory=$true)][string]$Namespace,
    [Parameter(Mandatory=$true)][string]$ArrayName
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputPath)) {
    Write-Host "[-] Input not found: $InputPath" -ForegroundColor Red
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $InputPath).Path)
$size  = $bytes.Length
if ($size -eq 0) {
    Write-Host "[-] Input file is empty: $InputPath" -ForegroundColor Red
    exit 1
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <stdint.h>")
[void]$sb.AppendLine("#include <cstddef>")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("namespace $Namespace {")
[void]$sb.AppendLine("`tinline const uint8_t $ArrayName[] = {")

$line = New-Object System.Text.StringBuilder
$col = 0
for ($i = 0; $i -lt $size; $i++) {
    $b = $bytes[$i]
    $tok = "0x{0:X2}" -f $b
    if ($i -lt ($size - 1)) { $tok += "," }
    if ($col -eq 0) { [void]$line.Append("`t`t") }
    [void]$line.Append($tok)
    $col++
    if ($col -ge 16 -or $i -eq ($size - 1)) {
        [void]$sb.AppendLine($line.ToString())
        $line.Length = 0
        $col = 0
    }
}

[void]$sb.AppendLine("`t};")
[void]$sb.AppendLine("`tinline const size_t $($ArrayName)_size = $size;")
[void]$sb.AppendLine("}")

$dir = Split-Path -Parent $OutputPath
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
[System.IO.File]::WriteAllText($OutputPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "[+] Wrote $OutputPath ($size bytes)" -ForegroundColor Green
