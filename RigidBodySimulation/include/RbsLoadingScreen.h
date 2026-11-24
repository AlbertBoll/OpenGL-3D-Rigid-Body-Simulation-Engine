#pragma once
#include "Core/InitializationProgress.h"
#include "Windows/SDLWindow.h"
#include "RbsEditorWidgets.h"
#include "RbsStartup.h"
#include <sdl2/SDL.h>
#include <glad/glad.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace Rbs::Editor
{
    namespace Progress = GEngine::InitializationProgress;

    // An operation owns the existing initialization observer only while its transaction is active.
    class LoadingScreen
    {
    public:
        enum class Operation { Startup, Load, Reload, Reset, Reconstruct };
        enum class Work { Engine, Renderer, Prepare, Assets, Objects, Bodies, Bindings, RestoreBodies, RestoreObjects, Publish, Count };
    private:
        using Clock = std::chrono::steady_clock;
        static constexpr auto index(Work value) { return static_cast<std::size_t>(value); }
        using Weights = std::array<double, static_cast<std::size_t>(Work::Count)>;
        // Millisecond weights come from ordinary startup, warm preset switching and authored Book reload.
        // A plan stays fixed for its lifetime. Only verified units and returned stages earn credit.
        static Weights MakePlan(Operation op, bool convex, bool restore, bool reuseBindings)
        {
            const bool startup = op == Operation::Startup;
            return {startup ? 540.0 : 0.0, startup ? 1775.0 : 0.0,
                    startup ? 0.0 : 12.0, reuseBindings ? 0.0 : (startup ? 1050.0 : 15.0),
                    8.0, convex ? 1370.0 : 1.0, 0.6, restore ? (convex ? 1366.0 : 1.0) : 0.0,
                    restore && !reuseBindings ? 0.5 : 0.0, startup ? 0.5 : 2.7};
        }
        const Operation operation;
        Work work;
        Weights weights;
        double totalWeight{}, completedWeight{}, supported{}, displayed{};
        const Clock::time_point start = Clock::now();
        Clock::time_point previous = start;
        double maxGap{};
        unsigned frames{}, geometryStarted{};
        int lastStage = -1, lastTrace = -1;
        std::array<char, 96> sceneTitle{};
        std::array<char, 128> caption{};
        std::array<char, 256> previousDetail{};
        bool published{}, finished{}, trace{};
        Progress::Scope subscription;

        const char* OperationName() const
        {
            switch (operation) {
            case Operation::Startup: return "Starting Editor";
            case Operation::Load: return "Loading scene";
            case Operation::Reload: return "Reloading scene";
            case Operation::Reset: return "Resetting scene";
            case Operation::Reconstruct: return "Rebuilding scene";
            }
            return "";
        }
        void Enter(Work next)
        {
            // A child callback cannot reset the parent's earned interval.
            if (next <= work) return;
            for (auto i = index(work); i < index(next); ++i) completedWeight += weights[i];
            work = next;
            supported = std::max(supported, completedWeight / totalWeight);
        }
        void Credit(std::uint64_t done, std::uint64_t count)
        {
            if (!count) return;
            const double fraction = double(std::min(done, count)) / double(count);
            supported = std::max(supported, (completedWeight + weights[index(work)] * fraction) / totalWeight);
        }
        // Leave lifecycle events and input releases for normal delivery after publication.
        // The input manager and ImGui SDL backend both need those releases to end a hold.
        struct PendingEvents { Uint32 windowId; bool close{}; };
        static int SDLCALL FilterSceneInput(void* data, SDL_Event* event)
        {
            auto& pending = *static_cast<PendingEvents*>(data);
            if (event->type == SDL_QUIT || (event->type == SDL_WINDOWEVENT &&
                event->window.windowID == pending.windowId && event->window.event == SDL_WINDOWEVENT_CLOSE))
                pending.close = true;
            switch (event->type) {
            case SDL_KEYDOWN: return event->key.windowID != pending.windowId;
            case SDL_TEXTINPUT: return event->text.windowID != pending.windowId;
            case SDL_TEXTEDITING: return event->edit.windowID != pending.windowId;
            case SDL_MOUSEMOTION: return event->motion.windowID != pending.windowId;
            case SDL_MOUSEWHEEL: return event->wheel.windowID != pending.windowId;
            case SDL_MOUSEBUTTONDOWN: return event->button.windowID != pending.windowId;
            case SDL_FINGERDOWN: case SDL_FINGERMOTION: return event->tfinger.windowID != pending.windowId;
            default:
                return 1;
            }
        }
    public:
        LoadingScreen(Operation op, const char* title, RbsScenePreset preset, bool restore = false, bool reuseBindings = false)
            : operation(op), work(op == Operation::Startup ? Work::Engine : Work::Prepare),
              weights(MakePlan(op, preset == RbsScenePreset::BookConformance || preset == RbsScenePreset::SphereDiamond,
                               restore, reuseBindings)), subscription(this, &Observe, true)
        {
            for (const auto weight : weights) totalWeight += weight;
            // CLI preset keys use hyphens. Browser titles already carry their authored spelling.
            bool capitalize = true;
            for (std::size_t i = 0; title[i] && i + 1 < sceneTitle.size(); ++i) {
                const char c = title[i];
                sceneTitle[i] = c == '-' ? ' ' : (capitalize && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c);
                capitalize = c == '-' || c == ' ';
            }
            std::snprintf(caption.data(), caption.size(), "%s   /   %s", OperationName(), sceneTitle.data());
            trace = SDL_getenv("GENGINE_RBS_LOADING_TRACE") != nullptr;
            fontsInitialized = operation != Operation::Startup;
            if (fontsInitialized) {
                auto& io = ImGui::GetIO();
                io.ClearInputKeys();
                for (int button = 0; button < IM_ARRAYSIZE(io.MouseDown); ++button)
                    if (io.MouseDown[button]) io.AddMouseButtonEvent(button, false);
            }
        }
        ~LoadingScreen() { if (!finished) Finish(false); }
        LoadingScreen(const LoadingScreen&) = delete;
        LoadingScreen& operator=(const LoadingScreen&) = delete;
        bool fontsInitialized{}, cancelled{};
        static LoadingScreen* Current()
        {
            return Progress::observer == &Observe ? static_cast<LoadingScreen*>(Progress::context) : nullptr;
        }
        static void Stage(Work milestone, Progress::Stage stage, std::string_view detail,
                          std::uint64_t completed = 0, std::uint64_t total = 0)
        {
            if (auto* screen = Current()) {
                screen->Enter(milestone);
                screen->Credit(completed, total);
                Progress::Report(stage, detail, completed, total);
            }
        }
        static void Observe(void* user, const Progress::State& state)
        {
            static_cast<LoadingScreen*>(user)->Draw(state);
        }
        void Draw(const Progress::State& state)
        {
            using namespace GEngine;
            if (!state.window) return;
            const bool startup = operation == Operation::Startup;
            const bool changed = lastStage != int(state.stage) || previousDetail != state.detail;
            if (work == Work::Engine) Enter(Work::Renderer); // Existing SDL window, GL context and ImGui are ready.
            if (work == Work::Renderer && changed && std::string_view(state.detail.data()).ends_with(" geometry")) {
                // The next registration starts only after its predecessor succeeded. The 23 reported
                // registrations plus the final platform/resource handoff form this nested interval.
                Credit(geometryStarted++, 24);
            }
            if (state.total) Credit(state.completed, state.total);
            auto* window = static_cast<SDLWindow*>(state.window);
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            constexpr const char* names[]{"Renderer", "Physics", "Scene", "Editor"};
            if (changed) {
                if (!startup)
                    (void)TryPrintDiagnosticLine(stdout, "RBS_SCENE_LOAD_STAGE scene={} stage={} elapsed_ms={:.3f} detail={} completed={} total={}",
                        sceneTitle.data(), names[int(state.stage)], elapsed * 1000, state.detail.data(), state.completed, state.total);
                else if (lastStage != int(state.stage))
                    (void)TryPrintDiagnosticLine(stdout, "RBS_LOADING stage={} elapsed_ms={:.3f} detail={}",
                        unsigned(state.stage), elapsed * 1000, state.detail.data());
                if (!startup || lastStage != int(state.stage)) std::fflush(stdout);
                previousDetail = state.detail;
                lastStage = int(state.stage);
            }
            if (!startup) {
                SDL_PumpEvents();
                PendingEvents pending{window->GetWindowID()};
                SDL_FilterEvents(&FilterSceneInput, &pending);
                if (pending.close && !published) { cancelled = true; Progress::state.cancelled = true; return; }
            } else if (!published) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    window->GetImGuiWindow()->HandleSDLEvent(event);
                    if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                        cancelled = true; Progress::state.cancelled = true; return;
                    }
                    if (event.type == SDL_WINDOWEVENT) window->RefreshDimensions();
                }
            }
            if (!published && frames && now - previous < std::chrono::milliseconds(33)) return;
            if (!window->BeginRender()) { Progress::state.cancelled = true; return; }
            if (!fontsInitialized) { Initialize(); fontsInitialized = true; }
            const double dt = std::chrono::duration<double>(now - previous).count();
            // Interpolate only already-earned work. The first drawable frame is zero and commit
            // presents 100 immediately, even when a short transaction fits inside one refresh.
            displayed = published ? 1.0 : (frames ? std::min(supported, displayed + dt * 1000.0 / totalWeight) : 0.0);
            const int percent = published ? 100 : std::clamp(int(displayed * 100.0), 0, 99);
            maxGap = std::max(maxGap, dt); previous = now; ++frames;
            GLint framebuffer{},viewport[4];glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
            window->RefreshDimensions();const auto pixels=window->GetFramebufferPixelSize();
            struct RestoreTarget {
                GLint framebuffer;
                const GLint* viewport;
                ~RestoreTarget() {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);
                    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
                }
            } restore{framebuffer,viewport};
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,pixels.Width,pixels.Height);
            if (startup) {
                glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glStencilMask(0xFF);
                glClearColor(20.f/255,26.f/255,32.f/255,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            }
            if(!window->BeginUI()){InitializationProgress::state.cancelled=true;return;}
            BeginFrame();
            auto* view=ImGui::GetMainViewport();auto* draw=ImGui::GetForegroundDrawList(view);
            const auto size=view->Size,origin=view->Pos;
            draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,26,32,255));
            const float width = std::min(Px(690), std::max(Px(80), size.x - Px(56)));
            const float left = origin.x + (size.x - width) * .5f, top = origin.y + size.y * .5f - Px(85);
            draw->AddText(Bold[3], Px(30), {left, top}, IM_COL32(230,237,246,255), "GEngine");
            draw->AddText({left,top+Px(44)}, IM_COL32(154,177,200,255), caption.data());
            const float y = top + Px(100);
            if (!published) {
                const float radius = Px(5), angle = float(std::fmod(elapsed * 5.0, 6.283185307));
                const ImVec2 center{left + radius, y + ImGui::GetTextLineHeight() * .5f};
                draw->PathArcTo(center, radius, angle, angle + 4.4f, 16);
                draw->PathStroke(IM_COL32(91,166,236,255), 0, Px(1.8f));
            }
            draw->AddText({left + (published ? 0 : Px(19)), y}, IM_COL32(205,216,229,255), state.detail.data());
            char value[72]{};
            if (state.total && !published) {
                const char* unit = work == Work::Assets ? "textures" : work == Work::Bindings ? "bindings" : "bodies";
                std::snprintf(value, sizeof(value), "%llu / %llu %s    %d%%",
                    static_cast<unsigned long long>(state.completed), static_cast<unsigned long long>(state.total), unit, percent);
            } else std::snprintf(value, sizeof(value), "%d%%", percent);
            draw->AddText({left + width - ImGui::CalcTextSize(value).x, y}, IM_COL32(181,201,221,255), value);
            const ImVec2 bar{left, y + Px(30)}; const float h = Px(9);
            draw->AddRectFilled(bar, {bar.x+width,bar.y+h}, IM_COL32(43,53,66,255), h*.5f);
            if (displayed > 0)
                draw->AddRectFilled(bar, {bar.x+width*float(displayed),bar.y+h}, IM_COL32(70,151,229,255), h*.5f);
            EndFrame();
            if (!window->EndUI()) { Progress::state.cancelled = true; return; }
            window->SwapBuffer();
            if (trace && (lastTrace != percent || changed)) {
                (void)TryPrintDiagnosticLine(stdout, "RBS_GLOBAL_PROGRESS op={} scene={} percent={} supported={:.6f} committed={} detail={}",
                    OperationName(), sceneTitle.data(), percent, published ? 1.0 : supported, int(published), state.detail.data());
                std::fflush(stdout); lastTrace = percent;
            }
        }
        void Finish(bool success = true)
        {
            if (finished) return;
            finished = true;
            if (success) {
                published = true;
                // Publication/readiness has already succeeded. The terminal frame never waits
                // for interpolation and a close arriving here belongs to the new ordinary frame.
                auto finalState = Progress::state;
                finalState.stage = Progress::Stage::Editor;
                finalState.detail = {'R','e','a','d','y'};
                finalState.completed = finalState.total = 0;
                Draw(finalState);
            }
            const auto now = Clock::now();
            maxGap = std::max(maxGap, std::chrono::duration<double>(now - previous).count());
            const double ms = std::chrono::duration<double, std::milli>(now - start).count();
            if (operation == Operation::Startup)
                (void)TryPrintDiagnosticLine(stdout, "RBS_LOADING_{} elapsed_ms={:.3f} frames={} max_pump_gap_ms={:.3f}",
                    success ? "COMPLETE" : "ROLLBACK", ms, frames, maxGap*1000);
            else
                (void)TryPrintDiagnosticLine(stdout, "RBS_SCENE_LOAD_{} scene={} elapsed_ms={:.3f} frames={} max_pump_gap_ms={:.3f}",
                    success ? "COMPLETE" : "ROLLBACK", sceneTitle.data(), ms, frames, maxGap*1000);
            std::fflush(stdout);
        }
    };
}
