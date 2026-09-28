#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#if defined(HAVE_GL_GL_H)
#include <GL/gl.h>
#elif defined(HAVE_OPENGL_GL_H)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <gl2ps.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vector_capture.hpp"

namespace vector_capture {

bool active = false;

namespace {

const int kMaxClipVertices = 16;
const float kEpsilon = 1.0e-7f;

struct ArrayState {
  bool enabled;
  GLint size;
  GLint type;
  GLint stride;
  const unsigned char* pointer;

  ArrayState()
    : enabled(false), size(0), type(0), stride(0), pointer(NULL) {}
};

struct LightState {
  bool enabled;
  GLfloat position[4];
  GLfloat ambient[4];
  GLfloat diffuse[4];
};

struct CaptureState {
  GLfloat modelview[16];
  GLfloat projection[16];
  GLint viewport[4];
  GLfloat depth_range[2];
  GLfloat current_color[4];
  GLfloat current_normal[3];
  GLfloat material_ambient[4];
  GLfloat material_diffuse[4];
  GLfloat global_ambient[4];
  GLfloat line_width;
  GLenum front_face;
  GLenum cull_face_mode;
  bool lighting;
  bool color_material;
  bool normalize;
  bool two_sided;
  bool cull_face;
  ArrayState vertex_array;
  ArrayState color_array;
  ArrayState normal_array;
  LightState lights[2];
};

struct CapturedVertex {
  GLfloat clip[4];
  GLfloat front_color[4];
  GLfloat back_color[4];
};

struct ProjectedVertex {
  GLfloat xyz[3];
  GLfloat front_color[4];
  GLfloat back_color[4];
};

void copy4(const GLfloat* source, GLfloat* destination) {
  for (int i = 0; i < 4; ++i) destination[i] = source[i];
}

void copy3(const GLfloat* source, GLfloat* destination) {
  for (int i = 0; i < 3; ++i) destination[i] = source[i];
}

GLfloat clamp_color(GLfloat value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

void query_array(GLenum pointer_name, GLenum size_name, GLenum type_name,
  GLenum stride_name, bool enabled, ArrayState* array, GLint fixed_size = 0) {
  array->enabled = enabled;
  array->size = fixed_size;
  array->type = 0;
  array->stride = 0;
  array->pointer = NULL;
  if (!enabled) return;

  void* pointer = NULL;
  glGetPointerv(pointer_name, &pointer);
  if (size_name != 0) glGetIntegerv(size_name, &array->size);
  glGetIntegerv(type_name, &array->type);
  glGetIntegerv(stride_name, &array->stride);
  array->pointer = static_cast<const unsigned char*>(pointer);
}

bool read_float_array(const ArrayState& array, int index, int components,
  GLfloat* values) {
  if (!array.enabled || array.pointer == NULL || array.type != GL_FLOAT ||
      index < 0 || components > array.size) return false;
  const GLint element_stride = array.stride != 0
    ? array.stride : array.size * static_cast<GLint>(sizeof(GLfloat));
  const unsigned char* address = array.pointer +
    static_cast<size_t>(index) * static_cast<size_t>(element_stride);
  const GLfloat* source = reinterpret_cast<const GLfloat*>(address);
  for (int i = 0; i < components; ++i) values[i] = source[i];
  return true;
}

void multiply_matrix_vector(const GLfloat matrix[16], const GLfloat input[4],
  GLfloat output[4]) {
  /* OpenGL exposes column-major matrices, while this loop computes M*v. */
  for (int row = 0; row < 4; ++row) {
    output[row] = matrix[row] * input[0]
                + matrix[4 + row] * input[1]
                + matrix[8 + row] * input[2]
                + matrix[12 + row] * input[3];
  }
}

void transform_normal(const CaptureState& state, const GLfloat input[3],
  GLfloat output[3]) {
  /* The application keeps MODELVIEW identity and applies its rotation to the
     CPU vertices/normals.  The upper-left transform also handles the common
     rigid-transform case if another caller uses this wrapper. */
  output[0] = state.modelview[0] * input[0]
            + state.modelview[4] * input[1]
            + state.modelview[8] * input[2];
  output[1] = state.modelview[1] * input[0]
            + state.modelview[5] * input[1]
            + state.modelview[9] * input[2];
  output[2] = state.modelview[2] * input[0]
            + state.modelview[6] * input[1]
            + state.modelview[10] * input[2];
  if (state.normalize) {
    const GLfloat length = std::sqrt(output[0] * output[0]
      + output[1] * output[1] + output[2] * output[2]);
    if (length > kEpsilon) {
      output[0] /= length;
      output[1] /= length;
      output[2] /= length;
    }
  }
}

void query_capture_state(CaptureState* state) {
  glGetFloatv(GL_MODELVIEW_MATRIX, state->modelview);
  glGetFloatv(GL_PROJECTION_MATRIX, state->projection);
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetFloatv(GL_DEPTH_RANGE, state->depth_range);
  glGetFloatv(GL_CURRENT_COLOR, state->current_color);
  glGetFloatv(GL_CURRENT_NORMAL, state->current_normal);
  glGetMaterialfv(GL_FRONT, GL_AMBIENT, state->material_ambient);
  glGetMaterialfv(GL_FRONT, GL_DIFFUSE, state->material_diffuse);
  glGetFloatv(GL_LIGHT_MODEL_AMBIENT, state->global_ambient);
  glGetFloatv(GL_LINE_WIDTH, &state->line_width);
  GLint front_face = GL_CCW;
  GLint cull_face_mode = GL_BACK;
  glGetIntegerv(GL_FRONT_FACE, &front_face);
  glGetIntegerv(GL_CULL_FACE_MODE, &cull_face_mode);
  state->front_face = static_cast<GLenum>(front_face);
  state->cull_face_mode = static_cast<GLenum>(cull_face_mode);
  state->lighting = glIsEnabled(GL_LIGHTING) == GL_TRUE;
  state->color_material = glIsEnabled(GL_COLOR_MATERIAL) == GL_TRUE;
  state->normalize = glIsEnabled(GL_NORMALIZE) == GL_TRUE;
  GLboolean two_sided = GL_FALSE;
  glGetBooleanv(GL_LIGHT_MODEL_TWO_SIDE, &two_sided);
  state->two_sided = two_sided == GL_TRUE;
  state->cull_face = glIsEnabled(GL_CULL_FACE) == GL_TRUE;

  query_array(GL_VERTEX_ARRAY_POINTER, GL_VERTEX_ARRAY_SIZE,
    GL_VERTEX_ARRAY_TYPE, GL_VERTEX_ARRAY_STRIDE,
    glIsEnabled(GL_VERTEX_ARRAY) == GL_TRUE, &state->vertex_array);
  query_array(GL_COLOR_ARRAY_POINTER, GL_COLOR_ARRAY_SIZE,
    GL_COLOR_ARRAY_TYPE, GL_COLOR_ARRAY_STRIDE,
    glIsEnabled(GL_COLOR_ARRAY) == GL_TRUE, &state->color_array);
  /* Normal arrays have a fixed three-component layout; unlike vertex and
     color arrays, OpenGL exposes no GL_NORMAL_ARRAY_SIZE query token. */
  query_array(GL_NORMAL_ARRAY_POINTER, 0,
    GL_NORMAL_ARRAY_TYPE, GL_NORMAL_ARRAY_STRIDE,
    glIsEnabled(GL_NORMAL_ARRAY) == GL_TRUE, &state->normal_array, 3);

  const GLenum light_names[2] = { GL_LIGHT0, GL_LIGHT1 };
  for (int i = 0; i < 2; ++i) {
    state->lights[i].enabled = glIsEnabled(light_names[i]) == GL_TRUE;
    glGetLightfv(light_names[i], GL_POSITION, state->lights[i].position);
    glGetLightfv(light_names[i], GL_AMBIENT, state->lights[i].ambient);
    glGetLightfv(light_names[i], GL_DIFFUSE, state->lights[i].diffuse);
  }
}

void compute_lit_color(const CaptureState& state, const GLfloat base[4],
  const GLfloat object_normal[3], const GLfloat object_position[3],
  bool reverse_normal, GLfloat output[4]) {
  if (!state.lighting) {
    copy4(base, output);
    return;
  }

  GLfloat normal[3];
  transform_normal(state, object_normal, normal);
  if (reverse_normal) {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
  GLfloat eye_position4[4] = { object_position[0], object_position[1],
    object_position[2], 1.0f };
  GLfloat eye_position[4];
  multiply_matrix_vector(state.modelview, eye_position4, eye_position);

  const GLfloat* material_ambient = state.color_material
    ? base : state.material_ambient;
  const GLfloat* material_diffuse = state.color_material
    ? base : state.material_diffuse;
  output[0] = material_ambient[0] * state.global_ambient[0];
  output[1] = material_ambient[1] * state.global_ambient[1];
  output[2] = material_ambient[2] * state.global_ambient[2];
  output[3] = material_diffuse[3];

  for (int i = 0; i < 2; ++i) {
    const LightState& light = state.lights[i];
    if (!light.enabled) continue;
    output[0] += material_ambient[0] * light.ambient[0];
    output[1] += material_ambient[1] * light.ambient[1];
    output[2] += material_ambient[2] * light.ambient[2];

    GLfloat direction[3];
    if (std::fabs(light.position[3]) < kEpsilon) {
      /* For a directional OpenGL light, position.xyz is the direction from
         the vertex toward the light at infinity. */
      direction[0] = light.position[0];
      direction[1] = light.position[1];
      direction[2] = light.position[2];
    }
    else {
      direction[0] = light.position[0] - eye_position[0];
      direction[1] = light.position[1] - eye_position[1];
      direction[2] = light.position[2] - eye_position[2];
    }
    const GLfloat length = std::sqrt(direction[0] * direction[0]
      + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length <= kEpsilon) continue;
    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;
    const GLfloat diffuse_factor = std::max(0.0f,
      normal[0] * direction[0] + normal[1] * direction[1]
      + normal[2] * direction[2]);
    output[0] += material_diffuse[0] * light.diffuse[0] * diffuse_factor;
    output[1] += material_diffuse[1] * light.diffuse[1] * diffuse_factor;
    output[2] += material_diffuse[2] * light.diffuse[2] * diffuse_factor;
  }

  output[0] = clamp_color(output[0]);
  output[1] = clamp_color(output[1]);
  output[2] = clamp_color(output[2]);
  output[3] = clamp_color(output[3]);
}

bool capture_vertex(const CaptureState& state, int index,
  CapturedVertex* vertex) {
  if (!read_float_array(state.vertex_array, index, 3, vertex->clip))
    return false;
  GLfloat object_position[4] = { vertex->clip[0], vertex->clip[1],
    vertex->clip[2], 1.0f };
  GLfloat clip_position[4];
  multiply_matrix_vector(state.modelview, object_position, clip_position);
  multiply_matrix_vector(state.projection, clip_position, vertex->clip);

  GLfloat base_color[4];
  if (!read_float_array(state.color_array, index, 4, base_color)) {
    if (!read_float_array(state.color_array, index, 3, base_color))
      copy4(state.current_color, base_color);
    else
      base_color[3] = 1.0f;
  }
  GLfloat normal[3];
  if (!read_float_array(state.normal_array, index, 3, normal))
    copy3(state.current_normal, normal);

  compute_lit_color(state, base_color, normal, object_position,
    false, vertex->front_color);
  compute_lit_color(state, base_color, normal, object_position,
    true, vertex->back_color);
  return true;
}

void interpolate(const CapturedVertex& first, const CapturedVertex& second,
  GLfloat factor, CapturedVertex* result) {
  const GLfloat inverse = 1.0f - factor;
  for (int i = 0; i < 4; ++i) {
    result->clip[i] = inverse * first.clip[i] + factor * second.clip[i];
    result->front_color[i] = inverse * first.front_color[i]
      + factor * second.front_color[i];
    result->back_color[i] = inverse * first.back_color[i]
      + factor * second.back_color[i];
  }
}

GLfloat plane_value(const CapturedVertex& vertex, int plane) {
  const GLfloat x = vertex.clip[0];
  const GLfloat y = vertex.clip[1];
  const GLfloat z = vertex.clip[2];
  const GLfloat w = vertex.clip[3];
  switch (plane) {
  case 0: return x + w;
  case 1: return w - x;
  case 2: return y + w;
  case 3: return w - y;
  case 4: return z + w;
  default: return w - z;
  }
}

bool inside(GLfloat value) {
  return value >= -kEpsilon;
}

int clip_polygon(const CapturedVertex* input, int input_count,
  CapturedVertex* output) {
  CapturedVertex current[kMaxClipVertices];
  CapturedVertex next[kMaxClipVertices];
  for (int i = 0; i < input_count; ++i) current[i] = input[i];
  int current_count = input_count;

  for (int plane = 0; plane < 6 && current_count > 0; ++plane) {
    int next_count = 0;
    CapturedVertex previous = current[current_count - 1];
    GLfloat previous_value = plane_value(previous, plane);
    bool previous_inside = inside(previous_value);
    for (int i = 0; i < current_count; ++i) {
      const CapturedVertex& vertex = current[i];
      const GLfloat value = plane_value(vertex, plane);
      const bool vertex_inside = inside(value);
      if (vertex_inside != previous_inside) {
        const GLfloat denominator = previous_value - value;
        const GLfloat factor = std::fabs(denominator) > kEpsilon
          ? previous_value / denominator : 0.0f;
        if (next_count < kMaxClipVertices)
          interpolate(previous, vertex, factor, &next[next_count++]);
      }
      if (vertex_inside && next_count < kMaxClipVertices)
        next[next_count++] = vertex;
      previous = vertex;
      previous_value = value;
      previous_inside = vertex_inside;
    }
    current_count = next_count;
    for (int i = 0; i < current_count; ++i) current[i] = next[i];
  }
  for (int i = 0; i < current_count; ++i) output[i] = current[i];
  return current_count;
}

bool clip_line(const CapturedVertex& first, const CapturedVertex& second,
  CapturedVertex* clipped_first, CapturedVertex* clipped_second) {
  GLfloat lower = 0.0f;
  GLfloat upper = 1.0f;
  for (int plane = 0; plane < 6; ++plane) {
    const GLfloat first_value = plane_value(first, plane);
    const GLfloat second_value = plane_value(second, plane);
    if (!inside(first_value) && !inside(second_value)) return false;
    if (inside(first_value) != inside(second_value)) {
      const GLfloat denominator = first_value - second_value;
      if (std::fabs(denominator) <= kEpsilon) return false;
      const GLfloat factor = first_value / denominator;
      if (!inside(first_value)) lower = std::max(lower, factor);
      else upper = std::min(upper, factor);
      if (lower > upper) return false;
    }
  }
  interpolate(first, second, lower, clipped_first);
  interpolate(first, second, upper, clipped_second);
  return true;
}

bool project(const CaptureState& state, const CapturedVertex& source,
  ProjectedVertex* projected) {
  const GLfloat w = source.clip[3];
  if (w <= kEpsilon || !std::isfinite(w)) return false;
  const GLfloat inverse_w = 1.0f / w;
  const GLfloat x = source.clip[0] * inverse_w;
  const GLfloat y = source.clip[1] * inverse_w;
  const GLfloat z = source.clip[2] * inverse_w;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  projected->xyz[0] = state.viewport[0]
    + (x + 1.0f) * static_cast<GLfloat>(state.viewport[2]) * 0.5f;
  projected->xyz[1] = state.viewport[1]
    + (y + 1.0f) * static_cast<GLfloat>(state.viewport[3]) * 0.5f;
  projected->xyz[2] = state.depth_range[0]
    + (z + 1.0f) * 0.5f * (state.depth_range[1] - state.depth_range[0]);
  copy4(source.front_color, projected->front_color);
  copy4(source.back_color, projected->back_color);
  return true;
}

GLfloat signed_area(const ProjectedVertex* vertices, int count) {
  for (int i = 1; i + 1 < count; ++i) {
    const GLfloat ax = vertices[i].xyz[0] - vertices[0].xyz[0];
    const GLfloat ay = vertices[i].xyz[1] - vertices[0].xyz[1];
    const GLfloat bx = vertices[i + 1].xyz[0] - vertices[0].xyz[0];
    const GLfloat by = vertices[i + 1].xyz[1] - vertices[0].xyz[1];
    const GLfloat area = ax * by - ay * bx;
    if (std::fabs(area) > kEpsilon) return area;
  }
  return 0.0f;
}

bool is_front_face(const CaptureState& state, GLfloat area) {
  if (state.front_face == GL_CW) return area < 0.0f;
  return area > 0.0f;
}

bool is_culled(const CaptureState& state, bool front) {
  if (!state.cull_face) return false;
  if (state.cull_face_mode == GL_FRONT_AND_BACK) return true;
  if (state.cull_face_mode == GL_FRONT) return front;
  return !front;
}

void add_triangle(const ProjectedVertex* vertices, bool front,
  int first, int second, int third) {
  GL2PSvertex triangle[3];
  const ProjectedVertex* source[3] = {
    &vertices[first], &vertices[second], &vertices[third] };
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) triangle[i].xyz[j] = source[i]->xyz[j];
    copy4(front ? source[i]->front_color : source[i]->back_color,
      triangle[i].rgba);
  }
  gl2psAddPolyPrimitive(GL2PS_TRIANGLE, 3, triangle, 0, 0.0f, 0.0f,
    0, 1, 1.0f, GL2PS_LINE_CAP_BUTT, GL2PS_LINE_JOIN_MITER, 0);
}

void emit_polygon(const CaptureState& state, const CapturedVertex* input,
  int input_count) {
  CapturedVertex clipped[kMaxClipVertices];
  const int clipped_count = clip_polygon(input, input_count, clipped);
  if (clipped_count < 3) return;

  ProjectedVertex projected[kMaxClipVertices];
  for (int i = 0; i < clipped_count; ++i)
    if (!project(state, clipped[i], &projected[i])) return;
  const GLfloat area = signed_area(projected, clipped_count);
  if (std::fabs(area) <= kEpsilon) return;
  const bool front = is_front_face(state, area);
  if (is_culled(state, front)) return;

  for (int i = 1; i + 1 < clipped_count; ++i)
    add_triangle(projected, state.two_sided ? front : true,
      0, i, i + 1);
}

void emit_line(const CaptureState& state, const CapturedVertex& first,
  const CapturedVertex& second) {
  CapturedVertex clipped_first, clipped_second;
  if (!clip_line(first, second, &clipped_first, &clipped_second)) return;
  ProjectedVertex projected[2];
  if (!project(state, clipped_first, &projected[0]) ||
      !project(state, clipped_second, &projected[1])) return;
  GL2PSvertex line[2];
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) line[i].xyz[j] = projected[i].xyz[j];
    copy4(projected[i].front_color, line[i].rgba);
  }
  const GLfloat width = state.line_width > 0.0f ? state.line_width : 1.0f;
  gl2psAddPolyPrimitive(GL2PS_LINE, 2, line, 0, 0.0f, 0.0f,
    0, 1, width, GL2PS_LINE_CAP_BUTT, GL2PS_LINE_JOIN_MITER, 0);
}

