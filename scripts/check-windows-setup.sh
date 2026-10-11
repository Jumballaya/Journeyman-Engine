#!/usr/bin/env bash
# Installs the Windows setup silently, as the user would, checks what it put
# where, then uninstalls it and checks it's all gone. Git Bash on Windows (CI).
#   scripts/check-windows-setup.sh <journeyman-windows-amd64-setup.exe> <version>
set -euo pipefail

setup="$1" version="$2"
dir="$(cygpath -u "$LOCALAPPDATA")/Programs/Journeyman"
bin="$(cygpath -w "$dir/bin")"
fail() { echo "FAIL: $*" >&2; exit 1; }
userPath() { powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path', 'User')" | tr -d '\r'; }
onPath() { tr ';' '\n' <<<"$(userPath)" | grep -qixF "$bin"; }
export MSYS2_ARG_CONV_EXCL='*'  # the setup's /-options aren't paths

"$setup" /VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART
got="$("$dir/bin/jm.exe" --version)"
[[ "$got" == "jm version $version" ]] || fail "the installed jm says '$got', expected $version"
for program in journeyman_editor journeyman_engine journeyman_server; do
  [[ -f "$dir/bin/$program.exe" ]] || fail "no $program.exe in $dir/bin"
done
onPath || fail "$bin isn't on the user's PATH"
[[ -f "$(cygpath -u "$APPDATA")/Microsoft/Windows/Start Menu/Programs/Journeyman Editor.lnk" ]] \
  || fail "no Start menu entry for the editor"
echo "installed $got in $dir"

"$dir/unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
# The uninstaller hands off to a copy of itself and returns at once.
for _ in $(seq 60); do [[ -e "$dir/bin/jm.exe" ]] || break; sleep 1; done
for _ in $(seq 10); do onPath || break; sleep 1; done
[[ ! -e "$dir/bin/jm.exe" ]] || fail "the uninstaller left $dir/bin/jm.exe"
! onPath || fail "the uninstaller left $bin on the user's PATH"
echo "uninstalled"
