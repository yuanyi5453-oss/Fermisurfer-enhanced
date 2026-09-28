// SPDX-License-Identifier: MIT
// Exercise actual wx event bindings, export state restoration, and safe failures.
#include <wx/wx.h>
#include <wx/filename.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "../src/menu.hpp"
#include "../src/variable.hpp"
#include "../src/export_image.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void press(const wxString& label) {
  wxWindow* control = wxWindow::FindWindowByLabel(label, myf);
  require(control != nullptr, "Missing toolbar button");
  wxCommandEvent event(wxEVT_BUTTON, control->GetId());
  event.SetEventObject(control);
  require(control->GetEventHandler()->ProcessEvent(event), "Button event not handled");
}
void same_rotation(const float expected[3][3]) {
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
    require(std::fabs(rot[i][j] - expected[i][j]) < 2e-5f, "Rotation did not return to its original view");
}
}

void run_control_integration_tests(const wxString& directory) {
  try {
    require(wxDirExists(directory), "Test output directory does not exist");
    float original[3][3]; std::memcpy(original, rot, sizeof(rot));
    myf->rotation_step_choice->SetSelection(0);
    press("Right"); press("Left"); same_rotation(original);
    press("Up"); press("Down"); same_rotation(original);
    for (int i = 0; i < 4; ++i) press("Right");
    same_rotation(original);
    myf->rotation_step_choice->SetSelection(1);
    press("Up"); press("Up"); same_rotation(original);
    press("Left"); press("Left"); same_rotation(original);
    // Exercise the new pivot using a primitive cell (Gamma is its corner),
    // nonzero translation, and both projection modes. Then restore the mesh.
    const int saved_bz = fbz, saved_perspective = lperspective;
    float saved_translation[3]; std::memcpy(saved_translation, trans, sizeof(trans));
    fbz = -1;
    float center[3] = {};
    for (int j = 0; j < 3; ++j)
      for (int i = 0; i < 3; ++i) center[j] += 0.5f * bvec[i][j];
    trans[0] += 0.19f; trans[1] -= 0.13f;
    const auto centre_position = [&](float result[3]) {
      for (int i = 0; i < 3; ++i) {
        result[i] = trans[i];
        for (int j = 0; j < 3; ++j) result[i] += scl * rot[i][j] * center[j];
      }
    };
    float fixed[3]; centre_position(fixed);
    for (int projection = 0; projection < 2; ++projection) {
      lperspective = projection;
      for (int step = 0; step < 2; ++step) {
        myf->rotation_step_choice->SetSelection(step);
        for (const char* direction : {"Left", "Right", "Up", "Down"}) {
          press(direction);
          float after[3]; centre_position(after);
          for (int i = 0; i < 3; ++i)
            require(std::fabs(after[i] - fixed[i]) < 2e-5f, "Cell centre drifted");
        }
      }
    }
    same_rotation(original);
    // Legacy selections must leave translation untouched.
    for (int step = 2; step < 4; ++step) {
      myf->rotation_step_choice->SetSelection(step);
      float legacy_translation[3]; std::memcpy(legacy_translation, trans, sizeof(trans));
      press("Right"); press("Left"); same_rotation(original);
      require(std::memcmp(legacy_translation, trans, sizeof(trans)) == 0, "Legacy pivot changed translation");
    }
    fbz = saved_bz; lperspective = saved_perspective;
    std::memcpy(trans, saved_translation, sizeof(trans));
    myf->sync_view_controls();
    double parsed;
    require(myf->textbox_rotatex->GetValue().ToDouble(&parsed) && std::isfinite(parsed), "Rotation textbox is not finite");
    myf->m_canvas->MakeCurrent();
    GLint viewport[4], restored[4], framebuffer, restored_fb;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    ExportOptions o; o.width = 800; o.height = 800; o.dpi = 600;
    wxString error;
    const wxString base = wxFileName(directory, "controls_base.png").GetFullPath();
    require(export_scene(myf->m_canvas, base, o, error), error.ToUTF8().data());
    glGetIntegerv(GL_VIEWPORT, restored);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restored_fb);
    require(framebuffer == restored_fb && std::memcmp(viewport, restored, sizeof(viewport)) == 0, "Export did not restore OpenGL state");
    std::vector<int> bands(draw_band, draw_band + nb);
    for (int ib = 0; ib < nb; ++ib) draw_band[ib] = 0;
    require(export_scene(myf->m_canvas, wxFileName(directory, "controls_cell.svg").GetFullPath(), o, error), error.ToUTF8().data());
    for (int ib = 0; ib < nb; ++ib) draw_band[ib] = bands[ib];
    glGetIntegerv(GL_VIEWPORT, restored);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restored_fb);
    require(framebuffer == restored_fb && std::memcmp(viewport, restored, sizeof(viewport)) == 0, "Vector export did not restore OpenGL state");
    require(export_scene(myf->m_canvas, wxFileName(directory, "controls_after_vector.png").GetFullPath(), o, error), error.ToUTF8().data());
    require(!export_scene(myf->m_canvas, base, o, error), "Existing file was overwritten without permission");
    require(!export_scene(myf->m_canvas, wxFileName(directory, "invalid.bmp").GetFullPath(), o, error), "Invalid extension was accepted");
    o.width = 1;
    require(!export_scene(myf->m_canvas, wxFileName(directory, "invalid.png").GetFullPath(), o, error), "Invalid size was accepted");
    o.width = 800;
    // 2D rotation has independent state: 3D pose, position, zoom, and display
    // options must remain exactly unchanged. Exercise real button bindings.
    float image_pose[3][3], image_trans[3];
    std::memcpy(image_pose, rot, sizeof(rot));
    std::memcpy(image_trans, trans, sizeof(trans));
    const float image_scale = scl;
    const auto same_3d_state = [&]() {
      require(std::memcmp(image_pose, rot, sizeof(rot)) == 0 &&
              std::memcmp(image_trans, trans, sizeof(trans)) == 0 &&
              scl == image_scale && fbz == saved_bz && lperspective == saved_perspective,
              "2D image rotation changed the 3D state");
    };
    myf->image_step_choice->SetSelection(0);
    press("Clockwise");
    require(myf->m_canvas->GetImageRotation() == 270, "Clockwise sign incorrect");
    same_3d_state();
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_cw90.png").GetFullPath(), o, error), error.ToUTF8().data());
    // Small vector export checks the same projection path without a huge mesh.
    for (int ib = 0; ib < nb; ++ib) draw_band[ib] = 0;
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_cell_cw90.svg").GetFullPath(), o, error), error.ToUTF8().data());
    for (int ib = 0; ib < nb; ++ib) draw_band[ib] = bands[ib];
    require(myf->m_canvas->GetImageRotation() == 270, "Vector export changed image rotation");
    press("Counterclockwise");
    require(myf->m_canvas->GetImageRotation() == 0, "2D inverse rotation failed");
    press("Counterclockwise");
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_ccw90.png").GetFullPath(), o, error), error.ToUTF8().data());
    press("Reset image");
    for (int i = 0; i < 4; ++i) press("Clockwise");
    require(myf->m_canvas->GetImageRotation() == 0, "Four 90-degree image turns failed");
    myf->image_step_choice->SetSelection(1);
    press("Clockwise");
    require(myf->m_canvas->GetImageRotation() == 180, "180-degree image step failed");
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_180.png").GetFullPath(), o, error), error.ToUTF8().data());
    press("Clockwise");
    require(myf->m_canvas->GetImageRotation() == 0, "Two 180-degree image turns failed");
    press("Counterclockwise"); press("Reset image");
    require(myf->m_canvas->GetImageRotation() == 0, "Reset image failed");
    same_3d_state();
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_reset.png").GetFullPath(), o, error), error.ToUTF8().data());
    // Non-square export: physical distances should remain unchanged.
    o.width = 1000; o.height = 800;
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_rect_base.png").GetFullPath(), o, error), error.ToUTF8().data());
    myf->image_step_choice->SetSelection(0); press("Clockwise");
    require(export_scene(myf->m_canvas, wxFileName(directory, "image_rect_cw90.png").GetFullPath(), o, error), error.ToUTF8().data());
    press("Reset image"); same_3d_state();
    o.width = o.height = 800;
    myf->rotation_step_choice->SetSelection(0);
    press("Right");
    require(export_scene(myf->m_canvas, wxFileName(directory, "controls_right90.png").GetFullPath(), o, error), error.ToUTF8().data());
    press("Left"); same_rotation(original);
    press("Clockwise"); press("Reset view");
    require(myf->m_canvas->GetImageRotation() == 0, "Reset view did not clear 2D rotation");
    require(scl == 1 && trans[0] == 0 && trans[1] == 0 && trans[2] == -5, "Reset view failed");
    printf("PASS: cell-centred 3D rotation, fixed centre in both projections, legacy origin,\n"
           "wx toolbar 90/180 bindings, inverse rotations, finite textboxes, reset,\n"
           "2D clockwise/counterclockwise 90/180, separate reset, exact 3D state preservation,\n"
           "PNG/SVG export, OpenGL state restoration, overwrite/extension/size guards.\n");
    fflush(stdout);
    std::exit(EXIT_SUCCESS);
  } catch (const std::exception& e) {
    fprintf(stderr, "FAIL: %s\n", e.what());
    std::exit(EXIT_FAILURE);
  }
}
