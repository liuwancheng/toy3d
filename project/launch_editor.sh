#!/bin/sh
# Toy3d project Editor launcher
set -eu
case "$0" in /*) script="$0" ;; *) script="./$0" ;; esac
project_dir=$(CDPATH= cd -P "$(dirname "$script")" && pwd -P)
descriptor=
default_bin=
if [ -f "$project_dir/saved/editor_launch.txt" ]; then
    { IFS= read -r descriptor || :; IFS= read -r default_bin || :; } < "$project_dir/saved/editor_launch.txt"
fi
if [ -n "$descriptor" ] && [ ! -f "$project_dir/$descriptor" ]; then descriptor=; fi
if [ -z "$descriptor" ]; then
    for file in "$project_dir"/*.toy; do
        [ -f "$file" ] || continue
        if [ -n "$descriptor" ]; then
            echo "ERROR: Multiple .toy files. Open the intended project in Editor to refresh saved/editor_launch.txt." >&2
            exit 1
        fi
        descriptor=${file##*/}
    done
fi
if [ -z "$descriptor" ]; then echo "ERROR: No .toy project beside this launcher." >&2; exit 1; fi
editor_bin=${TOY3D_EDITOR_BIN:-${default_bin:-"$project_dir/../bin"}}
editor="$editor_bin/Toy3dEditor"
if [ -x "$editor_bin/Toy3dEditor.app/Contents/MacOS/Toy3dEditor" ]; then
    editor="$editor_bin/Toy3dEditor.app/Contents/MacOS/Toy3dEditor"
fi
if [ ! -x "$editor" ]; then
    echo "ERROR: Editor was not found. Build the engine or set TOY3D_EDITOR_BIN to its bin directory." >&2
    exit 1
fi
exec "$editor" "$@" "--Project=$project_dir/$descriptor"
