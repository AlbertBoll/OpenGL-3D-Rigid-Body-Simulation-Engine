#pragma once

// This header is compiled only by the Phase 17 RBS diagnostic overlay.
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
#if !defined(GENGINE_LIFECYCLE_VALIDATION) || defined(GENGINE_LIFECYCLE_CONTROL) || defined(GENGINE_CONFIG_RELEASE)
#error Case59 observation requires the separately admitted Debug diagnostic lane.
#endif
#include <imgui/imgui_internal.h>
#include <atomic>
namespace PreEditorCase59Widget
{
    inline void (*observed)() noexcept = nullptr;
    inline ImRect item{}, frame{};
    inline ImGuiID id{};
    inline int generation{};
    inline bool active{}, changed{};
}
namespace ImGui
{
    inline bool PreEditorCase59InputText(const char* label, char* buffer, size_t capacity,
        ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* data = nullptr)
    {
        // Only the test fixture's single InputText call is wrapped. Preserve its
        // arguments, result and call count. No focus/item/input mutation is added.
        const float right = GetCurrentWindow()->DC.CursorPos.x + CalcItemWidth();
        const bool result = InputText(label, buffer, capacity, flags, callback, data);
        if (PreEditorCase59Widget::observed)
        {
            auto& c = *GetCurrentContext();
            PreEditorCase59Widget::item = c.LastItemData.Rect;
            PreEditorCase59Widget::frame = {c.LastItemData.Rect.Min, {right, c.LastItemData.Rect.Max.y}};
            PreEditorCase59Widget::id = c.LastItemData.ID;
            PreEditorCase59Widget::generation = c.FrameCount;
            PreEditorCase59Widget::active = IsItemActive();
            PreEditorCase59Widget::changed = result;
            PreEditorCase59Widget::observed();
        }
        return result;
    }
}
#define InputText PreEditorCase59InputText
#endif
#include "InputRoutingChecks.h"
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
#undef InputText
#endif
#include "CameraSnapshotChecks.h"
#include <filesystem>
#include <array>
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
#if !defined(GENGINE_LIFECYCLE_VALIDATION) || defined(GENGINE_LIFECYCLE_CONTROL) || !defined(GENGINE_CONFIG_RELEASE) || defined(GENGINE_LIFECYCLE_FOCUS_TRACE)
#error First-pick observation requires the separately admitted Release diagnostic lane.
#endif
#include <array>
#include <cstdint>
#include <cstring>
#include <locale>
#include <type_traits>
#endif

namespace PreEditorLifecycle
{
    using namespace ::GEngine;
    using namespace ::GEngine::Manager;

