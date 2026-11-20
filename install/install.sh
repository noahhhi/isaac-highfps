#!/usr/bin/env bash
set -euo pipefail

repo=https://github.com/noahhhi/isaac-highfps/releases
game_dir=""
replace=0
while (($#)); do
  case "$1" in
    --game-dir)
      if (($# < 2)); then echo 'Missing path for --game-dir.' >&2; exit 2; fi
      game_dir="$2"
      shift 2
      ;;
    --replace) replace=1; shift ;;
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
safe_to_install() {
  local pid active_dir
  while read -r pid; do
    active_dir="$(readlink -f "/proc/$pid/cwd" 2>/dev/null)" || {
      if [[ -d "/proc/$pid" ]]; then
        echo 'Exit Isaac before installing.' >&2
        exit 1
      fi
      continue
    }
    if [[ "$active_dir" == "$game_dir" ]] || grep -Fq -- " $game_dir/isaac-ng.exe" "/proc/$pid/maps" 2>/dev/null; then
      echo 'Exit Isaac before installing.' >&2
      exit 1
    fi
  done < <(pgrep -ix isaac-ng.exe || true)
}

safe_to_install
download="$(mktemp -d)"
staging=""
lock="$game_dir/.isaac-highfps-install.lock"
locked=0
cleanup() {
  rm -rf -- "$download"
  if [[ -n "$staging" ]]; then rm -rf -- "$staging"; fi
  if ((locked)); then rmdir -- "$lock"; fi
}
trap cleanup EXIT
get() {
  curl --disable --proto '=https' --proto-redir '=https' --fail --silent --show-error --location \
       --connect-timeout 8 --max-time 45 --output "$download/$2" "$1"
}
hash() {
  local value
  value="$(sha256sum < "$1")" || return
  printf '%s\n' "${value%% *}"
}

get "$repo/latest/download/j510-update.txt" j510-update.txt
mapfile -t manifest < "$download/j510-update.txt"
if [[ "${#manifest[@]}" != 6 || "${manifest[0]}" != isaac-highfps-j510-v1 ||
      ! "${manifest[1]}" =~ ^[1-9][0-9]{0,8}$ ]]; then
  echo 'Invalid J510 release manifest.' >&2
  exit 1
fi
for checksum in "${manifest[@]:2}"; do
  [[ "$checksum" =~ ^[0-9a-f]{64}$ ]] || { echo 'Invalid release checksum.' >&2; exit 1; }
done
[[ "$(hash "$game_dir/isaac-ng.exe")" == "${manifest[2]}" ]] || {
  echo 'This release needs the supported Repentance+ J510 executable. No files changed.' >&2
  exit 1
}

assets="$repo/download/j510-r${manifest[1]}"
files=(winmm.dll isaac-highfps-update.exe isaac-highfps.ini)
for index in "${!files[@]}"; do
  name="${files[index]}"
  get "$assets/$name" "$name"
  [[ "$(hash "$download/$name")" == "${manifest[index+3]}" ]] || {
    printf 'Checksum mismatch for %s. No files changed.\n' "$name" >&2
    exit 1
  }
done

mkdir -- "$lock" || { echo 'Another HighFPS installation is in progress.' >&2; exit 1; }
locked=1
safe_to_install
[[ "$(hash "$game_dir/isaac-ng.exe")" == "${manifest[2]}" ]] || exit 1
for name in "${files[@]}"; do
  target="$game_dir/$name"
  if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
    printf 'Cannot replace %s. Expected an ordinary file.\n' "$target" >&2
    exit 1
  fi
done
if [[ -e "$game_dir/winmm.dll" ]] && (( ! replace )); then
  installed="$(hash "$game_dir/winmm.dll")"
  case "$installed" in
    "${manifest[3]}"|d1273d694d9d86c8b137ceb68d3f32575316ee749fb3c634dca736f3a91d78ae|c4523d848b97242c26bc88c7f35461f23120aaf0e4ef6a53cce197ae4d42e97b|d1126945479e0d7ee6b60e474cd62d7ea6671afed7df103a61006678629ec8f4) ;;
    *)
      echo 'Existing winmm.dll differs. If it is safe to replace it, rerun with --replace to back it up and continue.' >&2
      exit 1 ;;
  esac
fi
staging="$(mktemp -d "$game_dir/.isaac-highfps-install-XXXXXXXX")"
for name in "${files[@]}"; do cp -- "$download/$name" "$staging/$name"; done
backup=""
for name in "${files[@]}"; do
  [[ -f "$game_dir/$name" ]] || continue
  if [[ -z "$backup" ]]; then backup="$(mktemp -d "$game_dir/highfps-backup-XXXXXXXX")"; fi
  cp -p -- "$game_dir/$name" "$backup/$name"
done
if [[ -n "$backup" ]]; then printf 'Backup saved in %s\n' "$backup"; fi
if [[ ! -e "$game_dir/isaac-highfps.ini" ]]; then
  mv -T -- "$staging/isaac-highfps.ini" "$game_dir/isaac-highfps.ini"
fi
mv -T -- "$staging/isaac-highfps-update.exe" "$game_dir/isaac-highfps-update.exe"
mv -T -- "$staging/winmm.dll" "$game_dir/winmm.dll"
printf 'Installed HighFPS J510 r%s in %s\n' "${manifest[1]}" "$game_dir"
printf '%s\n' 'For Proton, set the Steam launch option WINEDLLOVERRIDES="winmm=n,b" %command% if you have not already.'
