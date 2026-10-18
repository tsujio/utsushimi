#!/usr/bin/env bash
set -euo pipefail

# Windows/WSL上で、事前にusbipd bind済みのESP32-S3を接続する。
# 同じVID/PIDの機器が複数ある場合は、他の機器を外して再実行する。
command -v powershell.exe >/dev/null || {
    echo "powershell.exe not found; run this script in WSL" >&2
    exit 1
}

powershell.exe -NoProfile -NonInteractive -Command '
$ErrorActionPreference = "Stop"
$usbipd = "C:\Program Files\usbipd-win\usbipd.exe"
if (-not (Test-Path -LiteralPath $usbipd)) {
    throw "usbipd.exe not found: $usbipd"
}

$output = & $usbipd list
if ($LASTEXITCODE -ne 0) { throw "usbipd list failed" }
# 接続中の一覧行だけを対象とし、保存済みデバイスの行は選ばない。
$devices = @($output | Where-Object { $_ -match "^\s*\d+-\d+\s+303a:1001\s+" })
if ($devices.Count -eq 0) { throw "ESP32-S3 not found" }
if ($devices.Count -ne 1) {
    throw "Multiple ESP32-S3 devices found; disconnect the others and retry"
}
$line = $devices[0]
$busid = ($line.Trim() -split "\s+")[0]
if ($line -notmatch "\bAttached\b") {
    & $usbipd attach --wsl --busid $busid
    if ($LASTEXITCODE -ne 0) { throw "usbipd attach failed; check that the device is shared with usbipd bind" }
}
'

sudo modprobe cdc_acm

# tty番号は固定しない。sysfsのUSB親デバイスまで辿ってVID/PIDを照合する。
is_esp32() {
    local parent vendor product
    parent=$(readlink -f -- "$1/device") || return 1
    while [[ "$parent" == /sys/* ]]; do
        if [[ -r "$parent/idVendor" && -r "$parent/idProduct" ]]; then
            read -r vendor < "$parent/idVendor"
            read -r product < "$parent/idProduct"
            [[ "$vendor:$product" == "303a:1001" ]]
            return
        fi
        parent=${parent%/*}
    done
    return 1
}

shopt -s nullglob
for ((attempt = 0; attempt < 20; attempt++)); do
    devices=()
    for tty in /sys/class/tty/ttyACM*; do
        if is_esp32 "$tty" && [[ -c "/dev/${tty##*/}" ]]; then
            devices+=("/dev/${tty##*/}")
        fi
    done
    if (( ${#devices[@]} > 1 )); then
        echo "Multiple matching serial devices found; disconnect the others and retry" >&2
        exit 1
    fi
    if (( ${#devices[@]} == 1 )); then
        dev=${devices[0]}
        # 利用できる場合は権限を変更しない。
        if [[ ! -r "$dev" || ! -w "$dev" ]]; then
            sudo chgrp dialout -- "$dev"
            sudo chmod 660 -- "$dev"
        fi
        if [[ ! -r "$dev" || ! -w "$dev" ]]; then
            echo "Cannot access $dev; ensure your current WSL session belongs to dialout" >&2
            exit 1
        fi
        echo "Ready: $dev"
        exit 0
    fi
    sleep 0.5
done

echo "ESP32-S3 serial device not found in WSL" >&2
exit 1
