// SPDX-License-Identifier: MIT
// Native OpenGL vector capture and off-screen supersampled raster export.
#include <wx/wx.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/spinctrl.h>
#include <wx/filefn.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gl2ps.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "operation.hpp"
#include "menu.hpp"
#include "variable.hpp"
#include "export_image.hpp"
#include "vector_capture.hpp"

wxString cli_export_path;
wxString cli_view_settings;
static wxString self_test_directory;
void run_control_integration_tests(const wxString& directory);
static ExportOptions cli_options;

namespace {
// Restore screen rendering even if file creation, rendering, or encoding fails.
struct GLState {
  GLint framebuffer, renderbuffer, mode;
  GLState() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();
  }
  ~GLState() {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(mode);
    glPopClientAttrib(); glPopAttrib();
  }
};
struct Framebuffer {
  GLuint fbo = 0, color = 0, depth = 0;
  void create(int width, int height) {
    GLint limit = 0;
    GLint viewport[2] = {};
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &limit);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport);
    if (width > limit || height > limit || width > viewport[0] || height > viewport[1])
      throw std::runtime_error("Requested resolution exceeds the GPU limit. Reduce size or supersampling.");
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &color); glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("Could not allocate an off-screen OpenGL framebuffer.");
  }
  ~Framebuffer() {
    if (depth) glDeleteRenderbuffers(1, &depth);
    if (color) glDeleteRenderbuffers(1, &color);
    if (fbo) glDeleteFramebuffers(1, &fbo);
  }
};
bool valid_options(const ExportOptions& o, wxString& error) {
  if (o.width < 64 || o.height < 64 || o.width > 8192 || o.height > 8192 ||
      o.dpi < 72 || o.dpi > 1200 || (o.supersample != 1 && o.supersample != 2)) {
    error = "Width/height: 64..8192; DPI: 72..1200; supersample: 1 or 2.";
    return false;
  }
  return true;
}
void write_png(TestGLCanvas* canvas, const wxString& temp, const ExportOptions& o) {
  const int w = o.width * o.supersample, h = o.height * o.supersample;
  if (static_cast<long long>(w) * h > 64000000)
    throw std::runtime_error("Limit: 64 million internal pixels. Reduce size or select 1x supersampling.");
  Framebuffer target;
  target.create(w, h);
  canvas->RenderScene(w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 3);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL pixel readback failed.");
  wxImage output(w, h, false);
  if (!output.IsOk()) throw std::runtime_error("Could not allocate output image.");
  for (int y = 0; y < h; ++y)
    std::memcpy(output.GetData() + static_cast<size_t>(y) * w * 3,
                pixels.data() + static_cast<size_t>(h - 1 - y) * w * 3,
                static_cast<size_t>(w) * 3);
  if (o.supersample > 1) output.Rescale(o.width, o.height, wxIMAGE_QUALITY_HIGH);
  output.SetOption(wxIMAGE_OPTION_RESOLUTIONUNIT, wxIMAGE_RESOLUTION_INCHES);
  output.SetOption(wxIMAGE_OPTION_RESOLUTION, o.dpi);
  if (!output.SaveFile(temp, wxBITMAP_TYPE_PNG)) throw std::runtime_error("PNG encoding failed.");
}
void write_vector(TestGLCanvas* canvas, const wxString& temp, const wxString& ext,
                  const ExportOptions& o) {
  // PDF/SVG page units are points; keep the same physical size as a PNG at its DPI.
  const int w = std::max(1, int(std::lround(o.width * 72.0 / o.dpi)));
  const int h = std::max(1, int(std::lround(o.height * 72.0 / o.dpi)));
  Framebuffer target;
  target.create(w, h);
  GLint viewport[4] = {0, 0, w, h};
  const int format = ext == "pdf" ? GL2PS_PDF : GL2PS_SVG;
  const int flags = GL2PS_NO_OPENGL_CONTEXT | GL2PS_NO_BLENDING | GL2PS_OCCLUSION_CULL | GL2PS_BEST_ROOT |
                    (format == GL2PS_PDF ? GL2PS_COMPRESS : 0);
  {
    FILE* fp = wxFopen(temp, "wb");
    if (!fp) throw std::runtime_error("Cannot open temporary vector output.");
    canvas->RenderScene(w, h); // Establish projection and background before capture.
    const int begin = gl2psBeginPage("Fermi surface", "FermiSurfer Enhanced", viewport,
      format, GL2PS_BSP_SORT, flags, GL_RGBA, 0, nullptr, 0, 0, 0, 0, fp, nullptr);
    if (begin != GL2PS_SUCCESS) {
      fclose(fp); throw std::runtime_error("GL2PS could not begin vector capture.");
    }
    GL2PSvertex background[4] = {};
    const GLfloat corners[4][2] = {{0,0}, {GLfloat(w),0}, {GLfloat(w),GLfloat(h)}, {0,GLfloat(h)}};
    for (int i = 0; i < 4; ++i) {
      background[i].xyz[0] = corners[i][0]; background[i].xyz[1] = corners[i][1]; background[i].xyz[2] = 1.0f;
      for (int c = 0; c < 4; ++c) background[i].rgba[c] = BackGroundColor[c];
    }
    gl2psAddPolyPrimitive(GL2PS_QUADRANGLE, 4, background, 0, 0, 0, 0xffff, 1, 1, 0, 0, 0);
    vector_capture::active = true;
    try { canvas->RenderScene(w, h); }
    catch (...) {
      vector_capture::active = false;
      gl2psEndPage(); fclose(fp); throw;
    }
    vector_capture::active = false;
    const int status = gl2psEndPage();
    const bool io_error = ferror(fp) != 0;
    const int closed = fclose(fp);
    if (io_error || closed != 0) throw std::runtime_error("Could not finish writing vector file.");
    if (status == GL2PS_SUCCESS) return;
    throw std::runtime_error("GL2PS vector capture failed or scene was empty.");
  }
}
}