bool capture_vertex_checked(const CaptureState& state, int index,
  CapturedVertex* vertex) {
  return capture_vertex(state, index, vertex);
}

void draw_captured(const CaptureState& state, GLenum mode, int first,
  int count) {
  if (count <= 0 || !state.vertex_array.enabled ||
      state.vertex_array.pointer == NULL || state.vertex_array.type != GL_FLOAT ||
      state.vertex_array.size < 3) return;

  CapturedVertex a, b, c, d;
  switch (mode) {
  case GL_TRIANGLES:
    for (int i = 0; i + 2 < count; i += 3) {
      if (capture_vertex_checked(state, first + i, &a) &&
          capture_vertex_checked(state, first + i + 1, &b) &&
          capture_vertex_checked(state, first + i + 2, &c)) {
        const CapturedVertex triangle[3] = { a, b, c };
        emit_polygon(state, triangle, 3);
      }
    }
    break;
  case GL_QUADS:
    for (int i = 0; i + 3 < count; i += 4) {
      if (capture_vertex_checked(state, first + i, &a) &&
          capture_vertex_checked(state, first + i + 1, &b) &&
          capture_vertex_checked(state, first + i + 2, &c) &&
          capture_vertex_checked(state, first + i + 3, &d)) {
        const CapturedVertex quad[4] = { a, b, c, d };
        emit_polygon(state, quad, 4);
      }
    }
    break;
  case GL_TRIANGLE_FAN:
    if (!capture_vertex_checked(state, first, &a)) return;
    if (count > 1 && capture_vertex_checked(state, first + 1, &b)) {
      for (int i = 2; i < count; ++i) {
        if (!capture_vertex_checked(state, first + i, &c)) continue;
        const CapturedVertex triangle[3] = { a, b, c };
        emit_polygon(state, triangle, 3);
        b = c;
      }
    }
    break;
  case GL_TRIANGLE_STRIP:
    if (count < 3 || !capture_vertex_checked(state, first, &a) ||
        !capture_vertex_checked(state, first + 1, &b)) return;
    for (int i = 2; i < count; ++i) {
      if (!capture_vertex_checked(state, first + i, &c)) continue;
      if ((i & 1) == 0) {
        const CapturedVertex triangle[3] = { a, b, c };
        emit_polygon(state, triangle, 3);
      }
      else {
        const CapturedVertex triangle[3] = { b, a, c };
        emit_polygon(state, triangle, 3);
      }
      a = b;
      b = c;
    }
    break;
  case GL_LINES:
    for (int i = 0; i + 1 < count; i += 2) {
      if (capture_vertex_checked(state, first + i, &a) &&
          capture_vertex_checked(state, first + i + 1, &b))
        emit_line(state, a, b);
    }
    break;
  default:
    /* All draw modes emitted by draw.cpp are handled above.  Keep unknown
       modes harmless during a no-context vector page. */
    break;
  }
}

}

void draw_arrays(unsigned int mode, int first, int count) {
  if (!active) {
    glDrawArrays(static_cast<GLenum>(mode), first, count);
    return;
  }
  CaptureState state;
  query_capture_state(&state);
  draw_captured(state, static_cast<GLenum>(mode), first, count);
}

}
