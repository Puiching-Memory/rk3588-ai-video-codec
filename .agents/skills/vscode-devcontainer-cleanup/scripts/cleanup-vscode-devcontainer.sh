#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-inspect}"

case "$MODE" in
    inspect|cleanup)
        ;;
    *)
        echo "Usage: $0 [inspect|cleanup]" >&2
        exit 1
        ;;
esac

VSCODE_SERVER_ROOT="${VSCODE_SERVER_ROOT:-/root/.vscode-server}"
BIN_DIR="${VSCODE_SERVER_ROOT}/bin"
LOG_DIR="${VSCODE_SERVER_ROOT}/data/logs"
CACHE_DIR="${VSCODE_SERVER_ROOT}/extensionsCache"
X11_DIR="/tmp/.X11-unix"

declare -A keep_builds=()
declare -A active_logs=()
declare -A active_x11=()
declare -A helper_build_for_pid=()
declare -a helper_pids=()
declare -a stale_helper_pids=()
current_x11=""

trim_leading_spaces() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    printf '%s\n' "$value"
}

extract_build_from_args() {
    local args="$1"
    local prefix="${BIN_DIR}/"
    local after build

    [[ "$args" == *"$prefix"* ]] || return 1

    after="${args#*"$prefix"}"
    build="${after%%/*}"

    [[ "$build" =~ ^[0-9a-f]{40}$ ]] || return 1
    printf '%s\n' "$build"
}

extract_log_from_args() {
    local args="$1"
    local prefix="${LOG_DIR}/"
    local after log_name

    [[ "$args" == *"$prefix"* ]] || return 1

    after="${args#*"$prefix"}"
    log_name="${after%%/*}"

    [[ "$log_name" =~ ^[0-9]{8}T[0-9]{6}$ ]] || return 1
    printf '%s\n' "$log_name"
}

list_dir_entries() {
    local dir="$1"

    [[ -d "$dir" ]] || return 0
    find "$dir" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort
}

dir_size() {
    local dir="$1"

    if [[ -e "$dir" ]]; then
        du -sh "$dir" 2>/dev/null | awk '{print $1}'
    else
        printf 'missing\n'
    fi
}

reset_state() {
    keep_builds=()
    active_logs=()
    active_x11=()
    helper_build_for_pid=()
    helper_pids=()
    stale_helper_pids=()
    current_x11=""
}

collect_state() {
    local line pid args build log_name name

    reset_state

    while IFS= read -r line; do
        line="$(trim_leading_spaces "$line")"
        [[ -n "$line" ]] || continue

        pid="${line%% *}"
        args="${line#"$pid"}"
        args="$(trim_leading_spaces "$args")"

        if build="$(extract_build_from_args "$args" 2>/dev/null)"; then
            if [[ "$args" == *"/tmp/vscode-remote-containers-server-"* ]]; then
                helper_pids+=("$pid")
                helper_build_for_pid["$pid"]="$build"
            else
                keep_builds["$build"]=1
            fi
        fi

        if log_name="$(extract_log_from_args "$args" 2>/dev/null)"; then
            active_logs["$log_name"]=1
        fi
    done < <(ps -eo pid=,args=)

    if [[ -r /proc/net/unix ]]; then
        while IFS= read -r line; do
            name="${line##*/}"
            [[ "$name" =~ ^X[0-9]+$ ]] || continue
            active_x11["$name"]=1
        done < <(awk '/\/tmp\/.X11-unix\// {print $8}' /proc/net/unix | sort -u)
    fi

    if [[ "${DISPLAY:-}" =~ ^:([0-9]+)(\.[0-9]+)?$ ]]; then
        current_x11="X${BASH_REMATCH[1]}"
    fi

    for pid in "${helper_pids[@]}"; do
        build="${helper_build_for_pid[$pid]}"
        if [[ -z "${keep_builds[$build]+x}" ]]; then
            stale_helper_pids+=("$pid")
        fi
    done
}

count_lines() {
    local dir="$1"

    if [[ -d "$dir" ]]; then
        find "$dir" -mindepth 1 -maxdepth 1 | wc -l
    else
        printf '0\n'
    fi
}

print_sorted_assoc_keys() {
    local -n ref="$1"

    if [[ "${#ref[@]}" -eq 0 ]]; then
        printf '  (none)\n'
        return
    fi

    printf '%s\n' "${!ref[@]}" | sort | sed 's/^/  /'
}

print_sorted_lines_with_indent() {
    local dir="$1"
    local output

    output="$(list_dir_entries "$dir")"
    if [[ -z "$output" ]]; then
        printf '  (none)\n'
        return
    fi

    printf '%s\n' "$output" | sed 's/^/  /'
}

