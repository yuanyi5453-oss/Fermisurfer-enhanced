#include "../src/rotation_controls.hpp"

#include <cassert>
#include <cmath>
#include <initializer_list>

namespace {

const float kTolerance = 1.0e-5f;
const float kRoundTripTolerance = 2.0e-4f;

void identity(float matrix[3][3]) {
  int i, j;
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      matrix[i][j] = (i == j) ? 1.0f : 0.0f;
}

void assert_identity(const float matrix[3][3]) {
  int i, j;
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      assert(std::fabs(matrix[i][j] - ((i == j) ? 1.0f : 0.0f)) < kTolerance);
}

void assert_same(const float left[3][3], const float right[3][3]) {
  int i, j;
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      assert(std::fabs(left[i][j] - right[i][j]) < kTolerance);
}

void assert_same_with_tolerance(const float left[3][3],
  const float right[3][3], float tolerance) {
  int i, j;
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      assert(std::fabs(left[i][j] - right[i][j]) < tolerance);
}

float determinant(const float matrix[3][3]) {
  return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
       - matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
       + matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

void assert_rotation(const float matrix[3][3]) {
  int i, j, k;
  for (i = 0; i < 3; ++i) {
    for (j = 0; j < 3; ++j) {
      float dot = 0.0f;
      for (k = 0; k < 3; ++k)
        dot += matrix[i][k] * matrix[j][k];
      assert(std::fabs(dot - ((i == j) ? 1.0f : 0.0f)) < kRoundTripTolerance);
    }
  }
  assert(std::fabs(determinant(matrix) - 1.0f) < kRoundTripTolerance);
}

void assert_round_trip(float x_degrees, float y_degrees, float z_degrees) {
  float original[3][3];
  float reconstructed[3][3];
  float x_out, y_out, z_out;
  fermi_rotation::matrix_from_euler_degrees(original,
    x_degrees, y_degrees, z_degrees);
  fermi_rotation::matrix_to_euler_degrees(original,
    &x_out, &y_out, &z_out);
  fermi_rotation::matrix_from_euler_degrees(reconstructed,
    x_out, y_out, z_out);
  assert_same_with_tolerance(original, reconstructed, kRoundTripTolerance);
  assert_rotation(original);
  assert_rotation(reconstructed);
}

}

int main() {
  // A clip-space quarter turn must be a rigid rotation in PIXELS, even on
  // rectangular canvases. Test handedness, centre, depth, and homogeneous w.
  for (int width : {800, 1200}) for (int height : {600, 800}) {
    for (int turn = -4; turn <= 4; ++turn) {
      float m[16];
      fermi_rotation::image_rotation_matrix(m, turn, width, height);
      const float x = 0.23f, y = -0.31f;
      const float px = x * width / 2, py = y * height / 2;
      const float outx = (m[0] * x + m[4] * y) * width / 2;
      const float outy = (m[1] * x + m[5] * y) * height / 2;
      const int t = (turn % 4 + 4) % 4;
      const float expected_x[] = {px, -py, -px, py};
      const float expected_y[] = {py, px, -py, -px};
      assert(std::fabs(outx - expected_x[t]) < 3e-5f);
      assert(std::fabs(outy - expected_y[t]) < 3e-5f);
      assert(m[10] == 1 && m[15] == 1 && m[12] == 0 && m[13] == 0);
    }
  }
  float matrix[3][3];
  float original[3][3];

  identity(matrix);
  for (int i = 0; i < 4; ++i)
    fermi_rotation::apply_screen_rotation(matrix,
      fermi_rotation::ScreenYAxis, 90.0f);
  assert_identity(matrix);

  identity(matrix);
  for (int i = 0; i < 4; ++i)
    fermi_rotation::apply_screen_rotation(matrix,
      fermi_rotation::ScreenXAxis, 90.0f);
  assert_identity(matrix);

  fermi_rotation::matrix_from_euler_degrees(original, 17.0f, -23.0f, 41.0f);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      matrix[i][j] = original[i][j];
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenXAxis, 90.0f);
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenXAxis, -90.0f);
  assert_same(matrix, original);

  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      matrix[i][j] = original[i][j];
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenYAxis, 180.0f);
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenYAxis, 180.0f);
  assert_same(matrix, original);

  /* General Euler angles and explicit mixed screen-axis rotations. */
  assert_round_trip(17.0f, -23.0f, 41.0f);
  assert_round_trip(-132.0f, 27.0f, -74.0f);
  identity(matrix);
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenXAxis, 37.0f);
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenYAxis, -58.0f);
  fermi_rotation::apply_screen_rotation(matrix,
    fermi_rotation::ScreenXAxis, 19.0f);
  assert_rotation(matrix);
  {
    float x_out, y_out, z_out;
    float reconstructed[3][3];
    fermi_rotation::matrix_to_euler_degrees(matrix,
      &x_out, &y_out, &z_out);
    fermi_rotation::matrix_from_euler_degrees(reconstructed,
      x_out, y_out, z_out);
    assert_same_with_tolerance(matrix, reconstructed, kRoundTripTolerance);
  }

  /* Test both sides of the Euler gimbal singularity, including values very
     close to it.  At the exact singularity many Euler triples represent the
     same matrix; matrix equality is the invariant being tested. */
  assert_round_trip(23.0f, 89.999f, -47.0f);
  assert_round_trip(-71.0f, -89.999f, 135.0f);
  assert_round_trip(23.0f, 90.0f, -47.0f);
  assert_round_trip(-71.0f, -90.0f, 135.0f);

  // Non-zero primitive-cell centre, arbitrary view/translation and zoom.
  const float center[3] = {0.7f, -0.3f, 1.1f};
  const float initial_translation[3] = {0.21f, -0.44f, -5.3f};
  const float scale = 0.63f;
  for (int axis = 0; axis < 2; ++axis) for (int angle = -180; angle <= 180; angle += 90) {
    fermi_rotation::matrix_from_euler_degrees(matrix, 37.0f, 21.0f, -14.0f);
    float before[3] = {}, translation[3];
    for (int i = 0; i < 3; ++i) {
      translation[i] = initial_translation[i]; before[i] = translation[i];
      for (int j = 0; j < 3; ++j) before[i] += scale * matrix[i][j] * center[j];
    }
    fermi_rotation::apply_centered_rotation(matrix, translation, scale, center,
      static_cast<fermi_rotation::ScreenAxis>(axis), float(angle));
    assert_rotation(matrix);
    for (int i = 0; i < 3; ++i) {
      float after = translation[i];
      for (int j = 0; j < 3; ++j) after += scale * matrix[i][j] * center[j];
      assert(std::fabs(after - before[i]) < 2e-6f);
    }
    fermi_rotation::apply_centered_rotation(matrix, translation, scale, center,
      static_cast<fermi_rotation::ScreenAxis>(axis), -float(angle));
    for (int i = 0; i < 3; ++i)
      assert(std::fabs(translation[i] - initial_translation[i]) < 2e-6f);
  }
  return 0;
}
