#!/usr/bin/env bash
set -euo pipefail

include_imported=0
if [[ ${1:-} == "--include-imported" ]]; then
    include_imported=1
    shift
fi
if (( $# != 0 )); then
    printf 'Usage: %s [--include-imported]\n' "$0" >&2
    exit 2
fi

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS3DEV=${PS3DEV:-/opt/movebeat-sdk/ps3dev}
export PSL1GHT=${PSL1GHT:-$PS3DEV}
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
ppu-g++ --version

# PSL1GHT make rules cannot handle source paths containing spaces. A fresh
# Linux-side build also avoids Windows file-lock and timestamp issues.
build_root=$(mktemp -d /tmp/movebeat-build.XXXXXXXX)
printf 'Build directory: %s\n' "$build_root"
cp -R "$project_dir/core" "$project_dir/ps3" "$project_dir/tests" "$build_root/"

if (( include_imported )); then
    # Override only for reviewed/staged bundles; the user's library remains the
    # default and is never rewritten by a build.
    imported_root=${MOVEBEAT_LEVELS_DIR:-$project_dir/out/levels}
    package_levels="$build_root/ps3/package/USRDIR/levels"
    if [[ ! -d $imported_root ]]; then
        printf 'No imported level directory: %s\n' "$imported_root" >&2
        exit 1
    fi
    imported_count=0
    while IFS= read -r -d '' level_dir; do
        level_name=${level_dir##*/}
        if [[ $level_name == .* || $level_name == *[!A-Za-z0-9._+-]* ]]; then
            printf 'Unsafe imported level directory name: %s\n' "$level_name" >&2
            exit 1
        fi
        if [[ ! -f $level_dir/level.ini ]]; then
            printf 'Imported level has no level.ini: %s\n' "$level_dir" >&2
            exit 1
        fi
        python3 "$project_dir/tools/validate_bundle.py" "$level_dir"
        if find "$level_dir" -type l -print -quit | grep -q .; then
            printf 'Imported level contains a symbolic link: %s\n' "$level_dir" >&2
            exit 1
        fi
        if [[ -e $package_levels/$level_name ]]; then
            printf 'Imported level conflicts with a packaged level: %s\n' "$level_name" >&2
            exit 1
        fi
        cp -R "$level_dir" "$package_levels/$level_name"
        imported_count=$((imported_count + 1))
    done < <(find "$imported_root" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)
    if (( imported_count == 0 )); then
        printf 'No imported level bundles found in %s\n' "$imported_root" >&2
        exit 1
    fi
    printf 'Included imported BeatSaver levels: %d\n' "$imported_count"
fi

make -C "$build_root/ps3" -j2 pkg
make -C "$build_root/ps3" -j2 core-tests-pkg
mkdir -p "$project_dir/out/ps3"
for artifact in movebeat.self movebeat.pkg movebeat.gnpdrm.pkg \
                movebeat-tests.elf movebeat-tests.self; do
    test -s "$build_root/ps3/$artifact"
    cp "$build_root/ps3/$artifact" "$project_dir/out/ps3/$artifact"
done
for artifact in movebeat-tests.pkg movebeat-tests.gnpdrm.pkg; do
    test -s "$build_root/ps3/$artifact"
    cp "$build_root/ps3/$artifact" "$project_dir/out/ps3/$artifact"
done
(
    cd "$project_dir/out/ps3"
    sha256sum movebeat.gnpdrm.pkg movebeat.pkg movebeat.self \
              movebeat-tests.elf movebeat-tests.gnpdrm.pkg \
              movebeat-tests.pkg movebeat-tests.self > SHA256SUMS.txt
)
ppu-readelf -h "$build_root/ps3/movebeat.elf"
printf 'Artifacts: %s/out/ps3\n' "$project_dir"
