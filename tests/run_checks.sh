#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$project/build" "$project/test-output"
g++ -std=c++11 -Wall -Wextra "$project/source/tests/test_rotation_controls.cpp" \
  "$project/source/src/rotation_controls.cpp" -o "$project/build/test_rotation_controls"
"$project/build/test_rotation_controls"
printf 'PASS: mathematical rotation tests\n'
if [[ "${1:-}" == --math-only ]]; then exit 0; fi
test_dir="$(mktemp -d "$project/test-output/run_XXXXXX")"
python3 -B "$project/tests/make_fixture.py" > "$test_dir/synthetic.frmsf"
OMP_NUM_THREADS="${OMP_NUM_THREADS:-4}" "$project/build/fermisurfer-enhanced" \
  --self-test-controls "$test_dir" --view-settings "$project/tests/view.in" \
  "$test_dir/synthetic.frmsf" 2>&1 | tee "$test_dir/test.log"
cmp "$test_dir/controls_base.png" "$test_dir/controls_after_vector.png"
cmp "$test_dir/controls_base.png" "$test_dir/image_reset.png"
printf 'PASS: export state and reset pixel-exact; outputs: %s\n' "$test_dir"
if python3 -B -c 'import numpy; from PIL import Image' 2>/dev/null; then
  python3 -B "$project/tests/check_pixels.py" "$test_dir" | tee "$test_dir/pixel_checks.log"
else
  printf 'SKIP: optional pixel/SVG geometry comparison requires numpy and Pillow\n'
fi
