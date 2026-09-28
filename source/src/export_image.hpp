// SPDX-License-Identifier: MIT
#ifndef FERMISURFER_EXPORT_IMAGE_HPP
#define FERMISURFER_EXPORT_IMAGE_HPP
#include <wx/string.h>
class wxWindow;
class wxCmdLineParser;
class TestGLCanvas;
struct ExportOptions {
  int width = 3600;
  int height = 3600;
  int dpi = 600;
  int supersample = 2;
  bool overwrite = false;
};
extern wxString cli_export_path;
extern wxString cli_view_settings;
void add_export_options(wxCmdLineParser& parser);
bool parse_export_options(wxCmdLineParser& parser);
bool export_scene(TestGLCanvas* canvas, const wxString& path,
                  const ExportOptions& options, wxString& error);
void show_export_dialog(wxWindow* parent);
void run_pending_export(TestGLCanvas* canvas);
#endif
