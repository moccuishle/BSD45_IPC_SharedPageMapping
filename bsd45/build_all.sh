#!/usr/bin/env bash
# build_all.sh - userland -> /bin/sh -> /test -> kernel -> ISO.
# Mirrors build_all.ps1.  Compile-time knobs are passed as environment
# variables (the PowerShell equivalents are -Unicode / -Define):
#   KERNEL_UNICODE=1      build kernel strings as UTF-16 (PS: -Unicode)
#   KERNEL_DEFINE=A,B     comma-separated -D macros (PS: -Define A,B)
# e.g.  KERNEL_DEFINE=S10_AUTOTEST bash build_all.sh
set -euo pipefail
ws="$(cd "$(dirname "$0")" && pwd)"

# Pick up optional flags from argv too, so `bash build_all.sh -D A,B -u`
# works like the PowerShell switches.
while [ $# -gt 0 ]; do
    case "$1" in
        -D|--define)
            if [ $# -lt 2 ]; then echo "build_all.sh: $1 needs a value" >&2; exit 2; fi
            KERNEL_DEFINE="$2"; export KERNEL_DEFINE; shift 2 ;;
        -u|--unicode) KERNEL_UNICODE=1; export KERNEL_UNICODE; shift ;;
        *) echo "build_all.sh: unknown option $1" >&2; shift ;;
    esac
done

bash "$ws/build_userland.sh"
bash "$ws/build_sh.sh"
bash "$ws/build_sh.sh" -t test
bash "$ws/build_kernel.sh"
bash "$ws/build_iso.sh"
echo "all done"
