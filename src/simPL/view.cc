// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../libcc/libcc.hh"
#include "simPL.hh"
#include "view.hh"
#include "../../lib/imgui/imgui.h"

static void InitializeSimulation(Simulation *out_simulation)
{
    static int simulations_id = 0;

    Fmt(out_simulation->name, "Simulation #%1", ++simulations_id);
    out_simulation->pause = true;
#ifdef SIMPL_ENABLE_HOT_RELOAD
    out_simulation->auto_reset = true;
#endif

    InitializeConfig_(20000, 0, &out_simulation->config);
}

void RenderMainMenu(HeapArray<Simulation> *simulations)
{
    ImGui::BeginMainMenuBar();

    if (ImGui::MenuItem("New simulation")) {
        Simulation *simulation = simulations->AppendDefault();
        InitializeSimulation(simulation);
    }

    ImGui::EndMainMenuBar();
}

bool RenderSimulationWindow(HeapArray<Simulation> *simulations, Size idx)
{
    Simulation *simulation = &(*simulations)[idx];

    bool open = true;
    ImGui::Begin(simulation->name, &open, ImVec2(500, 500));

    ImGui::Columns(3, nullptr, false);
    ImGui::TextUnformatted(Fmt(&frame_alloc, "Humans: %1", simulation->humans.len).ptr); ImGui::NextColumn();
    ImGui::TextUnformatted(Fmt(&frame_alloc, "Alive: %1", simulation->alive_count).ptr); ImGui::NextColumn();
    ImGui::TextUnformatted(Fmt(&frame_alloc, "Iteration: %1", simulation->iteration).ptr); ImGui::NextColumn();
    ImGui::Columns(1);

    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputInt("Count", &simulation->config.count);
        ImGui::InputInt("Seed", &simulation->config.seed);

        if (ImGui::Button("Start")) {
            simulation->Reset();
            simulation->pause = false;
        }
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Pause", &simulation->pause);
        if (ImGui::Button("Reset")) {
            simulation->Reset();
        }
#ifdef SIMPL_ENABLE_HOT_RELOAD
        ImGui::SameLine();
        ImGui::Checkbox("Auto-reset", &simulation->auto_reset);
#endif
        if (ImGui::Button("Copy")) {
            Simulation *copy = simulations->AppendDefault();
            simulation = &(*simulations)[idx]; // May have been reallocated

            InitializeSimulation(copy);
            copy->config = simulation->config;
        }
    }

    if (ImGui::CollapsingHeader("Results", ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTabBar("ResultTabs")) {
        if (ImGui::BeginTabItem("Deaths")) {
            float deaths[ARRAY_SIZE(DeathTypeNames) + 1][6] = {};
            for (const Human &human: simulation->humans) {
                if (!human.alive) {
                    int age_cat = 0;
                    if (human.age < 45) continue;
                    else if (human.age < 55) age_cat = 0;
                    else if (human.age < 65) age_cat = 1;
                    else if (human.age < 75) age_cat = 2;
                    else if (human.age < 85) age_cat = 3;
                    else if (human.age < 95) age_cat = 4;
                    else age_cat = 5;

                    deaths[0][age_cat] += 1.0f;
                    deaths[(int)human.death_type + 1][age_cat] += 1.0f;
                }
            }

            ImGui::Columns(7, "Table");
            ImGui::Separator();
            ImGui::Text("Cause"); ImGui::NextColumn();
            ImGui::Text("45-54"); ImGui::NextColumn();
            ImGui::Text("55-64"); ImGui::NextColumn();
            ImGui::Text("65-74"); ImGui::NextColumn();
            ImGui::Text("75-84"); ImGui::NextColumn();
            ImGui::Text("85-94"); ImGui::NextColumn();
            ImGui::Text("95+"); ImGui::NextColumn();
            ImGui::Separator();
            for (Size i = 0; i < ARRAY_SIZE(DeathTypeNames); i++) {
                ImGui::TextUnformatted(DeathTypeNames[i]); ImGui::NextColumn();
                for (Size j = 0; j < 6; j++) {
                    if (deaths[0][j]) {
                        float proportion = (deaths[i + 1][j] / deaths[0][j]) * 100.0f;
                        const char *str = Fmt(&frame_alloc, "%1 (%2%%)",
                                              deaths[i + 1][j], FmtDouble(proportion, 1)).ptr;

                        ImGui::TextUnformatted(str); ImGui::NextColumn();
                    } else {
                        ImGui::TextUnformatted("-"); ImGui::NextColumn();
                    }
                }
            }
            ImGui::Columns(1);
            ImGui::Separator();

            // It's actually off by one, because 0 is 'All'
            static int histogram_type = 0;
            ImGui::Combo("Type", &histogram_type, [](void *, int idx, const char **str) {
                *str = idx ? DeathTypeNames[idx - 1] : "All";
                return true;
            }, nullptr, ARRAY_SIZE(DeathTypeNames) + 1);
            ImGui::PlotHistogram("Histogram", deaths[histogram_type], ARRAY_SIZE(deaths[histogram_type]), 0,
                                 nullptr, 0.0f, (float)simulation->humans.len, ImVec2(0, 80));

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Risk factors")) {
            float population[3] = {};
            float smokers[3] = {};
            for (const Human &human: simulation->humans) {
                if (human.age >= 65) population[2] += 1.0f;
                if (human.age >= 55) population[1] += 1.0f;
                if (human.age >= 45) population[0] += 1.0f;

                if (human.smoking_status || human.smoking_cessation_age) {
                    int age = human.smoking_cessation_age ? human.smoking_cessation_age : human.age;

                    if (age >= 65) smokers[2] += 1.0f;
                    if (age >= 55) smokers[1] += 1.0f;
                    if (age >= 45) smokers[0] += 1.0f;
                }
            }

            ImGui::Columns(4, "Table");
            ImGui::Separator();
            ImGui::Text(""); ImGui::NextColumn();
            ImGui::Text("45-54"); ImGui::NextColumn();
            ImGui::Text("55-64"); ImGui::NextColumn();
            ImGui::Text("65-74"); ImGui::NextColumn();
            ImGui::Separator();
            ImGui::Text("Smokers"); ImGui::NextColumn();
            for (Size i = 0; i < ARRAY_SIZE(smokers); i++) {
                if (population[i]) {
                    float proportion = (smokers[i] / population[i]) * 100.0f;
                    const char *str = Fmt(&frame_alloc, "%1 (%2%%)",
                                          smokers[i], FmtDouble(proportion, 1)).ptr;
                    ImGui::TextUnformatted(str); ImGui::NextColumn();
                } else {
                    ImGui::Text("-"); ImGui::NextColumn();
                }
            }
            ImGui::Columns(1);
            ImGui::Separator();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    return open;
}
