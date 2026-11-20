#!/usr/bin/env bash
set -euo pipefail

repo=https://github.com/noahhhi/isaac-highfps/releases
launcher=isaac-highfps.sh
script="$(cd -- "$(dirname -- "$0")" && pwd -P)/$(basename -- "$0")"
version_state=.isaac-highfps-game-version
managed_state=.isaac-highfps-managed-sha256

if [[ "${1:-}" == --launch ]]; then
  shift
  if [[ "${1:-}" == -- ]]; then shift; fi
  (($#)) || { echo 'Pass the Steam launch command after --launch --.' >&2; exit 2; }
  /usr/bin/env bash "$script" --check-after "$$" >/dev/null 2>&1 &
  export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winmm=n,b"
  exec "$@"
fi

game_dir=""
replace=0
owner=""
while (($#)); do
  case "$1" in
    --game-dir)
      if (($# < 2)); then echo 'Missing path for --game-dir.' >&2; exit 2; fi
      game_dir="$2"
      shift 2
      ;;
    --replace) replace=1; shift ;;
    --check-after)
      owner="$2"
      [[ "$owner" =~ ^[1-9][0-9]*$ ]] || exit 2
      game_dir="$(dirname -- "$script")"
      shift 2
      ;;
    *) printf 'Usage: %s [--game-dir PATH] [--replace]\n' "$0" >&2; exit 2 ;;
  esac
done
if [[ -z "$game_dir" ]]; then
  for steam in "$HOME/.local/share/Steam" "$HOME/.steam/steam" \
               "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
    [[ -d "$steam/steamapps" ]] || continue
    candidates=("$steam")
    if [[ -r "$steam/steamapps/libraryfolders.vdf" ]]; then
      mapfile -t libraries < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
                                  "$steam/steamapps/libraryfolders.vdf")
      candidates+=("${libraries[@]}")
    fi
    for library in "${candidates[@]}"; do
      candidate="$library/steamapps/common/The Binding of Isaac Rebirth"
      if [[ -f "$candidate/isaac-ng.exe" ]]; then
        game_dir="$candidate"
        break 2
      fi
    done
  done
fi
[[ -n "$game_dir" && -f "$game_dir/isaac-ng.exe" ]] || {
  echo 'Isaac was not found. Pass --game-dir with the directory containing isaac-ng.exe.' >&2
  exit 1
}
game_dir="$(cd -- "$game_dir" && pwd -P)"

game_version() {
  LC_ALL=C grep -aEom1 'Binding of Isaac: Repentance\+ v[0-9]+(\.[0-9]+){3}\.J[0-9]+' \
    "$game_dir/isaac-ng.exe" | awk 'NR == 1 { print substr($NF, 2) }'
}
auto_enabled() {
  [[ ! -f "$game_dir/isaac-highfps.ini" ]] || awk '
    { gsub(/\r/, ""); line=tolower($0) }
    /^[[:space:]]*\[/ { highfps=(line ~ /^[[:space:]]*\[highfps\][[:space:]]*$/); next }
    highfps && line ~ /^[[:space:]]*(autoupdate|enabled)[[:space:]]*=[[:space:]]*0[[:space:]]*(;.*)?$/ { exit 1 }
  ' "$game_dir/isaac-highfps.ini"
}

safe_to_install() {
  local pid active_dir
  while read -r pid; do
    active_dir="$(readlink -f "/proc/$pid/cwd" 2>/dev/null)" || {
      if [[ -d "/proc/$pid" ]]; then
        echo 'Exit Isaac before installing.' >&2
        return 1
      fi
      continue
    }
    if [[ "$active_dir" == "$game_dir" ]] || grep -Fq -- " $game_dir/isaac-ng.exe" "/proc/$pid/maps" 2>/dev/null; then
      echo 'Exit Isaac before installing.' >&2
      return 1
    fi
  done < <(pgrep -ix isaac-ng.exe || true)
}

read_text() { if [[ -r "$1" ]]; then cat -- "$1"; fi; }
process_start() {
  local info
  if info="$(cat "/proc/$1/stat" 2>/dev/null)"; then
    # Fields before the last closing parenthesis include the arbitrary process name.
    awk '{ print $20 }' <<< "${info##*) }"
  fi
}
hash() {
  local value
  value="$(sha256sum < "$1")" || return
  printf '%s\n' "${value%% *}"
}
set_state() {
  local state_temp
  state_temp="$(mktemp "$game_dir/.isaac-highfps-state-XXXXXXXX")"
  printf '%s\n' "$2" > "$state_temp"
  mv -T -- "$state_temp" "$game_dir/$1"
}
get() {
  curl --disable --proto '=https' --proto-redir '=https' --fail --silent --show-error --location \
       --connect-timeout 8 --max-time 45 --max-filesize 16777216 --output "$download/$2" "$1"
}

download=""
staging=""
locked_dir=""
check_lock="$game_dir/.isaac-highfps-check.lock"
install_lock="$game_dir/.isaac-highfps-install.lock"
cleanup() {
  if [[ -n "$download" ]]; then rm -rf -- "$download"; fi
  if [[ -n "$staging" ]]; then rm -rf -- "$staging"; fi
  if [[ -n "$locked_dir" ]]; then rmdir -- "$locked_dir"; fi
}
trap cleanup EXIT

if [[ -n "$owner" ]]; then
  owner_start="$(process_start "$owner")"
  auto_enabled || exit 0
  version="$(game_version)" || exit 0
  [[ -n "$version" && "$(read_text "$game_dir/$version_state")" != "$version" ]] || exit 0
  mkdir -- "$check_lock" || exit 0
  locked_dir="$check_lock"
  [[ "$(read_text "$game_dir/$version_state")" != "$version" ]] || exit 0
  set_state "$version_state" "$version"
  exec >>"$game_dir/isaac-highfps-update.log" 2>&1
  printf 'Checking GitHub for game version %s.\n' "$version"
else
  safe_to_install
  version="$(game_version)" || { echo 'Could not read the Isaac version.' >&2; exit 1; }
fi

download="$(mktemp -d)"
get "$repo/latest/download/highfps-builds.txt" highfps-builds.txt
IFS= read -r schema < "$download/highfps-builds.txt"
[[ "$schema" == isaac-highfps-builds-v1 ]] || { echo 'Invalid release manifest.' >&2; exit 1; }
entry="$(awk -v version="$version" '$1 == version { print; exit }' "$download/highfps-builds.txt")"
if [[ -z "$entry" ]]; then
  printf 'No stable HighFPS release for Isaac %s. Existing files kept.\n' "$version"
  if [[ -n "$owner" ]]; then exit 0; else exit 1; fi
fi
read -r listed game_hash tag dll_hash ini_hash extra <<< "$entry"
[[ -z "$extra" && "$tag" =~ ^[a-zA-Z0-9][a-zA-Z0-9.-]{0,63}$ ]] || exit 1
for checksum in "$game_hash" "$dll_hash" "$ini_hash"; do
  [[ "$checksum" =~ ^[0-9a-f]{64}$ ]] || { echo 'Invalid release checksum.' >&2; exit 1; }
done
[[ "$(hash "$game_dir/isaac-ng.exe")" == "$game_hash" ]] || {
  echo 'Game executable differs from the supported build. Existing files kept.' >&2
  exit 1
}

if [[ -n "$owner" && -f "$game_dir/winmm.dll" && "$(hash "$game_dir/winmm.dll")" == "$dll_hash" ]]; then
  echo 'The correct DLL is already present.'
  exit 0
fi

assets="$repo/download/$tag"
get "$assets/winmm.dll" winmm.dll
[[ "$(hash "$download/winmm.dll")" == "$dll_hash" ]] || { echo 'DLL checksum mismatch.' >&2; exit 1; }
if [[ ! -f "$game_dir/isaac-highfps.ini" ]]; then
  get "$assets/isaac-highfps.ini" isaac-highfps.ini
  [[ "$(hash "$download/isaac-highfps.ini")" == "$ini_hash" ]] || { echo 'INI checksum mismatch.' >&2; exit 1; }
fi

if [[ -n "$owner" ]]; then
  echo 'Verified the download. Waiting for Isaac to close.'
  while [[ -n "$owner_start" && "$(process_start "$owner")" == "$owner_start" ]]; do sleep 1; done
  auto_enabled || exit 0
  rmdir -- "$check_lock"
  locked_dir=""
fi
mkdir -- "$install_lock" || { echo 'Another HighFPS installation is in progress.' >&2; exit 1; }
locked_dir="$install_lock"
safe_to_install
[[ "$(hash "$game_dir/isaac-ng.exe")" == "$game_hash" ]] || exit 1
for name in winmm.dll isaac-highfps.ini "$launcher"; do
  target="$game_dir/$name"
  if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
    printf 'Cannot replace %s. Expected an ordinary file.\n' "$target" >&2
    exit 1
  fi
done
if [[ -e "$game_dir/winmm.dll" ]] && (( ! replace )); then
  installed="$(hash "$game_dir/winmm.dll")"
  managed="$(read_text "$game_dir/$managed_state")"
  case "$installed" in
    "$dll_hash"|"$managed"|717cd2a2cd2f9592a7f106229f811d10e48117fd9226dd05c7bed90312e80c45|d1273d694d9d86c8b137ceb68d3f32575316ee749fb3c634dca736f3a91d78ae|c4523d848b97242c26bc88c7f35461f23120aaf0e4ef6a53cce197ae4d42e97b|d1126945479e0d7ee6b60e474cd62d7ea6671afed7df103a61006678629ec8f4) ;;
    *)
      echo 'Existing winmm.dll differs. To back it up and continue, run the installer with --replace.' >&2
      exit 1 ;;
  esac
