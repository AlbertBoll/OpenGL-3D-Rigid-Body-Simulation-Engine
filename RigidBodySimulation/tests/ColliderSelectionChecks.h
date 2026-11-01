#pragma once
// Opt-in OS-input/GPU-picking observation. Never compiled into the ordinary build.
#include <sdl2/SDL.h>
#include <sdl2/SDL_syswm.h>
#include <windows.h>
#include <cstdio>

namespace Rbs
{
    bool ColliderSmokeControlPoint(const char*, ::GEngine::Math::Vec2f&);
    std::size_t ColliderSmokePendingCapsules();
}

namespace Rbs::ColliderSelectionProbe
{
    struct Probe
    {
        FILE* log{};
        unsigned tick{}, checks{}, frameLines{}, cyanLeft{}, cyanRight{}, proposedLeft{}, proposedRight{};
        bool failed{}, done{}, physicalCtrlClick{};
        HWND window{}, previousForeground{};
        POINT previousCursor{};
        _Entity sphere, box;
        PhysicsBodyHandle a{}, b{};
        std::uintptr_t shapeA{}, shapeB{};
        std::uint64_t worldContext{};
        std::optional<Subscription> trace;
    };
    inline Probe probe;

    inline bool Check(bool value, const char* name)
    {
        ++probe.checks;
        std::fprintf(probe.log, "CHECK\t%s\t%s\n", value ? "PASS" : "FAIL", name);
        std::fflush(probe.log);
        probe.failed |= !value;
        return value;
    }
    inline bool Key(WORD key, bool down)
    {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    }
    inline bool Mouse(bool down)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    }
    inline bool Move(Vec2f client)
    {
        POINT point{LONG(client.x), LONG(client.y)};
        return ClientToScreen(probe.window, &point) && SetCursorPos(point.x, point.y);
    }
    inline void Finish(const char* result)
    {
        (void)Key(VK_LCONTROL, false);
        (void)Mouse(false);
        probe.trace.reset();
        std::fprintf(probe.log, "SUMMARY\t%s\tchecks=%u\n", result, probe.checks);
        std::fclose(probe.log);
        probe.log = nullptr;
        probe.done = true;
        SetCursorPos(probe.previousCursor.x, probe.previousCursor.y);
        if (IsWindow(probe.previousForeground)) SetForegroundWindow(probe.previousForeground);
    }
}

void RigidBodySimulationApp::ObserveColliderSelectionFrame(const RenderFrame& frame)
{
    using namespace Rbs::ColliderSelectionProbe;
    if (!probe.log || probe.done) return;
    probe.frameLines = unsigned(frame.DebugLines().size());
    probe.cyanLeft = probe.cyanRight = probe.proposedLeft = probe.proposedRight = 0;
    for (const auto& line : frame.DebugLines())
    {
        const bool cyan = line.color.r == 0 && line.color.g == 1 && line.color.b == 1;
        const bool proposed = line.color.r == 1 && line.color.b == .15f;
        if (cyan) ++(line.start.x < 0 ? probe.cyanLeft : probe.cyanRight);
        if (proposed) ++(line.start.x < 0 ? probe.proposedLeft : probe.proposedRight);
    }
    std::fprintf(probe.log, "FRAME\t%u\tselected=%zu\tlines=%u\tcyan=%u,%u\tproposed=%u,%u\n",
        probe.tick, m_ColliderOutlines.count, probe.frameLines, probe.cyanLeft, probe.cyanRight,
        probe.proposedLeft, probe.proposedRight);
}