bool export_scene(TestGLCanvas* canvas, const wxString& path, const ExportOptions& o, wxString& error) {
  if (!valid_options(o, error)) return false;
  wxFileName file(path);
  const wxString ext = file.GetExt().Lower();
  if (ext != "png" && ext != "pdf" && ext != "svg") {
    error = "Output extension must be .png, .pdf or .svg."; return false;
  }
  if (!file.DirExists()) { error = "Output directory does not exist."; return false; }
  if (file.FileExists() && !o.overwrite) { error = "Output exists; choose a new filename or use --overwrite."; return false; }
  wxString temp;
  try {
    canvas->MakeCurrent();
    GLState saved;
    while (glGetError() != GL_NO_ERROR) {} // Discard unrelated pre-existing screen errors.
    temp = wxFileName::CreateTempFileName(file.GetFullPath() + ".tmp-");
    if (temp.empty()) throw std::runtime_error("Cannot create temporary file in the output directory.");
    if (ext == "png") write_png(canvas, temp, o);
    else write_vector(canvas, temp, ext, o);
    if (!wxRenameFile(temp, path, o.overwrite)) throw std::runtime_error("Cannot move completed export to its destination.");
    canvas->Refresh(false);
    return true;
  } catch (const std::exception& e) {
    if (!temp.empty() && wxFileExists(temp)) wxRemoveFile(temp);
    error = wxString::FromUTF8(e.what());
    canvas->Refresh(false);
    return false;
  }
}

void add_export_options(wxCmdLineParser& p) {
  p.AddOption("", "export", "Export current view to SVG, PDF or PNG and exit");
  p.AddOption("", "view-settings", "Load batch-style settings but keep the interactive window open");
  p.AddOption("", "self-test-controls", "Run GUI/export integration tests in an existing directory");
  p.AddOption("", "width", "Output width, pixel equivalent (default 3600)", wxCMD_LINE_VAL_NUMBER);
  p.AddOption("", "height", "Output height, pixel equivalent (default 3600)", wxCMD_LINE_VAL_NUMBER);
  p.AddOption("", "dpi", "PNG DPI / vector physical page size (default 600)", wxCMD_LINE_VAL_NUMBER);
  p.AddOption("", "supersample", "PNG antialiasing: 1 or 2 (default 2)", wxCMD_LINE_VAL_NUMBER);
  p.AddSwitch("", "overwrite", "Allow replacement of the named output file");
}
bool parse_export_options(wxCmdLineParser& p) {
  p.Found("export", &cli_export_path);
  p.Found("view-settings", &cli_view_settings);
  p.Found("self-test-controls", &self_test_directory);
  long value;
  const auto read = [&](const char* key, int& dest) {
    if (p.Found(key, &value)) {
      if (value < 0 || value > 100000) return false;
      dest = static_cast<int>(value);
    }
    return true;
  };
  wxString error;
  if (!read("width", cli_options.width) || !read("height", cli_options.height) ||
      !read("dpi", cli_options.dpi) || !read("supersample", cli_options.supersample) ||
      !valid_options(cli_options, error)) {
    fprintf(stderr, "Invalid export settings: %s\n", error.ToUTF8().data()); return false;
  }
  cli_options.overwrite = p.Found("overwrite");
  return true;
}

