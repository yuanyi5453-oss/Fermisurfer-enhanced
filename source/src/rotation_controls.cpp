#include "rotation_controls.hpp"

#include <cmath>

namespace {

const float kPi = 3.14159265358979323846f;
const float kRadiansToDegrees = 180.0f / kPi;
const float kDegreesToRadians = kPi / 180.0f;

void identity(float matrix[3][3]) {
  int i, j;
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      matrix[i][j] = (i == j) ? 1.0f : 0.0f;
}

/* Avoid accumulating visible round-off when a user repeatedly presses a
   cardinal-angle button. */
void cardinal_sincos(float radians, float* cosine, float* sine) {
  *cosine = std::cos(radians);
  *sine = std::sin(radians);
  if (std::fabs(*cosine) < 1.0e-6f) *cosine = 0.0f;
  if (std::fabs(*sine) < 1.0e-6f) *sine = 0.0f;
  if (std::fabs(std::fabs(*cosine) - 1.0f) < 1.0e-6f)
    *cosine = (*cosine < 0.0f) ? -1.0f : 1.0f;
  if (std::fabs(std::fabs(*sine) - 1.0f) < 1.0e-6f)
    *sine = (*sine < 0.0f) ? -1.0f : 1.0f;
}

void multiply_left(const float left[3][3], float right[3][3]) {
  float result[3][3];
  int i, j, k;
  for (i = 0; i < 3; ++i) {
    for (j = 0; j < 3; ++j) {
      result[i][j] = 0.0f;
      for (k = 0; k < 3; ++k)
        result[i][j] += left[i][k] * right[k][j];
    }
  }
  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      right[i][j] = result[i][j];
}

float clamp_unit(float value) {
  if (value < -1.0f) return -1.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

float wrap_degrees(float value) {
  while (value > 180.0f) value -= 360.0f;
  while (value <= -180.0f) value += 360.0f;
  return value;
}

}

namespace fermi_rotation {

void image_rotation_matrix(float matrix[16], int quarter_turns, int width, int height) {
  const int turn = (quarter_turns % 4 + 4) % 4;
  const float cosines[] = {1, 0, -1, 0};
  const float sines[] = {0, 1, 0, -1};
  for (int i = 0; i < 16; ++i) matrix[i] = 0.0f;
  matrix[0] = matrix[5] = cosines[turn];
  matrix[10] = matrix[15] = 1.0f;
  if (width > 0 && height > 0) {
    matrix[1] = sines[turn] * float(width) / height;
    matrix[4] = -sines[turn] * float(height) / width;
  }
}

void apply_screen_rotation(float rotation[3][3], ScreenAxis axis,
  float degrees) {
  float delta[3][3];
  float cosine, sine;
  const float radians = degrees * kDegreesToRadians;

  identity(delta);
  cardinal_sincos(radians, &cosine, &sine);

  if (axis == ScreenXAxis) {
    /* Positive angle matches a downward mouse drag. */
    delta[1][1] = cosine;
    delta[1][2] = -sine;
    delta[2][1] = sine;
    delta[2][2] = cosine;
  }
  else {
    /* Positive angle matches a rightward mouse drag. */
    delta[0][0] = cosine;
    delta[0][2] = sine;
    delta[2][0] = -sine;
    delta[2][2] = cosine;
  }

  multiply_left(delta, rotation);
}

void apply_centered_rotation(float rotation[3][3], float translation[3],
  float scale, const float center[3], ScreenAxis axis, float degrees) {
  float old_position[3] = {};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      old_position[i] += scale * rotation[i][j] * center[j];
  apply_screen_rotation(rotation, axis, degrees);
  for (int i = 0; i < 3; ++i) {
    float new_position = 0.0f;
    for (int j = 0; j < 3; ++j)
      new_position += scale * rotation[i][j] * center[j];
    translation[i] += old_position[i] - new_position;
  }
}

void matrix_to_euler_degrees(const float rotation[3][3],
  float* x_degrees, float* y_degrees, float* z_degrees) {
  const float y = std::asin(clamp_unit(rotation[0][2]));
  const float cosine_y = std::cos(y);
  float x;
  float z;

  if (std::fabs(cosine_y) > 1.0e-6f) {
    x = std::atan2(-rotation[1][2], rotation[2][2]);
    z = std::atan2(-rotation[0][1], rotation[0][0]);
  }
  else {
    /* At the gimbal singularity choose x = 0 and preserve the visible z
       orientation.  The matrix remains the source of truth. */
    x = 0.0f;
    z = std::atan2(rotation[1][0], rotation[1][1]);
  }

  *x_degrees = wrap_degrees(x * kRadiansToDegrees);
  *y_degrees = wrap_degrees(y * kRadiansToDegrees);
  *z_degrees = wrap_degrees(z * kRadiansToDegrees);
}

void matrix_from_euler_degrees(float rotation[3][3],
  float x_degrees, float y_degrees, float z_degrees) {
  const float x = x_degrees * kDegreesToRadians;
  const float y = y_degrees * kDegreesToRadians;
  const float z = z_degrees * kDegreesToRadians;
  const float cx = std::cos(x);
  const float sx = std::sin(x);
  const float cy = std::cos(y);
  const float sy = std::sin(y);
  const float cz = std::cos(z);
  const float sz = std::sin(z);

  rotation[0][0] = cy * cz;
  rotation[0][1] = -cy * sz;
  rotation[0][2] = sy;
  rotation[1][0] = cz * sx * sy + cx * sz;
  rotation[1][1] = cx * cz - sx * sy * sz;
  rotation[1][2] = -cy * sx;
  rotation[2][0] = -cx * cz * sy + sx * sz;
  rotation[2][1] = cz * sx + cx * sy * sz;
  rotation[2][2] = cx * cy;
}

}
