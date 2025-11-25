#!/usr/bin/env bash
set -euo pipefail

# Default configuration (can be overridden via CLI flags)
UPSTREAM_URL="https://github.com/libretro/RetroArch.git"
UPSTREAM_REF="v1.22.2"
BASE_URL="$UPSTREAM_URL"
BASE_REF="v1.22.1"
MIYOO_URL="https://github.com/Rparadise-Team/RetroArch.git"
MIYOO_REF="1.22.1"
OUTPUT_PATCH="RA_MIYOOMINI.patch"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_FILE=""
KEYWORD_PATTERN='Miyoo|MIYOO|Trimui|MiniFlip|MiniV4|MiniPlus|MiyooFlip|MiniFlip32|MiniFlip64|MMIYOOV4'
NAME_GLOBS=(
  '*miyoo*'
  '*Miyoo*'
  '*Trimui*'
  '*MiniFlip*'
  '*MiniV4*'
  '*MiniPlus*'
  '*MMIYOOV4*'
)
KEEP_WORKDIR=0
UPSTREAM_DIR=""
MIYOO_DIR=""
BASE_DIR=""
EXTRA_PATHS=()
LIST_ONLY=0

usage() {
  cat <<'USAGE'
Usage: dist-scripts/make_ra_miyoo_patch.sh [options]

Produce a Miyoo/Trimui focused patch that rebases RetroArch 1.22.0 custom code
onto the official RetroArch v1.22.1 sources.

Main options:
  --output <file>            Path where the resulting patch will be stored (default RA_MIYOOMINI.patch)
  --upstream-dir <path>      Existing clone of RetroArch v1.22.1
  --miyoo-dir <path>         Existing clone of the Miyoo fork (default branch 1.22.0)
  --upstream-url <url>       Alternate RetroArch official URL (default github.com/libretro/RetroArch.git)
  --base-url <url>           URL used for the common ancestor tree
  --miyoo-url <url>          Alternate fork URL (default github.com/Rparadise-Team/RetroArch.git)
  --upstream-ref <ref>       Ref/tag/branch for the official RetroArch tree (default v1.22.1)
  --base-ref <ref>           Ref/tag/branch that acts as the common ancestor (default v1.22.0)
  --miyoo-ref <ref>          Ref/tag/branch for the fork (default 1.22.0)
  --keywords <regex>         Extra keyword filter for relevant files
  --extra <path>             Additional file or directory to include (repeat as needed)
  --manifest <file>          Writes the selected file list to disk
  --list-only                Only emit the manifest without generating a diff
  --base-dir <path>          Existing clone for the common ancestor (optional)
  --keep-workdir             Keep the temporary working directory for debugging
  -h, --help                 Display this help text
USAGE
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Error: '$1' command is required" >&2
    exit 1
  fi
}

clean_path() {
  local path="$1"
  if [[ -z "$path" ]]; then
    echo ""
    return
  fi
  echo "$(cd "$path" && pwd)"
}

clone_repo() {
  local url="$1"
  local ref="$2"
  local target="$3"
  if [[ -d "$target/.git" ]]; then
    echo "Skipping clone, repository already exists at $target"
    return
  fi
  git clone --quiet --depth=1 --branch "$ref" "$url" "$target"
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --output)
        OUTPUT_PATCH="$2"
        shift 2
        ;;
      --upstream-dir)
        UPSTREAM_DIR="$(clean_path "$2")"
        shift 2
        ;;
      --base-dir)
        BASE_DIR="$(clean_path "$2")"
        shift 2
        ;;
      --miyoo-dir)
        MIYOO_DIR="$(clean_path "$2")"
        shift 2
        ;;
      --upstream-url)
        UPSTREAM_URL="$2"
        shift 2
        ;;
      --base-url)
        BASE_URL="$2"
        shift 2
        ;;
      --miyoo-url)
        MIYOO_URL="$2"
        shift 2
        ;;
      --upstream-ref)
        UPSTREAM_REF="$2"
        shift 2
        ;;
      --base-ref)
        BASE_REF="$2"
        shift 2
        ;;
      --miyoo-ref)
        MIYOO_REF="$2"
        shift 2
        ;;
      --keywords)
        KEYWORD_PATTERN="$2"
        shift 2
        ;;
      --extra)
        EXTRA_PATHS+=("$2")
        shift 2
        ;;
      --manifest)
        MANIFEST_FILE="$2"
        shift 2
        ;;
      --list-only)
        LIST_ONLY=1
        shift
        ;;
      --keep-workdir)
        KEEP_WORKDIR=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
  done
}

