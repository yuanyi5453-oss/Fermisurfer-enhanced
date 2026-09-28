#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$project_dir/build" "$project_dir/exports" "$project_dir/validation"
mkdir -p "$project_dir/build/lib"
gl2ps_dir="$project_dir/source/third_party/gl2ps-1.4.2"
gcc -O2 -fPIC -shared -DGL2PS_HAVE_ZLIB "$gl2ps_dir/gl2ps.c" \
  -o "$project_dir/build/lib/libgl2ps_enhanced.so.new" -lGL -lz -lm
mv -f -- "$project_dir/build/lib/libgl2ps_enhanced.so.new" "$project_dir/build/lib/libgl2ps_enhanced.so"
read -r -a wx_cflags <<< "$(wx-config --cxxflags)"
read -r -a wx_libs <<< "$(wx-config --libs std,gl)"
g++ -std=c++17 -O2 -fopenmp -DGL_GLEXT_PROTOTYPES -DHAVE_GL_GL_H -DHAVE_GL_GLU_H \
  '-DVERSION="2.4.0-enhanced"' -I"$gl2ps_dir" "${wx_cflags[@]}" \
  "$project_dir"/source/src/*.cpp "$project_dir/source/tests/control_integration.cpp" \
  -o "$project_dir/build/fermisurfer-enhanced.new" \
  "${wx_libs[@]}" -lGL -lGLU -L"$project_dir/build/lib" -lgl2ps_enhanced \
  '-Wl,-rpath,$ORIGIN/lib'
mv -f -- "$project_dir/build/fermisurfer-enhanced.new" "$project_dir/build/fermisurfer-enhanced"
printf 'Built: %s\n' "$project_dir/build/fermisurfer-enhanced"