    // Both lanes call the unchanged Phase 15 measurement function. The Phase 17
    // control is the approved Phase 16 input path, not Phase 15's legacy control.
    inline PlatformResult Measure(BaseApp& app, const char* role)
    {
        if (auto result = PreEditorInput::Measure(app); !result)
            return result;
        const auto destination = std::string("input-cost-") + role + ".csv";
        std::error_code error;
        std::filesystem::rename("input-cost.csv", destination, error);
        if (error)
            return PreEditorInput::Failure("Phase 17 cost archive failed");
        return {};
    }

#ifndef GENGINE_LIFECYCLE_CONTROL
    using namespace ::GEngine::Component;
    struct Probe
    {
        unsigned updates{}, segment{}, applied = UINT_MAX, stable{}, observed{}, readbacks{}, checks{}, invalidStage{};
        bool initialized{}, waitingRestore{}, pendingPick{}, ui{}, uiStarted{}, costDone{};
        glm::uvec2 previousSize{};
        glm::dvec2 initialLogical{};
        glm::vec3 editorPosition{}, uiPosition{};
        glm::quat editorOrientation{};
        float editorDistance{};
        bool uiPaused{}, uiDebug{};
        std::uint32_t uiPicked{};
        EntityRenderId expected{}, replaced{};
        Subscription focusTrace;
        std::optional<FrameCamera> retained;
        std::ofstream rows{"lifecycle-frames.csv"};
        std::ofstream cases{"lifecycle-checks.csv"};
        Probe()
        {
            rows << "segment,frame,width,height,scale,identity_index,identity_generation,identity_domain,source,regenerated,background\n";
            cases << "case,pass\n";
        }
    };
    inline Probe probe;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    namespace PickTrace
    {
        constexpr unsigned Capacity = 2048;
        struct Mouse
        {
            double x{}, y{};
            bool pressed{}, released{}, held{};
            unsigned modifiers{};
        };
        struct Record
        {
            const char* kind{}; // Only static string literals are stored.
            std::uint64_t ordinal{}, sequence{};
            std::int64_t result{-1};
            unsigned updates{}, segment{}, stable{}, observed{}, uiGeneration{}, capture{};
            bool focused{};
            InputRouting routing{}, reportedRouting{};
            Mouse physical{}, view{};
            std::array<std::uint64_t, 12> u{};
            std::array<double, 12> v{};
        };
        static_assert(std::is_trivially_copyable_v<Record>);
        static_assert(sizeof(Record) * Capacity <= 1024 * 1024);
        inline std::array<Record, Capacity> records{};
        inline unsigned count{}, overflow{}, uiGeneration{};
        inline std::uint64_t armOrdinal{}, finishOrdinal{};
        inline bool begun{}, enabled{}, finished{}, attached{}, detached{}, afterCost{}, firstPick{};
        inline int firstPickPassed = -1;
        inline BaseApp* app{};
        inline Subscription observer;

        inline Mouse MouseValue(const InputState& state) noexcept
        {
            const auto& mouse = state.m_Mouse;
            const auto position = mouse.GetPosition();
            return {position.x, position.y, mouse.isButtonPressed(GENGINE_BUTTON_LEFT),
                    mouse.isButtonReleased(GENGINE_BUTTON_LEFT), mouse.isButtonHeld(GENGINE_BUTTON_LEFT),
                    state.modifiers};
        }
        inline Record* Next(const char* kind) noexcept
        {
            if (!enabled || finished) return nullptr;
            if (count == Capacity) { ++overflow; enabled = false; return nullptr; }
            auto& row = records[count++];
            row.kind = kind; row.ordinal = count;
            row.updates = probe.updates; row.segment = probe.segment;
            row.stable = probe.stable; row.observed = probe.observed; row.uiGeneration = uiGeneration;
            const auto& input = *app->GetInputManager();
            row.routing = input.Routing(); row.focused = input.Focused();
            row.capture = unsigned(input.PointerCapture());
            row.physical = MouseValue(input.GetPhysicalState());
            row.view = MouseValue(input.GetViewState());
            return &row;
        }
        inline void Arm(BaseApp& owner) noexcept
        {
            if (begun) return;
            begun = true; app = &owner; afterCost = probe.costDone;
            // This is reached only after both original Release cost archives exist.
            auto subscription = owner.GetInputManager()->Observe([](const InputEvent& event) noexcept
            {
                if (auto* row = Next("routed"))
                {
                    row->sequence = event.sequence;
                    row->u = {unsigned(event.kind), event.window, unsigned(event.key), unsigned(event.button),
                              event.modifiers, unsigned(event.destination), unsigned(event.cancellation),
                              event.repeated, event.handled, event.cleanup};
                    row->v = {event.x, event.y, double(event.dx), double(event.dy), event.wheelX, event.wheelY};
                }
            });
            attached = bool(subscription);
            if (subscription) observer = std::move(*subscription);
            enabled = true;
            if (auto* row = Next("armed"))
            { row->result = attached; row->u[0] = afterCost; armOrdinal = row->ordinal; }
        }
        inline void JsonString(std::ostream& out, const char* value)
        {
            constexpr char digits[] = "0123456789abcdef";
            out << '"';
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value ? value : ""); *p; ++p)
            {
                if (*p == '"' || *p == '\\') out << '\\' << char(*p);
                else if (*p < 32) out << "\\u00" << digits[*p >> 4] << digits[*p & 15];
                else out << char(*p);
            }
            out << '"';
        }
        inline void NumberJson(std::ostream& out, double value)
        { if (std::isfinite(value)) out << value; else out << "null"; }
        inline void RoutingJson(std::ostream& out, const InputRouting& routing)
        {
            out << "{\"window\":" << routing.window << ",\"left\":"; NumberJson(out, routing.left);
            out << ",\"top\":"; NumberJson(out, routing.top);
            out << ",\"right\":"; NumberJson(out, routing.right);
            out << ",\"bottom\":"; NumberJson(out, routing.bottom);
            out << ",\"finite\":" << (std::isfinite(routing.left) && std::isfinite(routing.top) &&
                                          std::isfinite(routing.right) && std::isfinite(routing.bottom))
                << ",\"visible\":" << routing.visible
                << ",\"focused\":" << routing.focused << ",\"uiMouse\":" << routing.uiMouse
                << ",\"uiKeyboard\":" << routing.uiKeyboard << '}';
        }
        inline void MouseJson(std::ostream& out, const Mouse& mouse)
        {
            out << "{\"x\":"; NumberJson(out, mouse.x);
            out << ",\"y\":"; NumberJson(out, mouse.y);
            out << ",\"finite\":" << (std::isfinite(mouse.x) && std::isfinite(mouse.y))
                << ",\"pressed\":" << mouse.pressed
                << ",\"released\":" << mouse.released << ",\"held\":" << mouse.held
                << ",\"modifiers\":" << mouse.modifiers << '}';
        }
        inline void Finish(const char* reason) noexcept
        {
            if (!begun || finished) return;
            if (auto* row = Next("finished")) { row->result = firstPickPassed; finishOrdinal = row->ordinal; }
            enabled = false; finished = true;
            detached = !attached || bool(observer.Reset());
            try
            {
                const auto path = "lifecycle-first-pick-trace.json";
                if (std::filesystem::exists(path))
                { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_17_PICK_TRACE_DUMP_FAIL existing_destination"); return; }
                std::ofstream output(path);
                output.imbue(std::locale::classic());
                output << std::boolalpha << std::setprecision(17);
                output << "{\"schema\":1,\"diagnostic_only\":true,\"acceptance_eligible\":false,"
                       << "\"guard\":\"GENGINE_LIFECYCLE_PICK_TRACE\",\"armed\":" << begun
                       << ",\"armed_after_cost_batches\":" << afterCost << ",\"finished\":" << finished
                       << ",\"attach_success\":" << attached << ",\"detach_success\":" << detached
                       << ",\"first_pick_observed\":" << firstPick << ",\"first_pick_passed\":";
                if (firstPickPassed < 0) output << "null"; else output << bool(firstPickPassed);
                output << ",\"finish_reason\":"; JsonString(output, reason);
                output << ",\"capacity\":" << Capacity << ",\"record_buffer_bytes\":" << sizeof(records)
                       << ",\"count\":" << count << ",\"overflow\":" << overflow
                       << ",\"arm_ordinal\":" << armOrdinal << ",\"finish_ordinal\":" << finishOrdinal
                       << ",\"payload_layouts\":{"
                       << "\"routed_u\":[\"kind\",\"window\",\"key\",\"button\",\"modifiers\",\"destination\",\"cancellation\",\"repeated\",\"handled\",\"cleanup\"],"
                       << "\"routed_v\":[\"x\",\"y\",\"dx\",\"dy\",\"wheelX\",\"wheelY\"],"
                       << "\"ui_u\":[\"activeId\",\"activeWindowId\",\"movingWindowId\",\"viewportMoveId\",\"navToggle\",\"flags\",\"queueSize\",\"queueHeadTypePlusOne\",\"mouseDown0\",\"mouseDown1\",\"keyAlt\"],"
                       << "\"ui_flags\":[\"active\",\"popup\",\"text\",\"viewBackground\",\"viewGesture\",\"visible\",\"viewportFocused\",\"viewportHovered\",\"wantMouse\",\"wantKeyboard\",\"wantText\"],"
                       << "\"ui_v\":[\"mouseX\",\"mouseY\"],"
                       << "\"ui_region_v\":[\"left\",\"top\",\"right\",\"bottom\"],"
                       << "\"pick_prepare_u\":[\"pixelPresent\",\"pixelX\",\"pixelY\"],"
                       << "\"pick_prepare_v\":[\"originX\",\"originY\",\"logicalWidth\",\"logicalHeight\",\"pixelWidth\",\"pixelHeight\"],"
                       << "\"pick_conversion_u\":[\"pixelX\",\"pixelY\",\"errorCodePlusOne\"],"
                       << "\"pick_ready_u\":[\"pixelX\",\"pixelY\"],"
                       << "\"pick_readback_enter_u\":[\"pixelPresent\",\"snapshotPresent\",\"pixelX\",\"pixelY\"],"
                       << "\"pick_readback_result_u\":[\"errorCodePlusOne\"],\"pick_readback_result_v\":[\"pixelValue\"],"
                       << "\"pick_resolve_u\":[\"index\",\"generation\",\"registry\",\"errorCodePlusOne\"],"
                       << "\"pick_entity_u\":[\"entity\",\"errorCodePlusOne\"],"
                       << "\"pick_hovered_u\":[\"entity\"],"
                       << "\"pending_pick_u\":[\"hoveredPresent\",\"hoveredEntity\",\"nameEvaluated\",\"nameMismatch\"],"
                       << "\"target_u\":[\"index\",\"generation\",\"registry\"],\"warp_focus_v\":[\"logicalX\",\"logicalY\"]},\"records\":[";
                for (unsigned i = 0; i != count; ++i)
                {
                    const auto& row = records[i];
                    if (i) output << ',';
                    output << "{\"kind\":"; JsonString(output, row.kind);
                    output << ",\"ordinal\":" << row.ordinal << ",\"sequence\":" << row.sequence
                           << ",\"result\":" << row.result << ",\"updates\":" << row.updates
                           << ",\"segment\":" << row.segment << ",\"stable\":" << row.stable
                           << ",\"observed\":" << row.observed << ",\"uiGeneration\":" << row.uiGeneration
                           << ",\"focused\":" << row.focused << ",\"capture\":" << row.capture << ",\"routing\":";
                    RoutingJson(output, row.routing); output << ",\"reported_routing\":";
                    RoutingJson(output, row.reportedRouting); output << ",\"physical\":";
                    MouseJson(output, row.physical); output << ",\"view\":"; MouseJson(output, row.view);
                    if (std::strcmp(row.kind, "injected") == 0)
                    {
                        output << ",\"native_type\":" << row.u[0] << ",\"window\":" << row.u[1]
                               << ",\"button\":" << row.u[2] << ",\"x\":";
                        NumberJson(output, row.v[0]); output << ",\"y\":"; NumberJson(output, row.v[1]);
                    }
                    output << ",\"u\":[";
                    for (unsigned j = 0; j != row.u.size(); ++j) { if (j) output << ','; output << row.u[j]; }
                    output << "],\"v\":[";
                    for (unsigned j = 0; j != row.v.size(); ++j)
                    { if (j) output << ','; NumberJson(output, row.v[j]); }
                    output << "],\"v_finite\":[";
                    for (unsigned j = 0; j != row.v.size(); ++j) { if (j) output << ','; output << std::isfinite(row.v[j]); }
                    output << "]}";
                }
                output << "],\"dump_complete\":true}\n";
                output.close();
                if (output) Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_PICK_TRACE_DUMP_COMPLETE records={} overflow={}", count, overflow);
                else Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_17_PICK_TRACE_DUMP_FAIL write_or_close");
            }
            catch (...) { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_17_PICK_TRACE_DUMP_FAIL exception"); }
        }
    }
    // Defined in this single diagnostic TU; other admitted TUs only declare bridges.
    bool PickTraceActive() noexcept { return PickTrace::enabled && !PickTrace::finished; }
    void PickTraceValues(const char* kind, std::int64_t result = -1,
                         const std::array<std::uint64_t, 12>& words = {},
                         const std::array<double, 12>& values = {}) noexcept
    {
        if (auto* row = PickTrace::Next(kind)) { row->result = result; row->u = words; row->v = values; }
    }
    void PickTraceUI(const char* kind, const InputRouting& routing, int result,
                     unsigned flags, unsigned viewportMoveId) noexcept
    {
        auto* row = PickTrace::Next(kind);
        if (!row) return;
        auto& context = *ImGui::GetCurrentContext();
        const auto& io = context.IO;
        row->result = result; row->reportedRouting = routing;
        row->u = {context.ActiveId, context.ActiveIdWindow ? context.ActiveIdWindow->ID : 0,
                  context.MovingWindow ? context.MovingWindow->ID : 0, viewportMoveId,
                  context.NavWindowingToggleLayer, flags | (unsigned(io.WantCaptureMouse) << 8) |
                  (unsigned(io.WantCaptureKeyboard) << 9) | (unsigned(io.WantTextInput) << 10),
                  std::uint64_t(context.InputEventsQueue.Size), context.InputEventsQueue.Size ?
                  std::uint64_t(context.InputEventsQueue[0].Type) + 1 : 0,
                  io.MouseDown[0], io.MouseDown[1], io.KeyAlt};
        row->v = {io.MousePos.x, io.MousePos.y};
    }
    void PickTraceUIBegin() noexcept
    {
        if (!PickTrace::enabled || PickTrace::finished) return;
        ++PickTrace::uiGeneration; PickTraceValues("ui_begin");
    }
    void FinishPickTrace(const char* reason) noexcept { PickTrace::Finish(reason); }
#endif
    inline bool Check(bool value, const char* name)
    {
        ++probe.checks;
        probe.cases << name << ',' << value << '\n';
        probe.cases.flush();
        if (!value) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_17_CHECK_FAIL {}", name);
        return value && bool(probe.cases);
    }
    inline PlatformResult Failure(const char* name)
    {
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase 17 lifecycle", name});
    }
    inline float Scale() { return probe.segment == 3 ? 1.25f : probe.segment == 4 ? 1.5f : 1.f; }
    bool UIActive() { return probe.ui; }
    bool GpuChecks() { return !probe.ui && probe.initialized && probe.segment < 12; }
    inline glm::dquat Orientation(bool rotated = false)
    {
        return glm::dquat(glm::quat(glm::vec3(-.12f, rotated ? -.12f : 0.f, 0)));
    }
    inline glm::dvec3 Position(bool rotated = false)
    {
        return glm::dvec3(0, 7, 6) - PreEditorCamera::Rotate(Orientation(rotated), {0, 0, -1}) * 28.;
    }
    // Trace markers are Phase17 Amendment03 only. Ordinary and Phase15/16 input
    // consumers retain their original code. Values are observations, never gates.
    inline void FocusMark(unsigned kind, std::uint64_t a = 0, std::uint64_t b = 0,
                          std::uint64_t c = 0, std::uint64_t d = 0)
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::Mark(kind, a, b, c, d);
#endif
    }
    inline void FocusFrame()
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::SetFrame(probe.updates, probe.segment, probe.stable, probe.observed);
#endif
    }
    inline void FocusSample(unsigned kind, BaseApp& app)
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::Sample(kind, int(app.GetInputManager()->Focused()));
#endif
    }
    inline int Push(SDL_Event& event, unsigned origin)
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::Injection injection(origin);
        const auto detail = event.type == SDL_WINDOWEVENT ? unsigned(event.window.event) :
            event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ? unsigned(event.key.keysym.scancode) : 0u;
        FocusMark(112, event.type, event.window.windowID, detail, origin);
#endif
        const auto result = SDL_PushEvent(&event);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        if (origin == 26)
            PickTraceValues("injected", result, {event.type, event.button.windowID, event.button.button},
                            {double(event.button.x), double(event.button.y)});
#endif
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        FocusMark(113, std::uint64_t(std::int64_t(result)), event.type, event.window.windowID, detail);
#endif
        return result;
    }
    inline bool NativeTap(BaseApp& app, SDL_Scancode scan)
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::Injection injection(20);
        FocusMark(114, unsigned(scan));