fi

staging="$(mktemp -d "$game_dir/.isaac-highfps-install-XXXXXXXX")"
cp -- "$download/winmm.dll" "$staging/"
if [[ -f "$download/isaac-highfps.ini" && ! -e "$game_dir/isaac-highfps.ini" ]]; then
  cp -- "$download/isaac-highfps.ini" "$staging/"
fi
if [[ -z "$owner" ]]; then
  cp -- "$script" "$staging/$launcher"
  chmod 755 "$staging/$launcher"
fi
backup=""
for name in winmm.dll isaac-highfps.ini "$launcher"; do
  [[ -f "$game_dir/$name" ]] || continue
  if [[ -z "$backup" ]]; then backup="$(mktemp -d "$game_dir/highfps-backup-XXXXXXXX")"; fi
  cp -p -- "$game_dir/$name" "$backup/$name"
done
if [[ -n "$backup" ]]; then printf 'Backup saved in %s\n' "$backup"; fi
for name in isaac-highfps.ini "$launcher" winmm.dll; do
  if [[ -f "$staging/$name" ]]; then mv -T -- "$staging/$name" "$game_dir/$name"; fi
done
set_state "$managed_state" "$dll_hash"
set_state "$version_state" "$version"
printf 'Installed HighFPS %s for Isaac %s in %s\n' "$tag" "$listed" "$game_dir"
if [[ -z "$owner" ]]; then
  printf 'To enable launch-time version checks, set Steam Launch Options to:\n"%s/%s" --launch -- %%command%%\n' "$game_dir" "$launcher"
fi
