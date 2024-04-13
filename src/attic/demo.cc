// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see https://www.gnu.org/licenses/.

#include "src/core/base/base.hh"
#include "src/core/gui/gui.hh"
#include "vendor/imgui/imgui.h"

namespace RG {

int Main(int argc, char **argv)
{
    gui_Window win;

    win.Create("hello");
    win.InitImGui();

    char txt1[256] = {};
    char txt2[256] = {};

    while (win.ProcessEvents(false)) {
        ImGui::Begin("My Window");
        ImGui::Text("Hello, world!");
        ImGui::InputText("Text1", txt1, RG_SIZE(txt1));
        ImGui::InputText("Text2", txt2, RG_SIZE(txt2));
        ImGui::End();

        win.RenderImGui();
        win.SwapBuffers();
    }

    return 0;
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::RunApp(argc, argv); }