kill_stale_helpers() {
    local pid

    for pid in "${stale_helper_pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done

    collect_state

    for pid in "${stale_helper_pids[@]}"; do
        kill -KILL "$pid" 2>/dev/null || true
    done
}

remove_old_build_dirs() {
    local dir build removed=0

    [[ -d "$BIN_DIR" ]] || {
        printf '0\n'
        return
    }

    while IFS= read -r dir; do
        [[ -d "$dir" ]] || continue
        build="${dir##*/}"
        if [[ -z "${keep_builds[$build]+x}" ]]; then
            rm -rf -- "$dir"
            removed=$((removed + 1))
        fi
    done < <(find "$BIN_DIR" -mindepth 1 -maxdepth 1 -type d | sort)

    printf '%s\n' "$removed"
}

remove_old_logs() {
    local dir log_name removed=0

    [[ -d "$LOG_DIR" ]] || {
        printf '0\n'
        return
    }

    while IFS= read -r dir; do
        [[ -d "$dir" ]] || continue
        log_name="${dir##*/}"
        if [[ -z "${active_logs[$log_name]+x}" ]]; then
            rm -rf -- "$dir"
            removed=$((removed + 1))
        fi
    done < <(find "$LOG_DIR" -mindepth 1 -maxdepth 1 -type d | sort)

    printf '%s\n' "$removed"
}

remove_stale_x11_entries() {
    local path name removed=0

    [[ -d "$X11_DIR" ]] || {
        printf '0\n'
        return
    }

    while IFS= read -r path; do
        [[ -e "$path" || -L "$path" ]] || continue
        name="${path##*/}"
        if [[ -n "$current_x11" && "$name" == "$current_x11" ]]; then
            continue
        fi
        if [[ -n "${active_x11[$name]+x}" ]]; then
            continue
        fi
        rm -f -- "$path"
        removed=$((removed + 1))
    done < <(find "$X11_DIR" -mindepth 1 -maxdepth 1 -name 'X*' | sort -V)

    printf '%s\n' "$removed"
}

clear_extension_cache() {
    local count

    if [[ ! -d "$CACHE_DIR" ]]; then
        printf '0\n'
        return
    fi

    count="$(find "$CACHE_DIR" -mindepth 1 -maxdepth 1 | wc -l)"
    find "$CACHE_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    printf '%s\n' "$count"
}

print_summary() {
    local phase="$1"

    printf '== %s ==\n' "$phase"
    printf 'mode=%s\n' "$MODE"
    printf 'current_display=%s\n' "${current_x11:-none}"
    printf 'active_build_count=%s\n' "${#keep_builds[@]}"
    printf 'helper_process_count=%s\n' "${#helper_pids[@]}"
    printf 'stale_helper_process_count=%s\n' "${#stale_helper_pids[@]}"
    printf 'build_dir_count=%s\n' "$(count_lines "$BIN_DIR")"
    printf 'log_dir_count=%s\n' "$(count_lines "$LOG_DIR")"
    printf 'x11_entry_count=%s\n' "$(count_lines "$X11_DIR")"
    printf 'active_x11_socket_count=%s\n' "${#active_x11[@]}"
    printf 'extension_cache_size=%s\n' "$(dir_size "$CACHE_DIR")"
    printf 'active_builds:\n'
    print_sorted_assoc_keys keep_builds
    printf 'active_logs:\n'
    print_sorted_assoc_keys active_logs
    printf 'x11_entries:\n'
    print_sorted_lines_with_indent "$X11_DIR"
}

collect_state
print_summary before

if [[ "$MODE" == "cleanup" ]]; then
    stale_before="${#stale_helper_pids[@]}"
    if [[ "$stale_before" -gt 0 ]]; then
        kill_stale_helpers
    fi

    collect_state
    removed_build_dirs="$(remove_old_build_dirs)"
    removed_logs="$(remove_old_logs)"
    removed_x11="$(remove_stale_x11_entries)"
    cleared_cache_entries="$(clear_extension_cache)"

    collect_state

    printf '== cleanup ==\n'
    printf 'terminated_stale_helpers=%s\n' "$stale_before"
    printf 'removed_build_dirs=%s\n' "$removed_build_dirs"
    printf 'removed_log_dirs=%s\n' "$removed_logs"
    printf 'removed_x11_entries=%s\n' "$removed_x11"
    printf 'cleared_cache_entries=%s\n' "$cleared_cache_entries"
    print_summary after
fi