#endif
        const bool result = PreEditorInput::NativeTap(app, scan);
        FocusMark(115, result, unsigned(scan));
        return result;
    }
    inline bool ActivateNative(SDL_Window* native, BaseApp& app)
    {
        FocusSample(106, app);
        const bool result = PreEditorCamera::ActivateNativeWindow(native);
        FocusMark(107, result);
        FocusSample(118, app);
        return result;
    }
    inline bool RestoreNativeExposureAfterResize(SDL_Window* window)
    {
        const HWND owned = PreEditorCamera::nativeFixture.window;
        if (!window || !owned) return false;
        SDL_SysWMinfo native{};
        SDL_VERSION(&native.version);
        if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_WINDOWS)
            return false;
        DWORD process = 0;
        if (native.info.win.window != owned || !IsWindow(owned) ||
            !GetWindowThreadProcessId(owned, &process) || process != GetCurrentProcessId())
            return false;
        // SDL resize requests NOTOPMOST. Restore this existing native exposure
        // lease without activating, changing its baseline or acquiring focus.
        return SetWindowPos(owned, HWND_TOPMOST, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
    }
    inline bool ForegroundHandoff(BaseApp& app)
    {
        FocusMark(104, PreEditorInput::nativeForeground.held, PreEditorCamera::nativeFixture.locked,
                  reinterpret_cast<std::uintptr_t>(PreEditorInput::nativeForeground.window),
                  reinterpret_cast<std::uintptr_t>(PreEditorCamera::nativeFixture.window));
        const bool released = PreEditorInput::nativeForeground.Release();
        // Same short circuit as Candidate04: no camera Acquire after a failed release.
        const int acquired = released ? int(PreEditorCamera::AcquireNativeForeground()) : -1;
        FocusMark(105, released, std::uint64_t(std::int64_t(acquired)),
                  PreEditorInput::nativeForeground.held, PreEditorCamera::nativeFixture.locked);
        FocusSample(120, app);
        return released && acquired == 1;
    }
    inline bool FocusGate(BaseApp& app)
    {
        const bool native = PreEditorCamera::AcquireNativeForeground();
        // Preserve Candidate04's short circuit. -1 explicitly means NOT EVALUATED.
        const int engine = native ? int(app.GetInputManager()->Focused()) : -1;
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorCamera::FocusTrace::Gate(native, engine);
#endif
        return native && engine == 1;
    }
    inline bool Pump(BaseApp& app)
    {
        FocusMark(116);
        app.GetInputManager()->PrepareForUpdate();
        app.GetEventManager()->PollEvents();
        app.GetInputManager()->Update();
        const bool focused = app.GetInputManager()->Focused();
        FocusMark(117, focused);
        return focused;
    }
    inline bool Routing(BaseApp& app)
    {
        auto& input = *app.GetInputManager();
        input.BeginUIRouting();
        return bool(input.PublishRouting({app.GetWindow()->GetWindowID(), 0, 0, 1280, 720,
                                          true, true, false, false}));
    }
    inline bool Button(BaseApp& app, unsigned button, bool down)
    {
        SDL_Event event{};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = app.GetWindow()->GetWindowID();
        event.button.button = static_cast<Uint8>(button);
        event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.button.x = 640; event.button.y = 360;
        return Push(event, 21) == 1;
    }
    inline bool Key(BaseApp& app, SDL_Scancode scan, bool down)
    {
        auto event = PreEditorInput::Native(down ? 0 : 1, app.GetWindow()->GetWindowID());
        event.key.keysym.scancode = scan;
        event.key.keysym.sym = SDL_GetKeyFromScancode(scan);
        return Push(event, 22) == 1;
    }
    inline bool Motion(BaseApp& app)
    {
        auto event = PreEditorInput::Native(2, app.GetWindow()->GetWindowID());
        event.motion.xrel = 8; event.motion.yrel = -6;
        return Push(event, 23) == 1;
    }
    inline bool Wheel(BaseApp& app)
    {
        auto event = PreEditorInput::Native(3, app.GetWindow()->GetWindowID());
        return Push(event, 24) == 1;
    }

    // Separate regression evidence: all original lifecycle check names/counts remain intact.
    struct GestureProbe
    {
        unsigned frames{}, step{}, tick{}, settle{};
        bool started{}, complete{}, expectActive{}, expectCapture{}, allowNavTransfer{};
        unsigned expectedReads{}, expectedActivations{};
        enum class Movement { None, Orbit, Pan, Dolly, Fly } movement{};
        std::uint64_t reads{}, activations{};
        glm::vec3 position{}, focal{};
        glm::quat orientation{};
        float distance{};
        double highlightStart = -1;
        const char* name = "setup";
        std::ofstream rows{"viewport-gestures.csv"};
        GestureProbe() { rows << "step,case,pass,reads,activations,active,capture,moved\n"; }
    };
    inline GestureProbe gestureProbe;
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
    namespace Case59Trace
    {
        struct Row
        {
            std::uint64_t ordinal{}, stamp{};
            const char* kind{};
            unsigned update{}, step{}, tick{};
            int result = -1;
            std::array<std::uint64_t,48> u{};
            std::array<double,32> v{};
            std::array<std::uint64_t,10> event{};
            std::array<double,6> value{};
        };
        struct NativeRow { std::uint64_t ordinal{}, stamp{}; DWORD thread{}; SDL_Event event{}; };
        inline std::array<Row,512> rows{};
        inline std::array<NativeRow,256> nativeRows{};
        inline std::atomic<unsigned> nativeCount{};
        inline std::atomic<std::uint64_t> ordinal{};
        inline std::atomic<bool> enabled{}, overflow{};
        inline unsigned count{};
        inline bool begun{}, attached{}, detached{};
        inline BaseApp* app{};
        inline Subscription observer;
        inline std::uint64_t Stamp() noexcept { return SDL_GetPerformanceCounter(); }
        inline int SDLCALL Native(void*, SDL_Event* event) noexcept
        {
            if (!enabled.load()) return 1;
            const auto index = nativeCount.fetch_add(1);
            if (index >= nativeRows.size()) { overflow = true; return 1; }
            nativeRows[index] = {++ordinal, Stamp(), GetCurrentThreadId(), *event};
            return 1; // SDL event watches do not filter or change events.
        }
        inline Row* Record(const char* kind, int result = -1) noexcept
        {
            if (!enabled.load()) return nullptr;
            if (count == rows.size()) { overflow = true; return nullptr; }
            auto& row = rows[count++];
            row.ordinal=++ordinal; row.stamp=Stamp(); row.kind=kind; row.result=result;
            row.update=probe.updates; row.step=gestureProbe.step; row.tick=gestureProbe.tick;
            auto& c=*ImGui::GetCurrentContext(); const auto& io=c.IO;
            auto& input=*app->GetInputManager(); const auto routing=input.Routing();
            auto* sdl=SDL_GetWindowFromID(app->GetWindow()->GetWindowID());
            SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
            const HWND own=SDL_GetWindowWMInfo(sdl,&info) ? info.info.win.window : nullptr;
            const HWND foreground=GetForegroundWindow(); DWORD foregroundPid{};
            const auto foregroundThread=GetWindowThreadProcessId(foreground,&foregroundPid);
            GUITHREADINFO gui{}; gui.cbSize=sizeof(gui);
            const bool guiValid=GetGUIThreadInfo(foregroundThread,&gui)!=0;
            POINT cursor{}; const bool cursorValid=GetCursorPos(&cursor)!=0;
            int mouseX{},mouseY{},windowX{},windowY{};
            const auto buttons=SDL_GetMouseState(&mouseX,&mouseY);
            SDL_GetWindowPosition(sdl,&windowX,&windowY);
            const auto widget=PreEditorInput::uiProbe.points[4];
            const ImVec2 center{float((routing.left+routing.right)*.5),float((routing.top+routing.bottom)*.5)};
            const auto hit=[&](ImVec2 point) {
                std::array<std::uint64_t,3> value{};
                POINT screen{LONG(point.x),LONG(point.y)};
                if (own && ClientToScreen(own,&screen))
                {
                    const HWND window=WindowFromPoint(screen), root=GetAncestor(window,GA_ROOT);
                    DWORD pid{}; GetWindowThreadProcessId(root,&pid);
                    value={reinterpret_cast<std::uintptr_t>(window),reinterpret_cast<std::uintptr_t>(root),pid};
                }
                return value;
            };
            const auto widgetHit=hit(widget), viewHit=hit(center);
            const auto handle=[](HWND value) { return std::uint64_t(reinterpret_cast<std::uintptr_t>(value)); };
            auto* keyboard=SDL_GetKeyboardFocus(); auto* mouse=SDL_GetMouseFocus();
            row.u={c.ActiveId,c.ActiveIdWindow?c.ActiveIdWindow->ID:0,c.NavWindow?c.NavWindow->ID:0,
                c.HoveredWindow?c.HoveredWindow->ID:0,c.InputTextState.ID,PreEditorCase59Widget::id,
                PreEditorInput::uiProbe.textActive,io.KeyAlt,io.MouseDown[0],io.MouseClicked[0],io.MouseReleased[0],
                io.WantCaptureKeyboard,io.WantCaptureMouse,io.WantTextInput,c.NavWindowingToggleLayer,
                std::uint64_t(c.InputEventsQueue.Size),input.GetPhysicalState().modifiers,input.GetViewState().modifiers,
                unsigned(input.PointerCapture()),input.Focused(),routing.visible,routing.focused,routing.uiMouse,routing.uiKeyboard,
                keyboard?SDL_GetWindowID(keyboard):0,mouse?SDL_GetWindowID(mouse):0,SDL_GetWindowFlags(sdl),gui.flags,
                handle(foreground),foregroundPid,foregroundThread,handle(own),handle(GetFocus()),handle(gui.hwndFocus),
                handle(gui.hwndCapture),cursorValid,widgetHit[0],widgetHit[1],widgetHit[2],viewHit[0],viewHit[1],viewHit[2],
                guiValid,attached,unsigned(PreEditorCase59Widget::generation),unsigned(c.FrameCount),GetCurrentThreadId(),buttons};
            const auto origin=ImGui::GetMainViewport()->Pos;
            const auto item=PreEditorCase59Widget::item, frame=PreEditorCase59Widget::frame;
            row.v={routing.left,routing.top,routing.right,routing.bottom,center.x,center.y,widget.x,widget.y,
                io.MousePos.x,io.MousePos.y,double(cursor.x),double(cursor.y),double(mouseX),double(mouseY),
                item.Min.x,item.Min.y,item.Max.x,item.Max.y,frame.Min.x,frame.Min.y,frame.Max.x,frame.Max.y,
                origin.x,origin.y,double(windowX),double(windowY),ImGui::GetTime(),io.MouseClickedPos[0].x,
                io.MouseClickedPos[0].y,double(PreEditorCase59Widget::active),double(PreEditorCase59Widget::changed),0};
            return &row;
        }
        inline void Finish(bool sample = true) noexcept
        {
            if (!enabled.load()) return;
            if (sample) Record("finish"); enabled=false;
            PreEditorCase59Widget::observed=nullptr;
            SDL_DelEventWatch(Native,nullptr);
            detached=!attached || bool(observer.Reset());
        }
        inline void Update(BaseApp& owner) noexcept
        {
            if (gestureProbe.step>59) { Finish(); return; }
            if (gestureProbe.step<56) return;
            if (!begun)
            {
                begun=true; app=&owner;
                auto subscription=owner.GetInputManager()->Observe([](const InputEvent& event) noexcept {
                    if (auto* row=Record("routed"))
                    {
                        row->event={event.sequence,unsigned(event.kind),event.window,unsigned(event.key),unsigned(event.button),
                            event.modifiers,unsigned(event.destination),unsigned(event.cancellation),event.handled,event.cleanup};
                        row->value={event.x,event.y,double(event.dx),double(event.dy),event.wheelX,event.wheelY};
                    }
                });
                attached=bool(subscription); if (subscription) observer=std::move(*subscription);
                enabled=true; SDL_AddEventWatch(Native,nullptr);
                PreEditorCase59Widget::observed=[]() noexcept { Record("widget",PreEditorCase59Widget::active); };
                Record("arm",attached);
            }
            Record("update");
        }
        template<class F> bool Operand(const char* name,F&& evaluate)
        { const bool value=evaluate(); Record(name,value); return value; }
        inline void Pushed(const SDL_Event& event,int result)
        {
            if (auto* row=Record("push",result))
            {
                row->event[0]=event.type;
                if (event.type==SDL_MOUSEMOTION)
                { row->event[1]=event.motion.windowID; row->value={double(event.motion.x),double(event.motion.y),double(event.motion.xrel),double(event.motion.yrel)}; }
                else if (event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP)
                { row->event[1]=event.button.windowID; row->event[2]=event.button.button; row->value={double(event.button.x),double(event.button.y)}; }
                else if (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP)
                { row->event[1]=event.key.windowID; row->event[2]=event.key.keysym.scancode; row->event[3]=event.key.keysym.mod; }
            }
        }
        struct Dump
        {
            ~Dump()
            {
                Finish(false);
                std::ofstream file("case59-state.csv"); file << std::setprecision(17);
                file << "ordinal,stamp,kind,update,step,tick,result";
                for(unsigned i=0;i<48;++i) file << ",u" << i;
                for(unsigned i=0;i<32;++i) file << ",v" << i;
                for(unsigned i=0;i<10;++i) file << ",event" << i;
                for(unsigned i=0;i<6;++i) file << ",value" << i;
                file << '\n';
                for(unsigned i=0;i<count;++i)
                {
                    const auto& r=rows[i]; file << r.ordinal << ',' << r.stamp << ',' << r.kind << ',' << r.update << ',' << r.step << ',' << r.tick << ',' << r.result;
                    for(auto v:r.u) file << ',' << v; for(auto v:r.v) file << ',' << v;
                    for(auto v:r.event) file << ',' << v; for(auto v:r.value) file << ',' << v;
                    file << '\n';
                }
                std::ofstream native("case59-native.csv"); native << "ordinal,stamp,thread,type,window,code,modifiers,x,y,dx,dy\n";
                for(unsigned i=0;i<(std::min)(nativeCount.load(),unsigned(nativeRows.size()));++i)
                {
                    const auto& r=nativeRows[i]; const auto& e=r.event;
                    unsigned window{},code{},mod{}; int x{},y{},dx{},dy{};
                    if(e.type==SDL_MOUSEMOTION) { window=e.motion.windowID;x=e.motion.x;y=e.motion.y;dx=e.motion.xrel;dy=e.motion.yrel; }
                    else if(e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP) { window=e.button.windowID;code=e.button.button;x=e.button.x;y=e.button.y; }
                    else if(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) { window=e.key.windowID;code=e.key.keysym.scancode;mod=e.key.keysym.mod; }
                    else if(e.type==SDL_WINDOWEVENT) { window=e.window.windowID;code=e.window.event;x=e.window.data1;y=e.window.data2; }
                    native << r.ordinal << ',' << r.stamp << ',' << r.thread << ',' << e.type << ',' << window << ',' << code << ',' << mod << ',' << x << ',' << y << ',' << dx << ',' << dy << '\n';
                }
                std::ofstream summary("case59-summary.json");
                summary << "{\"diagnostic_only\":true,\"armed\":" << begun << ",\"attached\":" << attached
                    << ",\"detached\":" << detached << ",\"overflow\":" << overflow.load() << ",\"state_count\":" << count
                    << ",\"native_count\":" << nativeCount.load() << ",\"frequency\":" << SDL_GetPerformanceFrequency()
                    << ",\"buffer_bytes\":" << sizeof(rows)+sizeof(nativeRows) << "}\n";
            }
        };
        inline Dump dump;
    }