prepare_repos() {
  WORK_ROOT="$(mktemp -d)"
  if [[ "$KEEP_WORKDIR" -eq 1 ]]; then
    echo "Temporary workspace kept at: $WORK_ROOT"
  else
    trap 'rm -rf "$WORK_ROOT"' EXIT
  fi

  if [[ -z "$UPSTREAM_DIR" ]]; then
    UPSTREAM_DIR="$WORK_ROOT/upstream"
    clone_repo "$UPSTREAM_URL" "$UPSTREAM_REF" "$UPSTREAM_DIR"
  fi

  if [[ -z "$BASE_DIR" ]]; then
    BASE_DIR="$WORK_ROOT/base"
    clone_repo "$BASE_URL" "$BASE_REF" "$BASE_DIR"
  fi

  if [[ -z "$MIYOO_DIR" ]]; then
    MIYOO_DIR="$WORK_ROOT/miyoo"
    clone_repo "$MIYOO_URL" "$MIYOO_REF" "$MIYOO_DIR"
  fi
}

collect_files() {
  local -a matches=()
  if [[ -n "$KEYWORD_PATTERN" ]]; then
    while IFS= read -r path; do
      matches+=("$path")
    done < <(cd "$MIYOO_DIR" && rg --files-with-matches --hidden --no-ignore --glob '!.git/**' -e "$KEYWORD_PATTERN" || true)
  fi

  for glob in "${NAME_GLOBS[@]}"; do
    while IFS= read -r path; do
      matches+=("$path")
    done < <(cd "$MIYOO_DIR" && rg --files --hidden --no-ignore --glob '!.git/**' -g "$glob" || true)
  done

  local -a required=(
    Makefile.mini
    Makefile.miniflip
    Makefile.miniv4
    Makefile.miyooflip
    Makefile.miyooflip32b
    Makefile.plus
    Makefile.trimui
  )
  local -a default_extras=(
    audio/audio_driver.c
    audio/audio_driver.h
    audio/microphone_driver.c
    audio/drivers/audioio.c
    audio/drivers/audioio_miyoomini.c
    audio/drivers/oss.c
    audio/drivers/oss_miyoomini.c
    audio/drivers/sdl_audio.c
    audio/drivers/sdl_audio_miyoomini.c
    audio/drivers/volume
    command.c
    command.h
    config.def.h
    configuration.c
    configuration.h
    dingux/dingux_utils.c
    frontend/drivers/platform_unix.c
    gfx/drivers/miyoomini
    gfx/drivers/sdl_dingux_gfx.c
    gfx/drivers/sdl_rs90_gfx.c
    gfx/video_driver.c
    gfx/video_filter.c
    gfx/video_filters/normal2x.c
    gfx/video_filters/normal4x.c
    input/drivers_joypad/sdl_dingux_joypad.c
    input/drivers_joypad/sdl_miyoomini_joypad.c
    menu/menu_contentless_cores.c
    menu/menu_defines.h
    menu/menu_displaylist.c
    menu/menu_displaylist.h
    menu/menu_driver.c
    menu/menu_driver.h
    menu/menu_entries.h
    menu/menu_explore.c
    menu/menu_input.h
    menu/menu_setting.c
    menu/menu_setting.h
    menu/cbs/menu_cbs_sublabel.c
    menu/drivers/rgui.c
    menu/menu_shader.h
    menu/menu_screensaver.c
    menu/menu_screensaver.h
    miyoo.c
    miyoo.h
    msg_hash.c
    msg_hash.h
    libretro-common/audio/conversion/float_to_s16.c
    retroarch.h
    retroarch.c
    runloop.c
    runloop.h
  )

  matches+=("${required[@]}")
  matches+=("${default_extras[@]}")
  matches+=("${EXTRA_PATHS[@]}")

  if [[ ${#matches[@]} -eq 0 ]]; then
    echo "No files found. Adjust --keywords or --extra paths." >&2
    exit 1
  fi

  mapfile -t UNIQUE_FILES < <(printf '%s\n' "${matches[@]}" | sed '/^$/d' | LC_ALL=C sort -u)
}

emit_manifest() {
  if [[ -z "$MANIFEST_FILE" ]]; then
    return
  fi

  mkdir -p "$(dirname "$MANIFEST_FILE")"
  printf '%s\n' "${UNIQUE_FILES[@]}" > "$MANIFEST_FILE"
  echo "File list written to $MANIFEST_FILE"
}

auto_resolve_conflicts() {
  local target_file="$1"
  local keyword_regex="$2"
  python3 - "$target_file" "$keyword_regex" <<'PY'
import re
import sys

path, pattern = sys.argv[1], sys.argv[2]
regex = re.compile(pattern) if pattern else None

with open(path, 'r', encoding='utf-8', errors='ignore') as fh:
    data = fh.readlines()

result = []
i = 0
changed = False
while i < len(data):
    line = data[i]
    if line.startswith('<<<<<<< '):
        i += 1
        ours = []
        while i < len(data) and not data[i].startswith('======='):
            ours.append(data[i])
            i += 1
        if i >= len(data):
            print('Conflict missing delimiter in', path, file=sys.stderr)
            sys.exit(1)
        i += 1  # skip =======
        theirs = []
        while i < len(data) and not data[i].startswith('>>>>>>> '):
            theirs.append(data[i])
            i += 1
        if i >= len(data):
            print('Conflict missing closing marker in', path, file=sys.stderr)
            sys.exit(1)
        i += 1  # skip >>>>>>>
        ours_text = ''.join(ours)
        theirs_text = ''.join(theirs)
        include_theirs = True
        if regex and regex.search(theirs_text):
            include_theirs = True
        elif not theirs_text.strip():
            include_theirs = False
        elif ours_text.strip() and theirs_text.strip() == ours_text.strip():
            include_theirs = False
        if include_theirs:
            result.append(ours_text)
            if not ours_text.endswith('\n') and theirs_text:
                result.append('\n')
            result.append(theirs_text)
        else:
            result.append(ours_text)
        changed = True
    else:
        result.append(line)
        i += 1

resolved = ''.join(result)
if '<<<<<<< ' in resolved or '>>>>>>>' in resolved:
    print('Automatic resolution failed for', path, file=sys.stderr)
    sys.exit(1)

with open(path, 'w', encoding='utf-8') as fh:
    fh.write(resolved)

sys.exit(0 if changed else 1)
PY
}

dedupe_msg_hash_entries() {
  local target_file="$1"
  python3 - "$target_file" <<'PY'
import sys

path = sys.argv[1]

with open(path, 'r', encoding='utf-8', errors='ignore') as fh:
    lines = fh.readlines()

result = []
seen = set()
i = 0
removed = 0

while i < len(lines):
    line = lines[i]
    if line.lstrip().startswith('MSG_HASH('):
        block = [line]
        i += 1
        while i < len(lines):
            block.append(lines[i])
            if lines[i].strip() == ')':
                i += 1
                break
            i += 1
        identifier = None
        for candidate in block[1:]:
            stripped = candidate.strip()
            if stripped.startswith('MSG_') and stripped.endswith(','):
                identifier = stripped[:-1].strip()
                break
        if identifier and identifier in seen:
            removed += 1
            continue
        if identifier:
            seen.add(identifier)
        result.extend(block)
    else:
        result.append(line)
        i += 1

if removed:
    with open(path, 'w', encoding='utf-8') as fh:
        fh.writelines(result)
    print(f"Removed {removed} duplicate MSG_HASH entries in {path}", file=sys.stderr)
PY
}

post_process_merged_file() {
  local relative_path="$1"
  local target_file="$2"

  case "$relative_path" in
    intl/msg_hash_*.h)
      dedupe_msg_hash_entries "$target_file"
      ;;
  esac
}

merge_file_with_base() {
  local upstream_file="$1"
  local base_file="$2"
  local custom_file="$3"
  local target_file="$4"

  local tmp_upstream tmp_base tmp_custom
  tmp_upstream="$(mktemp)"
  tmp_base="$(mktemp)"
  tmp_custom="$(mktemp)"

  cp "$upstream_file" "$tmp_upstream"
  if [[ -f "$base_file" ]]; then
    cp "$base_file" "$tmp_base"
  else
    : > "$tmp_base"
  fi
  cp "$custom_file" "$tmp_custom"

  if git merge-file -p "$tmp_upstream" "$tmp_base" "$tmp_custom" > "$target_file"; then
    if grep -q '^<<<<<<< ' "$target_file"; then
      if auto_resolve_conflicts "$target_file" "$KEYWORD_PATTERN"; then
        echo "Conflict resolved automatically in $target_file" >&2
      else
        echo "Conflict detected while merging $target_file. Please resolve manually." >&2
        exit 1
      fi
    fi
  else
    if auto_resolve_conflicts "$target_file" "$KEYWORD_PATTERN"; then
      echo "Conflict resolved automatically in $target_file" >&2
    else
      echo "git merge-file failed while processing $target_file, please resolve manually." >&2
      exit 1
    fi
  fi

  rm -f "$tmp_upstream" "$tmp_base" "$tmp_custom"
}

copy_subset() {
  local upstream_subset="$WORK_ROOT/upstream_subset"
  local merged_subset="$WORK_ROOT/custom_subset"
  mkdir -p "$upstream_subset" "$merged_subset"

  local copied=0
  for rel in "${UNIQUE_FILES[@]}"; do
    local base_path="$UPSTREAM_DIR/$rel"
    local custom_path="$MIYOO_DIR/$rel"
    local ancestor_path="$BASE_DIR/$rel"

    if [[ ! -e "$base_path" && ! -e "$custom_path" ]]; then
      continue
    fi

    if [[ -d "$base_path" || -d "$custom_path" ]]; then
      if [[ -d "$base_path" ]]; then
        mkdir -p "$upstream_subset/$rel"
        rsync -a --quiet --exclude '.git' --exclude '.gitmodules' "$base_path/" "$upstream_subset/$rel/"
      fi
      if [[ -d "$custom_path" ]]; then
        mkdir -p "$merged_subset/$rel"
        rsync -a --quiet --exclude '.git' --exclude '.gitmodules' "$custom_path/" "$merged_subset/$rel/"
      fi
      copied=$((copied + 1))
      continue
    fi

    if [[ -f "$base_path" ]]; then
      mkdir -p "$upstream_subset/$(dirname "$rel")"
      cp "$base_path" "$upstream_subset/$rel"
    fi

    if [[ -f "$base_path" && -f "$custom_path" ]]; then
      mkdir -p "$merged_subset/$(dirname "$rel")"
      merge_file_with_base "$base_path" "$ancestor_path" "$custom_path" "$merged_subset/$rel"
      post_process_merged_file "$rel" "$merged_subset/$rel"
      copied=$((copied + 1))
      continue
    fi

    if [[ -f "$custom_path" ]]; then
      mkdir -p "$merged_subset/$(dirname "$rel")"
      cp "$custom_path" "$merged_subset/$rel"
      copied=$((copied + 1))
      continue
    fi

    if [[ -f "$base_path" ]]; then
      mkdir -p "$merged_subset/$(dirname "$rel")"
      cp "$base_path" "$merged_subset/$rel"
      copied=$((copied + 1))
    fi
  done

  if [[ $copied -eq 0 ]]; then
    echo "No matching files were copied. Nothing to diff." >&2
    exit 1
  fi
}

apply_overrides() {
  local target_dir="$1"
  local override_dir="$SCRIPT_DIR/miyoo-overrides"
  local applied=0
  if [[ ! -d "$override_dir" ]]; then
    return
  fi

  apply_single_override() {
    local patch_file="$1"
    local -a base_args=(--directory "$target_dir" --strip 1 --input "$patch_file")

    if patch --forward --dry-run "${base_args[@]}" >/dev/null 2>&1; then
      patch --forward "${base_args[@]}" >/dev/null
      echo "Applying override: $(basename "$patch_file")"
      return 0
    fi

    if patch --reverse --dry-run "${base_args[@]}" >/dev/null 2>&1; then
      echo "Override already applied: $(basename "$patch_file")"
      return 0
    fi

    echo "Failed to apply override: $(basename "$patch_file")" >&2
    patch --forward "${base_args[@]}"
  }

  while IFS= read -r -d '' patch_file; do
    if apply_single_override "$patch_file"; then
      applied=1
    fi
  done < <(find "$override_dir" -type f -name '*.patch' -print0 | LC_ALL=C sort -z)

  if [[ $applied -eq 0 ]]; then
    echo "No overrides applied"
  fi
}

create_patch() {
    local diff_output
    diff_output=$(cd "$WORK_ROOT" && { git --no-pager diff --binary --no-index upstream_subset custom_subset || true; } | \
        sed -e 's|a/|a/|g' \
            -e 's|b/|b/|g' \
            -e 's|--- a/|--- a/|g' \
            -e 's|+++ b/|+++ b/|g' \
            -e 's|diff --git a/|diff --git a/|g' \
            -e 's| b/| b/|g')

    if [[ -z "$diff_output" ]]; then
        echo "No differences detected between the selected files." >&2
        exit 1
    fi

    printf '%s\n' "$diff_output" > "$OUTPUT_PATCH"
    echo "Patch written to $OUTPUT_PATCH"
}

main() {
  require_cmd git
  require_cmd rg
  require_cmd rsync
  require_cmd python3
  require_cmd patch

  parse_args "$@"
  prepare_repos
  collect_files
  emit_manifest

  if [[ "$LIST_ONLY" -eq 1 ]]; then
    echo "--list-only requested, diff generation skipped."
    return
  fi
  copy_subset
  apply_overrides "$WORK_ROOT/custom_subset"
  create_patch
}

main "$@"