bool RigidBodySimulationApp::ValidateColliderSelectionUpdate()
{
    using namespace Rbs::ColliderSelectionProbe;
    if (probe.done) return false;
    ++probe.tick;
    const auto finish = [&](const char* result)
    {
        Finish(result);
        if (probe.failed)
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Collider selection", {}, "focused smoke", {},
                "Selection/input/render gate failed"});
        else ShutDown();
        return false;
    };
    if (probe.tick == 1)
    {
        const char* path = std::getenv("GENGINE_COLLIDER_SELECTION_SMOKE_OUTPUT");
        if (fopen_s(&probe.log, path, "w") || !probe.log)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Collider selection", {}, "open evidence", {}, "No output"});
            return false;
        }
        std::fprintf(probe.log, "SCOPE\tWin32 SendInput -> SDL -> physical modifier state -> RBS -> GPU picking -> selected set -> submitted debug lines; not human visual acceptance\n");
        probe.previousForeground = GetForegroundWindow();
        GetCursorPos(&probe.previousCursor);
        auto* window = SDL_GetWindowFromID(GetWindow()->GetWindowID());
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS)
        {
            std::fprintf(probe.log, "ENVIRONMENT\tWindows desktop handle unavailable\n");
            return finish("UNAVAILABLE");
        }
        probe.window = info.info.win.window;
        ShowWindow(probe.window, SW_RESTORE);
        const DWORD current = GetCurrentThreadId();
        const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attach = foreground && foreground != current;
        const bool attached = !attach || AttachThreadInput(current, foreground, TRUE);
        const bool activated = attached && (GetForegroundWindow() == probe.window || SetForegroundWindow(probe.window));
        SetFocus(probe.window);
        const bool detached = !attach || !attached || AttachThreadInput(current, foreground, FALSE);
        if (!activated || !detached)
        {
            std::fprintf(probe.log, "ENVIRONMENT\tForeground acquisition unavailable\tattached=%d\tactivated=%d\tdetached=%d\n", attached, activated, detached);
            return finish("UNAVAILABLE");
        }
        for (const auto id : m_ActiveScene->ReadRegistry().view<RigidBody3DComponent, MeshRendererComponent>())
        {
            _Entity e{id, m_ActiveScene.get()};
            const auto binding = m_ActiveScene->GetBodyBinding(e);
            if (!binding) continue;
            const auto shape = m_ActiveScene->GetPhysicsSystem()->ReadBodyCollider(*binding);
            if (shape && shape->type == ShapeType::Sphere) probe.sphere = e;
            if (shape && shape->type == ShapeType::Box && e.GetComponent<RigidBody3DComponent>().Type == BodyType::Dynamic) probe.box = e;
        }
        if (!Check(probe.sphere && probe.box, "ordinary SphereBoxStack native entities resolved")) return finish("FAIL");
        m_IsPause = true;
        m_ActiveScene->SetPaused(true);
        if (!Check(m_ActiveScene->SetLocalTranslation(probe.sphere, {-3, 5, -20}).has_value() &&
                   m_ActiveScene->SetLocalTranslation(probe.box, {3, 5, -20}).has_value(),
                "separate visible input fixtures authored through Scene")) return finish("FAIL");
        probe.a = *m_ActiveScene->GetBodyBinding(probe.sphere);
        probe.b = *m_ActiveScene->GetBodyBinding(probe.box);
        auto& physics = *m_ActiveScene->GetPhysicsSystem();
        probe.shapeA = physics.ReadBodyCollider(probe.a)->identity;
        probe.shapeB = physics.ReadBodyCollider(probe.b)->identity;
        probe.worldContext = physics.ColliderContextRevision();
        ClearColliderSelection();
        FocusMaterialCamera(m_EditorCamera_, {0, 5, -20}, .15f, Math::Pi, 20);
        auto trace = GetInputManager()->Observe([](const InputEvent& event)
        {
            if (!probe.log) return;
            if (event.kind == InputKind::ButtonDown || event.kind == InputKind::KeyDown)
                std::fprintf(probe.log, "INPUT\ttick=%u\tkind=%d\tkey=%u\tmodifiers=%u\tdestination=%d\tcleanup=%d\n",
                    probe.tick, int(event.kind), unsigned(event.key), unsigned(event.modifiers), int(event.destination), int(event.cleanup));
            if (event.kind == InputKind::ButtonDown && event.destination == InputLayer::View &&
                (event.modifiers & (LeftControl | RightControl))) probe.physicalCtrlClick = true;
        });
        if (!Check(bool(trace), "observe routed native input")) return finish("FAIL");
        probe.trace = std::move(*trace);
    }
    const auto target = [&](const _Entity& e)
    {
        if (!m_CameraSnapshot) return false;
        const auto position = m_ActiveScene->ReadBodyTransform(e);
        if (!position) return false;
        const auto projected = m_CameraSnapshot->WorldToPixel(position->presentation.translation);
        if (!projected || !projected->insideFrustum) return false;
        const auto& viewport = m_CameraSnapshot->Description().viewport;
        const auto logical = viewport.logicalOrigin + projected->value / glm::dvec2(viewport.pixelSize) * viewport.logicalSize;
        const auto pixel = m_CameraSnapshot->ReadbackPixelAt(logical);
        if (!pixel) return false;
        const auto value = m_MousePickFrameBuffer->ReadPixel(int(pixel->x), int(pixel->y));
        if (!value) return false;
        const auto actual = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *value);
        const auto expected = m_ActiveScene->RenderData().Identify(e);
        std::fprintf(probe.log, "TARGET\ttick=%u\tentity=%u\tlogical=%.3f,%.3f\tgpuMatches=%d\n", probe.tick,
            unsigned(e), logical.x, logical.y, int(actual && expected && *actual == *expected));
        return actual && expected && *actual == *expected && Move(Vec2f(logical));
    };
    const auto control = [&](const char* name)
    {
        Vec2f point;
        return Rbs::ColliderSmokeControlPoint(name, point) && Move(point);
    };
    switch (probe.tick)
    {
    case 14: Check(target(probe.sphere) && Mouse(true), "physical viewport activation press"); break;
    case 16: Check(Mouse(false), "physical activation release"); break;
    case 22: Check(target(probe.sphere) && Mouse(true), "physical normal Sphere pick"); break;
    case 24: Check(Mouse(false), "normal pick release"); break;
    case 30:
        Check(m_ColliderOutlines.count == 1 && m_HoveredEntity == probe.sphere &&
            m_ColliderOutlines.Contains(probe.a) && probe.cyanLeft > 0 && probe.cyanRight == 0,
            "one selected Sphere reaches submitted current geometry"); break;
    case 32: Check(Key(VK_LCONTROL, true), "OS Ctrl press"); break;
    case 35: Check(target(probe.box) && Mouse(true), "OS Ctrl+Click Box press"); break;
    case 37: Check(Mouse(false), "OS multi-pick release"); break;
    case 39: Check(Key(VK_LCONTROL, false), "OS Ctrl release"); break;
    case 45:
        Check(probe.physicalCtrlClick && m_ColliderOutlines.count == 2 && m_HoveredEntity == probe.box &&
            m_ColliderOutlines.Contains(probe.a) && m_ColliderOutlines.Contains(probe.b) &&
            probe.cyanLeft > 0 && probe.cyanRight > 0, "physical Ctrl reaches GPU picking and both current colliders are submitted"); break;
    case 48: Check(control("New Collider Type") && Mouse(true), "OS proposed-type combo press"); break;
    case 50: Check(Mouse(false), "OS combo release"); break;
    case 55: Check(control("Capsule") && Mouse(true), "OS settled Capsule item press"); break;
    case 57: Check(Mouse(false), "OS Capsule selection release"); break;
    case 65:
        Check(Rbs::ColliderSmokePendingCapsules() == 2 && probe.cyanLeft > 0 && probe.cyanRight > 0 &&
            probe.proposedLeft > 0 && probe.proposedRight > 0, "shared Capsule type produces current and candidate geometry for both selected bodies"); break;
    case 68: Check(target(probe.box) && Mouse(true), "reactivate viewport without losing selected set"); break;
    case 70: Check(Mouse(false), "reactivation release"); break;
    case 75: Check(Key(VK_LCONTROL, true), "Ctrl toggle-off press"); break;
    case 78: Check(target(probe.box) && Mouse(true), "Ctrl toggle-off Box"); break;
    case 80: Check(Mouse(false), "toggle-off release"); break;
    case 82: Check(Key(VK_LCONTROL, false), "toggle-off Ctrl release"); break;
    case 88:
        Check(m_ColliderOutlines.count == 1 && m_HoveredEntity == probe.sphere &&
            Rbs::ColliderSmokePendingCapsules() == 1 && probe.proposedLeft > 0 && probe.cyanRight == 0,
            "removing active Box retains Sphere selection and its proposal"); break;
    case 90: Check(Key(VK_LCONTROL, true), "Ctrl re-add press"); break;
    case 93: Check(target(probe.box) && Mouse(true), "Ctrl re-add Box"); break;
    case 95: Check(Mouse(false), "re-add release"); break;
    case 97: Check(Key(VK_LCONTROL, false), "re-add Ctrl release"); break;
    case 104: Check(m_ColliderOutlines.count == 2, "two members selected before Esc"); break;
    case 108: Check(Key(VK_ESCAPE, true), "physical Esc press"); break;
    case 110: Check(Key(VK_ESCAPE, false), "physical Esc release"); break;
    case 118:
        Check(!m_HoveredEntity && m_ColliderOutlines.count == 0 && Rbs::ColliderSmokePendingCapsules() == 0 &&
            probe.frameLines == 0, "Esc clears full selection active entity previews and Selected geometry");
        m_IsShowDebugBoundingBox = true;
        break;
    case 124: Check(target(probe.sphere) && Mouse(true), "normal select with independent All scope enabled"); break;
    case 126: Check(Mouse(false), "All-scope pick release"); break;
    case 132: Check(m_IsShowDebugBoundingBox && m_ColliderOutlines.count == 1, "All scope retains independent selection"); break;
    case 136: Check(Key(VK_ESCAPE, true), "Esc in All scope"); break;
    case 138: Check(Key(VK_ESCAPE, false), "All Esc release"); break;
    case 146:
    {
        Check(m_IsShowDebugBoundingBox && !m_HoveredEntity && !m_ColliderOutlines.count && probe.frameLines > 0,
            "Esc clears selection while preserving independently enabled All diagnostics");
        auto& physics = *m_ActiveScene->GetPhysicsSystem();
        Check(physics.ReadBodyCollider(probe.a)->identity == probe.shapeA &&
            physics.ReadBodyCollider(probe.b)->identity == probe.shapeB &&
            physics.ColliderContextRevision() == probe.worldContext,
            "all physical selection and preview actions preserve native Shapes and Physics context");
        return finish(probe.failed ? "FAIL" : "PASS");
    }
    default: break;
    }
    if (probe.failed) return finish("FAIL");
    return true;
}
