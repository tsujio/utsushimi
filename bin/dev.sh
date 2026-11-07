#!/usr/bin/env bash
# リポジトリ内に導入済みの開発ツールでmakeを実行する。
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$project_root"
local_host="$project_root/build/host-tools"
if [[ -d "$local_host/usr/lib/x86_64-linux-gnu" ]]; then
    export LD_LIBRARY_PATH="$local_host/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PATH="$local_host/usr/bin:$PATH"
fi
if [[ -z "${CXX:-}" ]] && ! command -v g++ >/dev/null &&
   [[ -x "$local_host/usr/bin/x86_64-linux-gnu-g++-15" ]]; then
    export CXX="$local_host/usr/bin/x86_64-linux-gnu-g++-15 --sysroot=$local_host -B$local_host/usr/lib/x86_64-linux-gnu"
fi
if [[ -z "${IDF_PATH:-}" && -f "$project_root/build/utsushimi-esp-idf/export.sh" ]]; then
    export IDF_PATH="$project_root/build/utsushimi-esp-idf"
    export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$project_root/build/utsushimi-idf-tools}"
fi
exec make "$@"