void run_pending_export(TestGLCanvas* canvas) {
  static bool started = false;
  if (started) return;
  if (!self_test_directory.empty()) {
    started = true;
    run_control_integration_tests(self_test_directory);
    return;
  }
  if (cli_export_path.empty()) return;
  started = true;
  wxString error;
  const bool ok = export_scene(canvas, cli_export_path, cli_options, error);
  if (ok) {
    printf("Exported: %s\n", cli_export_path.ToUTF8().data());
    printf("Settings: %dx%d; %d dpi; PNG SS=%d; EF shift=%g; color_scale=%d\n",
      cli_options.width, cli_options.height, cli_options.dpi, cli_options.supersample, EF, color_scale);
    for (int b = 0; b < nb; ++b) printf("Band index %d: visible=%d, triangles=%d\n", b, draw_band[b], ntri[b]);
  } else fprintf(stderr, "Export failed: %s\n", error.ToUTF8().data());
  fflush(stdout); fflush(stderr);
  std::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

void show_export_dialog(wxWindow* parent) {
  wxDialog dialog(parent, wxID_ANY, "Export current Fermi surface");
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* grid = new wxFlexGridSizer(2, 8, 12);
  const wxSize view = myf->m_canvas->GetClientSize();
  auto* width = new wxSpinCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                              wxSP_ARROW_KEYS, 64, 8192, 3600);
  auto* height = new wxSpinCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 64, 8192, std::max(64, std::min(8192, int(3600.0 * view.y / std::max(1, view.x)))));
  auto* dpi = new wxSpinCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                            wxSP_ARROW_KEYS, 72, 1200, 600);
  auto* ss = new wxChoice(&dialog, wxID_ANY);
  ss->Append("1x (standard)"); ss->Append("2x (smooth edges)"); ss->SetSelection(1);
  auto* format = new wxChoice(&dialog, wxID_ANY);
  format->Append("PDF (vector)"); format->Append("SVG (vector)"); format->Append("PNG (raster)"); format->SetSelection(0);
  const auto row = [&](const wxString& title, wxWindow* control) {
    grid->Add(new wxStaticText(&dialog, wxID_ANY, title), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(control, 1, wxEXPAND);
  };
  row("Format", format); row("Width (pixel equivalent)", width); row("Height (pixel equivalent)", height);
  row("DPI / page size", dpi); row("PNG antialiasing", ss);
  root->Add(grid, 0, wxALL | wxEXPAND, 16);
  auto* note = new wxStaticText(&dialog, wxID_ANY,
    "Exports only the current 3D scene, not the controls.\n"
    "PDF/SVG stay sharp when enlarged; dense meshes produce larger files.\n"
    "Page size = pixels / DPI in inches. Antialiasing applies to PNG only.");
  root->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
  root->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxALIGN_RIGHT, 12);
  dialog.SetSizerAndFit(root);
  if (dialog.ShowModal() != wxID_OK) return;
  const wxString ext = format->GetSelection() == 0 ? "pdf" : format->GetSelection() == 1 ? "svg" : "png";
  wxFileDialog save(parent, "Save Fermi surface", wxGetCwd(),
    wxFileName(frmsf_file_name).GetName() + "_view." + ext,
    ext.Upper() + " files|*." + ext, wxFD_SAVE);
  if (save.ShowModal() != wxID_OK) return;
  wxFileName destination(save.GetPath());
  if (destination.GetExt().empty()) destination.SetExt(ext);
  if (destination.GetExt().Lower() != ext) {
    wxMessageBox("Filename extension must match the selected format.", "Export", wxOK | wxICON_ERROR, parent); return;
  }
  if (destination.FileExists() && wxMessageBox("Replace this existing file?\n" + destination.GetFullPath(),
      "Confirm replacement", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) != wxYES) return;
  ExportOptions o;
  o.width = width->GetValue(); o.height = height->GetValue(); o.dpi = dpi->GetValue();
  o.supersample = ss->GetSelection() + 1; o.overwrite = true;
  wxString error;
  bool ok;
  { wxBusyCursor busy; ok = export_scene(myf->m_canvas, destination.GetFullPath(), o, error); }
  wxMessageBox(ok ? "Saved: " + destination.GetFullPath() : error, "Export",
               wxOK | (ok ? wxICON_INFORMATION : wxICON_ERROR), parent);
}
