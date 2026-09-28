/*
  Small, UI-independent helpers for the discrete view rotations.

  The renderer stores a row-major matrix and applies it as
  output[j] = rotation[j][i] * input[i].  The helpers below use that same
  convention so the arrow buttons compose with the mouse trackball rotation.
*/
#if ! defined(FERMISURFER_ROTATION_CONTROLS_H)
#define FERMISURFER_ROTATION_CONTROLS_H

namespace fermi_rotation {

/* Column-major clip-space rotation, corrected for rectangular pixel aspect.
   quarter_turns is counterclockwise; model geometry and lighting are untouched. */
void image_rotation_matrix(float matrix[16], int quarter_turns, int width, int height);

/* The screen X axis is the horizontal screen axis (used by Up/Down).
   The screen Y axis is the vertical screen axis (used by Left/Right). */
enum ScreenAxis {
  ScreenXAxis = 0,
  ScreenYAxis = 1
};

/* Pre-multiply a view rotation by a screen-space rotation. */
void apply_screen_rotation(float rotation[3][3], ScreenAxis axis,
  float degrees);

/* Keep scale * R * center + translation fixed while changing R. */
void apply_centered_rotation(float rotation[3][3], float translation[3],
  float scale, const float center[3], ScreenAxis axis, float degrees);

/* Convert the renderer's matrix convention to the Euler values used by the
   existing text boxes.  Results are in degrees. */
void matrix_to_euler_degrees(const float rotation[3][3],
  float* x_degrees, float* y_degrees, float* z_degrees);

/* Build the renderer's matrix convention from Euler angles in degrees. */
void matrix_from_euler_degrees(float rotation[3][3],
  float x_degrees, float y_degrees, float z_degrees);

}

#endif // FERMISURFER_ROTATION_CONTROLS_H