#endif
    // Passive, bounded DrawList evidence from existing frames, after cost batches.
    // Flush only at shutdown; no event pumping, timing changes or per-frame I/O.
    struct FocusVisualProbe
    {
        struct Row
        {
            unsigned update{}, segment{}, step{};
            std::uint64_t activations{};
            bool active{}, drawable{}, pass{};
            double time{}, start{};
            float flash{}, border{};
            int vertices{}, indices{};
            unsigned alpha{}, failures{};
            ImVec4 image{}, clip{};
            ImVec2 scale{};
        };
        std::array<Row, 2048> rows{};
        std::size_t count{};
        std::uint64_t total{}, passed{}, failed{}, activeFrames{}, drawableFrames{}, opaqueFrames{};
        std::uint64_t flashes{}, persistent{}, afterOneSecond{}, afterFiveSeconds{}, inactiveFrames{};
        std::uint64_t firstActivation{}, firstFailureOrdinal{}, digest{14695981039346656037ull};
        unsigned gestureMask{}, failureBits{};
        bool multipleActivations{};
        Row firstFailure{};
        bool overflow{};
        template<class T> void Mix(const T& value)
        {
            const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
            for (std::size_t i=0; i<sizeof(value); ++i) { digest^=bytes[i]; digest*=1099511628211ull; }
        }
        void Record(const Row& r)
        {
            ++total;
            passed+=r.pass; failed+=!r.pass; failureBits|=r.failures;
            if (!r.pass && !firstFailureOrdinal) { firstFailureOrdinal=total; firstFailure=r; }
            drawableFrames+=r.drawable;
            opaqueFrames+=r.drawable && r.border==1.f && r.alpha==255 && r.failures==0;
            inactiveFrames+=!r.active && r.drawable && r.flash==0;
            if (r.active && r.drawable)
            {
                if (!activeFrames) firstActivation=r.activations;
                multipleActivations|=r.activations!=firstActivation;
                ++activeFrames;
                flashes+=r.flash>0;
                persistent+=r.time-r.start>=.25 && r.border>0 && r.flash==0;
                afterOneSecond+=r.time-r.start>=1 && r.flash==0;
                afterFiveSeconds+=r.time-r.start>=5 && r.flash==0;
                if (r.step>=4 && r.step<24) gestureMask|=1u<<(r.step-4);
            }
            // Hash explicit fields only: struct padding never enters the evidence.
            Mix(total); Mix(r.update); Mix(r.segment); Mix(r.step); Mix(r.activations);
            Mix(r.active); Mix(r.drawable); Mix(r.pass); Mix(r.time); Mix(r.start);
            Mix(r.flash); Mix(r.border); Mix(r.vertices); Mix(r.indices); Mix(r.alpha); Mix(r.failures);
            Mix(r.image.x); Mix(r.image.y); Mix(r.image.z); Mix(r.image.w);
            Mix(r.clip.x); Mix(r.clip.y); Mix(r.clip.z); Mix(r.clip.w); Mix(r.scale.x); Mix(r.scale.y);
            // Retain the first 2047 rows and the current row. Every frame still
            // executes all assertions; previous-row and lifetime consumers see
            // the immediately preceding publication, including after compaction.
            if (count<rows.size()) rows[count++]=r;
            else rows.back()=r;
        }
        void WriteRow(std::ostream& file, const Row& r, std::uint64_t ordinal) const
        {
            file << r.update << ',' << r.segment << ',' << r.step << ',' << r.activations << ','
                 << r.active << ',' << r.drawable << ',' << r.pass << ',' << r.time << ',' << r.start << ','
                 << r.flash << ',' << r.border << ',' << r.vertices << ',' << r.indices << ',' << r.alpha << ','
                 << r.image.x << ',' << r.image.y << ',' << r.image.z << ',' << r.image.w << ','
                 << r.clip.x << ',' << r.clip.y << ',' << r.clip.z << ',' << r.clip.w << ','
                 << r.scale.x << ',' << r.scale.y << ',' << overflow << ',' << r.failures << ',' << ordinal << '\n';
        }
        ~FocusVisualProbe()
        {
            std::ofstream file("viewport-focus-visual.csv");
            const char* header="update,segment,step,activations,active,drawable,pass,time,start,flash,border,vertices,indices,alpha,left,top,right,bottom,clip_left,clip_top,clip_right,clip_bottom,scale_x,scale_y,overflow,failures,ordinal\n";
            file << header
                 << std::setprecision(17);
            for (std::size_t i=0; i<count; ++i)
                WriteRow(file,rows[i],i+1==count ? total : i+1);
            std::ofstream summary("viewport-focus-coverage.json");
            summary << "{\"schema\":1,\"capacity\":" << rows.size() << ",\"stored\":" << count
                << ",\"total\":" << total << ",\"compacted\":" << total-count
                << ",\"passed\":" << passed << ",\"failed\":" << failed << ",\"failure_bits\":" << failureBits
                << ",\"first_failure_ordinal\":" << firstFailureOrdinal << ",\"overflow\":" << overflow
                << ",\"active_frames\":" << activeFrames << ",\"drawable_frames\":" << drawableFrames
                << ",\"opaque_frames\":" << opaqueFrames << ",\"flashes\":" << flashes
                << ",\"persistent\":" << persistent << ",\"after_one_second\":" << afterOneSecond
                << ",\"after_five_seconds\":" << afterFiveSeconds << ",\"inactive_frames\":" << inactiveFrames
                << ",\"gesture_mask\":" << gestureMask << ",\"multiple_activations\":" << multipleActivations
                << ",\"field_digest_fnv1a64\":\"" << digest << "\"}\n";
            if (firstFailureOrdinal)
            {
                std::ofstream failure("viewport-focus-first-failure.csv");
                failure << header << std::setprecision(17);
                WriteRow(failure,firstFailure,firstFailureOrdinal);
            }
        }
    };
    inline FocusVisualProbe focusVisualProbe;
    bool ObserveViewportFocus(bool active, bool drawable, std::uint64_t activations, double start,
                              float flash, float border, ImDrawList* draw, int firstVertex, int firstIndex,
                              const ImVec4& image, const ImVec4& clip)
    {
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
        Case59Trace::Record("ui_published");
#endif
        if (!probe.initialized) return true;
        auto& p=focusVisualProbe;
        if (p.total==(std::numeric_limits<std::uint64_t>::max)()) { p.overflow=true; return false; }
        const int vertices=draw ? draw->VtxBuffer.Size-firstVertex : 0;
        const int indices=draw ? draw->IdxBuffer.Size-firstIndex : 0;
        const double now=ImGui::GetTime(), age=now-start;
        // The owner now requires opaque, exact state colors and 1/2/4 physical
        // pixels at 1x. Keep strict raw bounds/command clipping checks; also
        // independently check every strip's color, width and index coverage.
        unsigned failures=0;
        const auto require=[&](bool value, unsigned bit) { if (!value) failures|=bit; };
        require(std::isfinite(flash) && std::isfinite(border), 1u);
        unsigned alpha=0;
        if (drawable)
        {
            require(border==1.f && flash>=0.f && flash<=1.f, 2u);
            require(active ? start>=0 && age>=0 && (age<.25 ? flash>0 : flash==0) : flash==0, 4u);
            require(clip.x>=image.x && clip.y>=image.y && clip.z<=image.z && clip.w<=image.w, 8u);
            const unsigned flashAlpha=unsigned(std::round(255.f*flash));
            const int rings=flashAlpha ? 2 : 1;
            require(vertices==16*rings && indices==24*rings, 16u);
            for (int i=firstVertex; draw && i<draw->VtxBuffer.Size; ++i)
            {
                const auto& v=draw->VtxBuffer[i];
                require(v.pos.x>=image.x && v.pos.x<=image.z && v.pos.y>=image.y && v.pos.y<=image.w, 32u);
                alpha=(std::max)(alpha, (v.col>>IM_COL32_A_SHIFT)&255u);
            }
            require(alpha==255, 64u);
            const auto* window=ImGui::FindWindowByName("Viewport");
            const auto scale=ImGui::GetIO().DisplayFramebufferScale;
            require(window && window->Viewport && scale.x>0 && scale.y>0, 128u);
            if (draw && window && window->Viewport && scale.x>0 && scale.y>0 && vertices==16*rings)
            {
                const auto origin=window->Viewport->Pos;
                const float left=origin.x+std::ceil((clip.x-origin.x)*scale.x)/scale.x;
                const float top=origin.y+std::ceil((clip.y-origin.y)*scale.y)/scale.y;
                const float right=origin.x+std::floor((clip.z-origin.x)*scale.x)/scale.x;
                const float bottom=origin.y+std::floor((clip.w-origin.y)*scale.y)/scale.y;
                for (int ring=0; ring<rings; ++ring)
                {
                    const float physical=(std::max)(1.f,std::round((ring ? 4.f : active ? 2.f : 1.f)*window->Viewport->DpiScale));
                    const float x=(std::min)(physical/scale.x,(right-left)*.5f);
                    const float y=(std::min)(physical/scale.y,(bottom-top)*.5f);
                    const ImU32 color=ring ? IM_COL32(125,211,252,flashAlpha) :
                        active ? IM_COL32(56,189,248,255) : IM_COL32(71,85,105,255);
                    const ImVec4 strips[]={{left,top,right,top+y},{left,bottom-y,right,bottom},
                        {left,top+y,left+x,bottom-y},{right-x,top+y,right,bottom-y}};
                    for (int strip=0; strip<4; ++strip)
                    {
                        const auto& rect=strips[strip];
                        const ImVec2 positions[]={{rect.x,rect.y},{rect.z,rect.y},{rect.z,rect.w},{rect.x,rect.w}};
                        for (int vertex=0; vertex<4; ++vertex)
                        {
                            const auto& v=draw->VtxBuffer[firstVertex+ring*16+strip*4+vertex];
                            require(v.col==color, 256u);
                            require(std::abs(v.pos.x-positions[vertex].x)<1e-4f &&
                                    std::abs(v.pos.y-positions[vertex].y)<1e-4f, 512u);
                        }
                    }
                }
            }
            unsigned covered=0;
            if (draw) for (const auto& command:draw->CmdBuffer)
                if (command.IdxOffset<unsigned(firstIndex+indices) && command.IdxOffset+command.ElemCount>unsigned(firstIndex))
                {
                    require(command.ClipRect.x==clip.x && command.ClipRect.y==clip.y &&
                        command.ClipRect.z==clip.z && command.ClipRect.w==clip.w, 1024u);
                    require(command.TextureId==ImGui::GetIO().Fonts->TexID && !command.UserCallback, 2048u);
                    covered+=(std::min)(command.IdxOffset+command.ElemCount,unsigned(firstIndex+indices))-
                             (std::max)(command.IdxOffset,unsigned(firstIndex));
                }
            require(covered==unsigned(indices), 4096u);
        }
        else require(vertices==0 && indices==0 && border==0 && flash==0, 8192u);
        if (p.count)
        {
            const auto& previous=p.rows[p.count-1];
            if (active && previous.active && activations==previous.activations)
                require(start==previous.start, 16384u); // Gesture/release/hover must not retrigger.
        }
        const bool passed=failures==0;
        p.Record({probe.updates,probe.segment,gestureProbe.step,activations,active,drawable,passed,
            now,start,flash,border,vertices,indices,alpha,failures,image,clip,ImGui::GetIO().DisplayFramebufferScale});
        return passed;
    }
    inline bool GestureButton(BaseApp& app, unsigned button, bool down, glm::vec2 point)
    {
        SDL_Event e{}; e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.windowID = app.GetWindow()->GetWindowID(); e.button.button = Uint8(button);
        e.button.state = down ? SDL_PRESSED : SDL_RELEASED; e.button.clicks = 1;
        e.button.x = int(point.x); e.button.y = int(point.y);
        const int result=Push(e,31);
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
        Case59Trace::Pushed(e,result);
#endif
        return result==1;
    }
    inline bool GestureMotion(BaseApp& app, glm::vec2 point, int dx = 0, int dy = 0)
    {
        SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.windowID = app.GetWindow()->GetWindowID();
        e.motion.x = int(point.x); e.motion.y = int(point.y); e.motion.xrel = dx; e.motion.yrel = dy;
        const int result=Push(e,32);
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
        Case59Trace::Pushed(e,result);
#endif
        return result==1;
    }
    inline bool GestureKey(BaseApp& app, SDL_Scancode scan, bool down, SDL_Keymod modifiers)
    {
        SDL_Event e{}; e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.windowID = app.GetWindow()->GetWindowID(); e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.scancode = scan; e.key.keysym.sym = SDL_GetKeyFromScancode(scan);
        e.key.keysym.mod = modifiers; // Match real SDL event-time modifiers for the ImGui adapter too.
        const int result=Push(e,33);
#ifdef GENGINE_LIFECYCLE_CASE59_TRACE
        Case59Trace::Pushed(e,result);
#endif
        return result==1;
    }
    inline bool GestureFocus(BaseApp& app, bool gain)
    {
        SDL_Event e{}; e.type = SDL_WINDOWEVENT; e.window.windowID = app.GetWindow()->GetWindowID();
        e.window.event = gain ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
        return Push(e, 34) == 1;
    }
#ifdef GENGINE_LIFECYCLE_CASE59_FIXTURE
    // UI-owned Alt release legitimately retires InputText through ImGui menu
    // navigation. Establish the next test's text precondition with one real
    // fixture click; never change UI focus, drain a queue or repeat that click.
    struct Case59Premise
    {
        struct Row { unsigned update{}, kind{}, active{}, text{}, alt{}, down{}, queued{}; int result{}; };
        std::array<Row,16> rows{};
        unsigned count{}, frames{}, clicks{};
        bool preparing{}, overflow{};
        bool Record(unsigned kind,int result)
        {
            if(count==rows.size()) { overflow=true; return false; }
            const auto& c=*ImGui::GetCurrentContext();
            rows[count++]={probe.updates,kind,c.ActiveId,PreEditorInput::uiProbe.textActive,
                unsigned(c.IO.KeyAlt),unsigned(c.IO.MouseDown[0]),unsigned(c.InputEventsQueue.Size),result};
            return true;
        }
        ~Case59Premise()
        {
            std::ofstream file("case59-premise.csv");
            file << "update,kind,active_id,text_active,alt,down,queued,result,setup_clicks,overflow\n";
            for(unsigned i=0;i<count;++i) { const auto& r=rows[i];
                file << r.update << ',' << r.kind << ',' << r.active << ',' << r.text << ',' << r.alt << ','
                     << r.down << ',' << r.queued << ',' << r.result << ',' << clicks << ',' << overflow << '\n'; }
        }
    };
    inline Case59Premise case59Premise;
    inline bool BeginCase59(BaseApp& app,glm::vec2 textPoint)
    {
        auto& p=case59Premise; const auto& input=*app.GetInputManager();
        const bool eligible=!p.clicks && PreEditorInput::uiProbe.seen[4] && input.Focused() &&
            !input.Routing().focused && input.PointerCapture()==InputLayer::None &&
            input.GetPhysicalState().modifiers==0 &&
            !input.GetPhysicalState().m_Mouse.isButtonHeld(GENGINE_BUTTON_LEFT) &&
            PreEditorCamera::NativePointVisible(glm::dvec2(textPoint));
        bool result=false;
        if(eligible)
        {
            ++p.clicks;
            result=GestureMotion(app,textPoint) && GestureButton(app,SDL_BUTTON_LEFT,true,textPoint) &&
                GestureButton(app,SDL_BUTTON_LEFT,false,textPoint);
        }
        p.preparing=result;
        return p.Record(0,result) && result;
    }
    inline int ReadyCase59(BaseApp& app)
    {
        auto& p=case59Premise; const auto& c=*ImGui::GetCurrentContext(); const auto& input=*app.GetInputManager();
        const auto* window=ImGui::FindWindowByName("Input regression fixture");
        const bool active=PreEditorInput::uiProbe.textActive && c.ActiveId && c.InputTextState.ID==c.ActiveId &&
            c.ActiveIdWindow==window;
        const bool ready=active && !c.IO.KeyAlt && !c.IO.MouseDown[0] && !c.InputEventsQueue.Size &&
            input.GetPhysicalState().modifiers==0 && !input.GetPhysicalState().m_Mouse.isButtonHeld(GENGINE_BUTTON_LEFT) &&
            input.PointerCapture()==InputLayer::None && !input.Routing().focused &&
            input.Routing().uiMouse && input.Routing().uiKeyboard;
        const int result=++p.frames>8 ? -1 : ready ? 1 : 0;
        return p.Record(1,result) ? result : -1;
    }
    inline bool ClickCase59(BaseApp& app,glm::vec2 point)
    {
        // Exact original text-active && motion && DOWN && UP short circuit.
        const bool result=PreEditorInput::uiProbe.textActive && GestureMotion(app,point) &&
            GestureButton(app,SDL_BUTTON_LEFT,true,point) && GestureButton(app,SDL_BUTTON_LEFT,false,point);
        case59Premise.preparing=false;
        return case59Premise.Record(2,result) && result;
    }
    struct FocusLifetimePremise
    {
        bool begun{}, complete{};
        std::uint64_t stamp{}, activations{}, reads{};
        double start{}, wall{}, age{};
        unsigned frames{};
        ~FocusLifetimePremise()
        {
            std::ofstream file("focus-lifetime.json"); file << std::setprecision(17);
            file << "{\"begun\":" << begun << ",\"complete\":" << complete << ",\"frames\":" << frames
                 << ",\"wall_seconds\":" << wall << ",\"activation_age\":" << age
                 << ",\"activations\":" << activations << ",\"reads\":" << reads << "}\n";
        }
    };
    inline FocusLifetimePremise focusLifetime;
    inline int ObserveFocusLifetime(BaseApp& app,bool active,std::uint64_t activations,double start,std::uint64_t reads)
    {
        auto& p=focusLifetime;
        if(p.complete) return 1;
        if(!p.begun) { p.begun=true;p.stamp=SDL_GetPerformanceCounter();p.activations=activations;p.start=start;p.reads=reads; }
        ++p.frames;
        const auto& visual=focusVisualProbe;
        if(!FocusGate(app) || !active || !app.GetInputManager()->Routing().focused ||
            app.GetInputManager()->PointerCapture()!=InputLayer::None ||
            p.activations!=activations || p.start!=start || p.reads!=reads || !visual.count || visual.overflow) return -1;
        const auto& row=visual.rows[visual.count-1];
        if(!row.pass || !row.active || !row.drawable || row.start!=start || row.activations!=activations ||
            row.alpha!=255 || row.border!=1) return -1;
        p.wall=double(SDL_GetPerformanceCounter()-p.stamp)/double(SDL_GetPerformanceFrequency());
        p.age=row.time-row.start;
        p.complete=p.wall>=5.0 && p.age>=5.0;
        return p.complete ? 1 : 0;
    }
#endif
    // Candidate17: evidence-only state for the three existing Case55 ticks.
    // No camera mutation, extra frame, event pumping, or alternate projection.
    struct Case55Premise
    {
        unsigned ticks{}, injectionUpdate{};
        std::optional<::GEngine::CameraSnapshotDesc> injected;
        glm::vec2 point{};
        bool visible{};
        std::ofstream rows{"case55-premise.csv"};
        Case55Premise()
        {
            rows << "tick,update,pose,viewport,fixed,ordering,native_visible,injected,pass,x,y,z,qw,qx,qy,qz,left,top,logical_width,logical_height,pixel_width,pixel_height,click_x,click_y\n"
                 << std::setprecision(17);
        }
    };
    inline Case55Premise case55Premise;
    inline bool Case55Tick(BaseApp& app, unsigned tick, const ::GEngine::CameraSnapshot& snapshot,
                           const Camera::_EditorCamera& camera, const ::GEngine::CameraViewport& expected)
    {
        auto& evidence=case55Premise; const auto& d=snapshot.Description(); const auto& v=d.viewport;
        const glm::dquat q(camera.GetOrientation());
        const double scale=(std::max)({std::abs(q.w),std::abs(q.x),std::abs(q.y),std::abs(q.z)});
        const auto normalized=glm::normalize(q/scale); // Same normalization as CameraSnapshot::Create.
        const auto sameViewport=[](const auto& a,const auto& b) {
            return a.logicalOrigin==b.logicalOrigin && a.logicalSize==b.logicalSize &&
                a.pixelOrigin==b.pixelOrigin && a.pixelSize==b.pixelSize;
        };
        const bool pose=camera.m_YawDelta==0 && camera.m_PitchDelta==0 &&
            camera.m_PositionDelta==glm::vec3{} && d.orientation==normalized &&
            PreEditorCamera::Max(d.position-glm::dvec3(camera.GetPosition()))<=PreEditorCamera::WorldLimit(d.position);
        const bool viewport=sameViewport(v,expected);
        const bool fixed=!evidence.injected || (d.orientation==evidence.injected->orientation &&
            PreEditorCamera::Max(d.position-evidence.injected->position)<=PreEditorCamera::WorldLimit(d.position) &&
            sameViewport(v,evidence.injected->viewport) && d.projection==evidence.injected->projection &&
            d.verticalFovRadians==evidence.injected->verticalFovRadians &&
            d.orthographicHeight==evidence.injected->orthographicHeight &&
            d.nearPlane==evidence.injected->nearPlane && d.farPlane==evidence.injected->farPlane);
        const bool ordering=tick==evidence.ticks+1 && tick<=3 &&
            (tick==1 || probe.updates==evidence.injectionUpdate+tick-1);
        bool injected=false, passed=pose && viewport && fixed && ordering;
        if (passed && tick==1)
        {
            const auto pixel=snapshot.WorldToPixel(PreEditorCamera::Points[0]);
            passed=bool(pixel);
            if (passed)
            {
                evidence.point=glm::vec2(v.logicalOrigin+pixel->value/glm::dvec2(v.pixelSize)*v.logicalSize);
                evidence.visible=PreEditorCamera::NativePointVisible(glm::dvec2(evidence.point));
                passed=evidence.visible;
            }
            if (passed)
            {
                // Original Case55 tuple, unchanged order; injected one existing tick later.
                passed=GestureKey(app,SDL_SCANCODE_LALT,false,KMOD_NONE) &&
                    GestureMotion(app,evidence.point) && GestureButton(app,SDL_BUTTON_LEFT,true,evidence.point) &&
                    GestureButton(app,SDL_BUTTON_LEFT,false,evidence.point);
                injected=passed;
                if (passed) { evidence.injected=d; evidence.injectionUpdate=probe.updates; }
            }
        }
        ++evidence.ticks;
        evidence.rows << tick << ',' << probe.updates << ',' << pose << ',' << viewport << ',' << fixed << ','
            << ordering << ',' << evidence.visible << ',' << injected << ',' << passed << ','
            << d.position.x << ',' << d.position.y << ',' << d.position.z << ','
            << d.orientation.w << ',' << d.orientation.x << ',' << d.orientation.y << ',' << d.orientation.z << ','
            << v.logicalOrigin.x << ',' << v.logicalOrigin.y << ',' << v.logicalSize.x << ',' << v.logicalSize.y << ','
            << v.pixelSize.x << ',' << v.pixelSize.y << ',' << evidence.point.x << ',' << evidence.point.y << '\n';
        if (!passed || tick==3) evidence.rows.flush();
        return passed && bool(evidence.rows);
    }

    inline PlatformResult NativePollChecks(BaseApp& app)
    {
        auto owned = InputManager::GetScopedInstance(); auto& input = *owned;
        const auto require = [](bool ok, const char* name) -> PlatformResult {
            return ok ? PlatformResult{} : PreEditorInput::Failure(name); };
#define POLL_REQUIRE(x, name) if (auto r = require(bool(x), name); !r) return r
        POLL_REQUIRE(input.Initialize(), "poll fixture initialize"); input.SetWindow(app.GetWindow());
        POLL_REQUIRE(input.PublishRouting({app.GetWindow()->GetWindowID(),0,0,100,100,true,true,false,false}), "poll routing");
        unsigned motions = 0, cancels = 0;
        auto view = input.Subscribe(InputLayer::View, [&](InputDelivery& d) {
            if (d.event.kind == InputKind::Cancel) ++cancels;
            if (!d.event.cleanup) d.Handle();
            if (d.event.kind == InputKind::Motion) ++motions;
        });
        POLL_REQUIRE(view, "poll subscriber");
        const auto route = [&](InputKind kind, float x=20, float y=20) {
            InputEvent e; e.kind=kind; e.window=app.GetWindow()->GetWindowID();
            e.button=GENGINE_BUTTON_LEFT; e.x=x; e.y=y; e.dx=8; e.dy=-6;
            return input.Route(e);
        };
        POLL_REQUIRE(!input.EndNativePoll(), "unbalanced poll rejected");
        POLL_REQUIRE(input.BeginNativePoll() && input.BeginNativePoll(), "nested poll begin");
        POLL_REQUIRE(route(InputKind::ButtonDown) && input.CapturePointer(InputLayer::View,true), "logical capture in poll");
        POLL_REQUIRE(input.PointerCapture()==InputLayer::View && SDL_GetRelativeMouseMode()==SDL_FALSE, "native entry deferred only");
        POLL_REQUIRE(route(InputKind::Motion,200,200)==InputLayer::View && motions==1, "same-poll owned boundary motion");
        POLL_REQUIRE(input.EndNativePoll() && SDL_GetRelativeMouseMode()==SDL_FALSE, "nested exit no commit");
        POLL_REQUIRE(input.EndNativePoll() && SDL_GetRelativeMouseMode()==SDL_TRUE, "outer exit commits");
        POLL_REQUIRE(input.BeginNativePoll() && route(InputKind::ButtonUp) && input.ReleasePointer(InputLayer::View), "logical release immediate");
        POLL_REQUIRE(input.PointerCapture()==InputLayer::None && SDL_GetRelativeMouseMode()==SDL_TRUE, "native exit deferred only");
        POLL_REQUIRE(route(InputKind::Motion,200,200)==InputLayer::UI && motions==1, "release retires boundary ownership immediately");
        POLL_REQUIRE(route(InputKind::ButtonDown,30,35) && input.CapturePointer(InputLayer::View,true) &&
            route(InputKind::Motion,210,220)==InputLayer::View, "fresh gesture follows relative release");
        POLL_REQUIRE(input.EndNativePoll() && input.GetViewState().m_Mouse.GetPosition().x==210 &&
            SDL_GetRelativeMouseMode()==SDL_TRUE && motions==2, "older native release preserves later event state");
        POLL_REQUIRE(input.BeginNativePoll() && route(InputKind::FocusLost), "focus loss in poll");
        POLL_REQUIRE(!input.Focused() && input.PointerCapture()==InputLayer::None &&
            !input.GetViewState().m_Mouse.isButtonHeld(GENGINE_BUTTON_LEFT), "logical focus cancellation immediate");
        POINT newOrigin{}; POLL_REQUIRE(GetPhysicalCursorPos(&newOrigin), "native fresh origin");
        POLL_REQUIRE(route(InputKind::FocusGained) && route(InputKind::ButtonDown) &&
            input.CapturePointer(InputLayer::View,true) && route(InputKind::Motion,220,230), "regain fresh same-poll gesture");
        POLL_REQUIRE(input.EndNativePoll() && input.Focused() && input.PointerCapture()==InputLayer::View &&
            SDL_GetRelativeMouseMode()==SDL_TRUE && cancels!=0, "focus transaction commit");
        POLL_REQUIRE(route(InputKind::ButtonUp) && input.ReleasePointer(InputLayer::View) &&
            SDL_GetRelativeMouseMode()==SDL_FALSE, "direct release remains immediate");
        POINT restored{}; POLL_REQUIRE(GetPhysicalCursorPos(&restored) && restored.x==newOrigin.x &&
            restored.y==newOrigin.y, "normal release restores exact physical origin");
        const auto dpi = GetThreadDpiAwarenessContext();
        RECT clip{}; POLL_REQUIRE(GetClipCursor(&clip), "cursor clip baseline");
        POLL_REQUIRE(route(InputKind::ButtonDown) && input.CapturePointer(InputLayer::View,true), "cancellation cursor setup");
        POINT beforeCancel{}, afterCancel{};
        POLL_REQUIRE(GetPhysicalCursorPos(&beforeCancel) && input.BeginNativePoll() &&
            route(InputKind::FocusLost) && input.EndNativePoll() && GetPhysicalCursorPos(&afterCancel), "native cancellation exit");
        int nativeX=0, nativeY=0; SDL_GetMouseState(&nativeX,&nativeY);
        POLL_REQUIRE(input.GetViewState().m_Mouse.GetPosition().x==nativeX &&
            input.GetViewState().m_Mouse.GetPosition().y==nativeY &&
            input.GetPhysicalState().m_Mouse.GetPosition().x==nativeX &&
            input.GetPhysicalState().m_Mouse.GetPosition().y==nativeY &&
            input.GetViewState().m_Mouse.GetDX()==0 && input.GetViewState().m_Mouse.GetDY()==0,
            "cancel cleanup versions do not mask native exit position");
        RECT afterClip{};
        POLL_REQUIRE(beforeCancel.x==afterCancel.x && beforeCancel.y==afterCancel.y &&
            GetThreadDpiAwarenessContext()==dpi && GetClipCursor(&afterClip) &&
            clip.left==afterClip.left && clip.right==afterClip.right &&
            clip.top==afterClip.top && clip.bottom==afterClip.bottom,
            "cancellation zero physical displacement restores DPI and clip");
        POLL_REQUIRE(route(InputKind::FocusGained), "post-cancellation regain");
        POLL_REQUIRE(input.BeginNativePoll(), "bounded failure poll begin");
        bool rejected=false;
        for (unsigned i=0;i!=257;++i)
            if (!input.SetRelativeMouseMode(false)) { rejected=true; break; }
        POLL_REQUIRE(rejected && input.NativePollFailure() && !input.EndNativePoll() &&
            input.PointerCapture()==InputLayer::None && SDL_GetRelativeMouseMode()==SDL_FALSE &&
            !input.BeginNativePoll(), "overflow cancels and fails closed without native capture");
        POLL_REQUIRE(view->Reset(), "poll observer cleanup"); owned.reset(); SDL_StartTextInput();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_NATIVE_POLL_PASS direct=true nested=true ordering=true failure=true");
#undef POLL_REQUIRE
        return {};
    }

    inline bool MatrixAndCoordinates(const CameraSnapshot& s, CameraSnapshotDesc expected)
    {
        using PreEditorCamera::Max;
        using PreEditorCamera::Rotate;
        glm::dmat4 view(1), projection(0);
        const auto right = Rotate(expected.orientation, {1,0,0});
        const auto up = Rotate(expected.orientation, {0,1,0});
        const auto back = Rotate(expected.orientation, {0,0,1});
        for (int i = 0; i != 3; ++i)
        { view[i][0] = right[i]; view[i][1] = up[i]; view[i][2] = back[i]; }
        view[3] = {-glm::dot(right,expected.position), -glm::dot(up,expected.position), -glm::dot(back,expected.position), 1};
        const double aspect = double(expected.viewport.pixelSize.x) / expected.viewport.pixelSize.y;
        const double n = expected.nearPlane, f = expected.farPlane;
        if (expected.projection == CameraProjection::Orthographic)
        {
            projection[0][0] = 2 / (expected.orthographicHeight * aspect);
            projection[1][1] = 2 / expected.orthographicHeight;
            projection[2][2] = -2 / (f-n); projection[3][2] = -(f+n)/(f-n); projection[3][3] = 1;
        }
        else
        {
            projection[0][0] = 1 / (aspect * std::tan(expected.verticalFovRadians/2));
            projection[1][1] = 1 / std::tan(expected.verticalFovRadians/2);
            projection[2][2] = -(f+n)/(f-n); projection[3][2] = -2*f*n/(f-n); projection[2][3] = -1;
        }
        const auto inverse = s.ViewProjection() * s.InverseViewProjection();
        const auto inverseView = glm::dmat4(s.View()) * s.InverseView();
        const auto inverseProjection = glm::dmat4(s.Projection()) * s.InverseProjection();
        for (int c = 0; c != 4; ++c) for (int r = 0; r != 4; ++r)
            if (std::abs(s.View()[c][r]-view[c][r]) > 1e-5+2e-5*std::abs(view[c][r]) ||
                std::abs(s.Projection()[c][r]-projection[c][r]) > 1e-5+2e-5*std::abs(projection[c][r]) ||
                std::abs(inverse[c][r]-(c==r?1:0)) > 2e-4 ||
                std::abs(inverseView[c][r]-(c==r?1:0)) > 2e-4 ||
                std::abs(inverseProjection[c][r]-(c==r?1:0)) > 2e-4) return false;
        for (const auto xy : {glm::dvec2(0,0), glm::dvec2(-.8,-.8), glm::dvec2(.8,.8)})
            for (const double depth : {n,(n+f)/2,f})
            {
                // Pose/matrix accuracy was checked above against the independent
                // world oracle. Conversion takes the sampled pose as its input;
                // do not amplify permitted Transform rounding by 1/near depth.
                const auto world = PreEditorCamera::World(s.Description(), xy.x, xy.y, depth);
                const double z = expected.projection == CameraProjection::Orthographic
                    ? 2*(depth-n)/(f-n)-1 : (f+n)/(f-n)-2*f*n/((f-n)*depth);
                auto ndc = s.WorldToNdc(world);
                auto backWorld = s.NdcToWorld({xy,z});
                if (!ndc || !backWorld || Max(ndc->value-glm::dvec3(xy,z)) > 2e-5 ||
                    Max(*backWorld-world) > PreEditorCamera::WorldLimit(world)) return false;
            }
        return true;
    }

    // Uses an isolated Scene, the live resource domain and the same sampled-world
    // provider. All operations are owner-thread CPU work; no second Physics clock.
    inline PlatformResult Cpu(SceneRenderResources& owner)
    {
        auto access = owner.Publication().BeginFrame();
        auto resources = owner.ForFrame(access);
        auto scene = CreateRefPtr<_Scene>();
        auto view = scene->CreateEditorViewIdentity();
        if (!Check(view && scene->ValidateEditorViewIdentity(*view), "transient_live")) return Failure("transient");
        auto rawView = scene->RenderData().Resolve(*view);
        if (!Check(rawView && !scene->ReadRegistry().any_of<IDComponent, Transform3DComponent,
                RenderCameraComponent, MeshRendererComponent, RigidBody3DComponent>(*rawView),
                "transient_no_authored_state")) return Failure("transient authoring");
        auto retirementView = scene->CreateEditorViewIdentity();
        if (!retirementView || !scene->RenderData().Add<RenderCameraComponent>(*retirementView))
            return Failure("transient retirement fixture");
        auto invalidRetirementView = scene->ValidateEditorViewIdentity(*retirementView);
        if (!Check(!invalidRetirementView && invalidRetirementView.error().code == SceneCameraErrorCode::NotEditorView &&
                   scene->RetireEditorViewIdentity(*retirementView) && !scene->RenderData().Resolve(*retirementView),
                   "transient_camera_value_invalid_but_retirable")) return Failure("transient camera value retirement");
        // Generic compatibility writes must never promote the transient lifetime
        // or make authored Scene copy dereference its absent authored identity.
        if (!scene->RenderData().Add<RenderCameraComponent>(*view)) return Failure("transient camera contamination");
        auto transientCopy = _Scene::Copy(scene);
        if (!Check(transientCopy && (*transientCopy)->ReadRegistry().alive() == 0 &&
                   scene->RenderData().Resolve(*view),
                   "transient_render_component_copy_excluded")) return Failure("transient component copy");
        auto& compatibility = scene->Reg();
        compatibility.emplace<IDComponent>(*rawView, UUID{});
        compatibility.emplace<TagComponent>(*rawView, "Transient compatibility state");
        compatibility.emplace<Transform3DComponent>(*rawView);
        auto promoted = scene->SetActiveSceneCamera(*view, resources);
        auto transientActive = scene->ActiveSceneCamera();
        if (!Check(!promoted && promoted.error().code == SceneCameraErrorCode::MissingIdentity &&
                   transientActive && !*transientActive,
                   "transient_authored_components_do_not_promote_identity")) return Failure("transient promotion");
        transientCopy = _Scene::Copy(scene);
        if (!Check(transientCopy && (*transientCopy)->ReadRegistry().alive() == 0 &&
                   scene->RenderData().Resolve(*view),
                   "transient_authored_components_copy_excluded")) return Failure("transient authored copy");
        compatibility.remove<Transform3DComponent>(*rawView);
        compatibility.remove<TagComponent>(*rawView);
        compatibility.remove<IDComponent>(*rawView);
        if (!scene->RenderData().Remove<RenderCameraComponent>(*view)) return Failure("transient contamination cleanup");
        if (!Check(bool(scene->RetireEditorViewIdentity(*view)), "transient_retirement")) return Failure("retirement");
        const auto retired = *view;
        view = scene->CreateEditorViewIdentity();
        if (!Check(view && view->index == retired.index && *view != retired &&
                !scene->ValidateEditorViewIdentity(retired) && scene->ValidateEditorViewIdentity(*view),
                "transient_slot_reuse_old_generation_rejected")) return Failure("transient stale");
        for (unsigned cycle = 0; cycle != 8; ++cycle)
        {
            const auto old = *view;
            if (!scene->RetireEditorViewIdentity(old)) return Failure("bounded transient retirement");
            view = scene->CreateEditorViewIdentity();
            if (!Check(view && view->index == old.index && !scene->RenderData().Resolve(old) &&
                       scene->ReadRegistry().alive() == 1, "transient_storage_reuses_one_live_slot")) return Failure("transient storage");
        }
        auto a = scene->CreateEntity("Lifecycle A"), b = scene->CreateEntity("Lifecycle B");
        auto parent = scene->CreateEntity("Lifecycle parent");
        if (!a || !b || !parent) return Failure("CPU authored fixture creation");
        a->AddComponent<RenderCameraComponent>();
        b->AddComponent<RenderCameraComponent>();
        auto aid = scene->RenderData().Identify(*a), bid = scene->RenderData().Identify(*b);
        if (!aid || !bid) return Failure("CPU identity");
        auto activeIs = [&](EntityRenderId id)
        {
            auto active = scene->ActiveSceneCamera();
            return active && *active && **active == id;
        };
        if (!Check(scene->SetActiveSceneCamera(*aid, resources) && activeIs(*aid), "explicit_active_A")) return Failure("active");
        auto copied = _Scene::Copy(scene);
        if (!copied) return Failure("Scene copy");
        auto copiedActive = (*copied)->ActiveSceneCamera();
        auto copiedCamera = (*copied)->GetEntityByUUID(a->GetUUID());
        auto copiedId = (*copied)->RenderData().Identify(copiedCamera);
        unsigned sourceAuthored = 0, copyAuthored = 0, copyCameras = 0, copyTotal = 0;
        for (auto e : scene->ReadRegistry().view<IDComponent>()) { (void)e; ++sourceAuthored; }
        for (auto e : (*copied)->ReadRegistry().view<IDComponent>()) { (void)e; ++copyAuthored; }
        for (auto e : (*copied)->ReadRegistry().view<RenderCameraComponent>()) { (void)e; ++copyCameras; }
        (*copied)->ReadRegistry().each([&](auto) { ++copyTotal; });
        if (!Check(copiedActive && *copiedActive && copiedId && **copiedActive == *copiedId &&
                *copiedId != *aid && sourceAuthored == copyAuthored && copyCameras == 2 && copyTotal == copyAuthored,
                "copy_remaps_active_uuid_excludes_transient")) return Failure("copy exclusion");
        auto foreign = scene->SetActiveSceneCamera(*copiedId, resources);
        if (!Check(!foreign && foreign.error().code == SceneCameraErrorCode::ForeignId && activeIs(*aid),
                   "foreign_selection_preserves_active")) return Failure("foreign");
        const glm::vec3 local(3, 2, 10), offset(5, -1, 2);
        const glm::quat localQ(glm::vec3(0, .35f, 0)), parentQ(glm::vec3(0, .25f, 0));
        if (!scene->SetLocalPose(*a, local, localQ) || !scene->SetLocalPose(*parent, offset, parentQ) ||
            !scene->SetParent(*a, *parent)) return Failure("parent authoring");
        const auto steps = scene->GetPhysicsTiming().totalSteps;
        auto sampled = scene->SnapshotSceneCamera(*aid, resources, {});
        if (!sampled) return Failure("parent sampled camera");
        const auto expectedPosition = glm::dvec3(offset) + PreEditorCamera::Rotate(glm::dquat(parentQ), glm::dvec3(local));
        const auto expectedQ = glm::dquat(parentQ) * glm::dquat(localQ);
        if (!Check(PreEditorCamera::Max(sampled->Description().position - expectedPosition) <=
                PreEditorCamera::WorldLimit(expectedPosition) &&
                PreEditorCamera::Max(PreEditorCamera::Rotate(sampled->Description().orientation, {0,0,-1}) -
                    PreEditorCamera::Rotate(expectedQ, {0,0,-1})) <= 2e-5 &&
                scene->GetPhysicsTiming().totalSteps == steps, "sampled_rigid_parent_no_physics_tick")) return Failure("parent oracle");
        auto expected = sampled->Description(); expected.position = expectedPosition; expected.orientation = expectedQ;
        if (!Check(MatrixAndCoordinates(*sampled, expected), "parent_snapshot_matrix_coordinate_tolerances")) return Failure("parent math");
        const auto retained = *sampled;
        const auto frame = FrameCamera::FromSnapshot(*aid, retained);
        auto builder = RenderFrameBuilder::Create({.cameras = 1});
        if (!builder || !builder->AddCamera(frame)) return Failure("retained frame builder");
        auto retainedFrame = std::move(*builder).Finalize();
        if (!retainedFrame) return Failure("retained frame finalization");
        const auto retainedView = retained.ViewProjection();
        for (const auto scale : {glm::vec3(2,1,1), glm::vec3(-1,1,1), glm::vec3(0,1,1)})
        {
            if (!scene->SetLocalScale(*parent, scale)) return Failure("non-rigid fixture");
            auto bad = scene->SnapshotSceneCamera(*aid, resources, {});
            if (!Check(!bad && bad.error().code == SceneCameraErrorCode::InvalidPose && activeIs(*aid),
                       "scale_shear_reflection_singular_rejected")) return Failure("rigidity");
        }
        if (!scene->SetLocalScale(*parent, 1.f) || !scene->SetLocalTranslation(*parent, {6,-1,2})) return Failure("pose restore");
        a->GetComponent<RenderCameraComponent>().projection = CameraProjection::Orthographic;
        a->GetComponent<RenderCameraComponent>().orthographicHeight = 20;
        sampled = scene->SnapshotSceneCamera(*aid, resources, {});
        if (!Check(sampled && sampled->Description().projection == CameraProjection::Orthographic &&
                sampled->Description().position != retained.Description().position &&
                retained.ViewProjection() == retainedView && frame.MatchesSnapshot() &&
                frame.Snapshot()->ViewProjection() == retainedView, "projection_pose_edit_retains_old_snapshot_frame")) return Failure("retained");
        auto viewport = CameraViewport{}; viewport.pixelSize = {};
        auto empty = scene->SnapshotSceneCamera(*aid, resources, viewport);
        if (!Check(!empty && empty.error().code == SceneCameraErrorCode::EmptyViewport, "empty_viewport_typed")) return Failure("empty viewport");
        b->GetComponent<RenderCameraComponent>().verticalFovRadians = 0;
        auto invalid = scene->ReplaceSceneCamera(*aid, *bid, resources);
        if (!Check(!invalid && invalid.error().code == SceneCameraErrorCode::InvalidProjection &&
                   activeIs(*aid) && scene->RenderData().Resolve(*aid), "invalid_replacement_preserves_old")) return Failure("invalid replacement");
        b->GetComponent<RenderCameraComponent>().verticalFovRadians = glm::radians(45.f);
        b->RemoveComponent<RenderCameraComponent>();
        invalid = scene->SetActiveSceneCamera(*bid, resources);
        if (!Check(!invalid && invalid.error().code == SceneCameraErrorCode::MissingCamera && activeIs(*aid),
                   "missing_camera_preserves_active")) return Failure("missing camera");
        b->AddComponent<RenderCameraComponent>();
        b->RemoveComponent<Transform3DComponent>();
        invalid = scene->SetActiveSceneCamera(*bid, resources);
        if (!Check(!invalid && invalid.error().code == SceneCameraErrorCode::MissingTransform && activeIs(*aid),
                   "missing_transform_preserves_active")) return Failure("missing transform");
        b->AddComponent<Transform3DComponent>();
        b->Transform().Translation.x = std::numeric_limits<float>::quiet_NaN();
        invalid = scene->SetActiveSceneCamera(*bid, resources);
        if (!Check(!invalid && invalid.error().code == SceneCameraErrorCode::InvalidPose && activeIs(*aid),
                   "nonfinite_pose_preserves_active")) return Failure("nonfinite pose");
        b->Transform().Translation = {};
        {
            auto extraction = scene->RenderData().BeginExtraction();
            if (!extraction) return Failure("extraction fixture");
            auto blocked = scene->SetActiveSceneCamera(*bid, resources);
            auto viewBlocked = scene->RetireEditorViewIdentity(*view);
            if (!Check(!blocked && blocked.error().code == SceneCameraErrorCode::ExtractionActive &&
                    !viewBlocked && viewBlocked.error().code == SceneCameraErrorCode::ExtractionActive,
                    "extraction_rejects_lifecycle_mutation")) return Failure("extraction guard");
        }
        if (!Check(scene->ReplaceSceneCamera(*aid, *bid, resources) && activeIs(*bid) &&
                !scene->RenderData().Resolve(*aid) && frame.Snapshot()->ViewProjection() == retainedView &&
                retainedFrame->Cameras().size() == 1 && retainedFrame->Cameras()[0].entity == *aid &&
                retainedFrame->Cameras()[0].Snapshot()->ViewProjection() == retainedView,
                "replacement_new_identity_old_stale_retained_frame")) return Failure("replacement");
        auto reused = scene->CreateEntity("Reused A slot");
        if (!reused) return Failure("slot reuse creation");
        reused->AddComponent<RenderCameraComponent>();
        auto reuseId = scene->RenderData().Identify(*reused);
        auto stale = scene->SetActiveSceneCamera(*aid, resources);
        if (!Check(reuseId && reuseId->index == aid->index && *reuseId != *aid && !stale &&
                stale.error().code == SceneCameraErrorCode::StaleId && activeIs(*bid),
                "authored_slot_reuse_old_generation_rejected")) return Failure("authored stale");
        if (!Check(scene->DeleteSceneCamera(*reuseId) && activeIs(*bid), "inactive_delete_preserves_active")) return Failure("inactive delete");
        if (!scene->DeleteSceneCamera(*bid)) return Failure("active delete");
        auto cleared = scene->ActiveSceneCamera();
        if (!Check(cleared && !*cleared && !scene->RenderData().Resolve(*bid), "active_delete_clears_selection")) return Failure("active clearing");
        if (!scene->RetireEditorViewIdentity(*view)) return Failure("final transient retirement");
        const auto storage = retainedFrame->Storage();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_CPU_PASS checks={} transient_live=0 camera_live=0 retained_snapshot_values=true frame_used_bytes={} frame_capacity_bytes={} frame_allocations={}",
            probe.checks, storage.usedBytes, storage.capacityBytes, storage.storageAllocations);
        return {};
    }
#else
    inline float Scale() { return 1.f; }
#endif

#ifdef GENGINE_LIFECYCLE_CONTROL
    inline std::expected<bool, PlatformError> ControlUpdate(BaseApp& app)
    {
        app.SetManualFrameRateLimit(0);
        static unsigned frames = 0;
        ++frames;
        if (frames < 5)
            return true;
        auto* native = SDL_GetWindowFromID(app.GetWindow()->GetWindowID());
        if (!native || !PreEditorCamera::ActivateNativeWindow(native))
            return std::unexpected(PreEditorInput::Failure("Phase 17 control foreground").error());
        if (auto prepared = PreEditorInput::PrepareNativeFixture(app); !prepared)
            return std::unexpected(prepared.error());
        PreEditorInput::Describe(app);
        if (auto result = Measure(app, "editor"); !result)
            return std::unexpected(result.error());
        if (!app.GetInputManager()->CancelInput(InputCancelReason::Explicit) ||
            !PreEditorCamera::nativeFixture.Release() || !PreEditorInput::RequestNativeClose())
            return std::unexpected(PreEditorInput::Failure("Phase 17 control cleanup").error());
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_CONTROL_PASS");
        return false;
    }
#endif
}
