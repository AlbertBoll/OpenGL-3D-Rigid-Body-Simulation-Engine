#pragma once

// Compiled only by the Phase 16 diagnostic overlay, in the owning RBS thread.
#include "Camera/CameraSnapshot.h"
#include "Renderer/RenderVisibility.h"
#include "Core/Platform.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <sdl2/SDL_syswm.h>
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace PreEditorCamera
{
    using namespace GEngine;
    using Projection = Component::CameraProjection;

    struct NativeFixture
    {
        HWND window = nullptr;
        bool wasTopmost = false, locked = false;
        bool Release()
        {
            const bool unlocked = !locked || LockSetForegroundWindow(LSFW_UNLOCK);
            if (unlocked)
                locked = false;
            const bool restored =
                !window || !IsWindow(window) ||
                SetWindowPos(window, wasTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (restored)
                window = nullptr;
            return unlocked && restored;
        }
        ~NativeFixture()
        {
            (void)Release();
        }
    };
    inline NativeFixture nativeFixture;

    inline bool ActivateNativeWindow(SDL_Window* window)
    {
        SDL_SysWMinfo native{};
        SDL_VERSION(&native.version);
        if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_WINDOWS)
            return false;
        const HWND handle = native.info.win.window;
        if (!nativeFixture.window)
        {
            nativeFixture.window = handle;
            nativeFixture.wasTopmost = (GetWindowLongPtr(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        }
        if (!SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            return false;
        if (!IsWindowVisible(handle))
            ShowWindow(handle, SW_SHOW);
        if (GetForegroundWindow() == handle && GetFocus() == handle)
            return true;
        TITLEBARINFO title{};
        title.cbSize = sizeof(title);
        if (!GetTitleBarInfo(handle, &title))
            return false;
        const POINT point{(title.rcTitleBar.left + title.rcTitleBar.right) / 2,
                          (title.rcTitleBar.top + title.rcTitleBar.bottom) / 2};
        const auto ownedCaption = [handle](POINT p)
        {
            return GetAncestor(WindowFromPoint(p), GA_ROOT) == handle &&
                   SendMessage(handle, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)) == HTCAPTION;
        };
        for (const auto key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_SHIFT, VK_CONTROL, VK_MENU})
            if (GetAsyncKeyState(key) & 0x8000)
                return false;
        if (!ownedCaption(point) || !SetCursorPos(point.x, point.y))
            return false;
        POINT actual{};
        if (!GetCursorPos(&actual) || !ownedCaption(actual))
            return false;
        INPUT click[2]{};
        click[0].type = click[1].type = INPUT_MOUSE;
        click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        const auto sent = SendInput(2, click, sizeof(INPUT));
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_CAPTION_ACTIVATE x={} y={} sent={}",
                                   actual.x, actual.y, sent);
        return sent == 2;
    }

    inline bool AcquireNativeForeground()
    {
        const auto handle = nativeFixture.window;
        if (!handle || !IsWindowVisible(handle) || GetForegroundWindow() != handle ||
            GetFocus() != handle)
            return false;
        if (!nativeFixture.locked)
        {
            nativeFixture.locked = LockSetForegroundWindow(LSFW_LOCK) != 0;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_FOREGROUND locked={}",
                                       nativeFixture.locked);
        }
        return nativeFixture.locked;
    }

    inline bool NativePointVisible(glm::dvec2 logical)
    {
        POINT point{static_cast<LONG>(logical.x), static_cast<LONG>(logical.y)};
        return nativeFixture.window && ClientToScreen(nativeFixture.window, &point) &&
               GetAncestor(WindowFromPoint(point), GA_ROOT) == nativeFixture.window;
    }

    // Analytic reference: quaternion vector rotation, explicit projection coefficients,
    // and similar triangles. No production conversion/inversion call supplies an oracle.
    inline glm::dvec3 Rotate(glm::dquat q, glm::dvec3 v)
    {
        q /= std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        const glm::dvec3 u(q.x, q.y, q.z);
        return v + 2.0 * glm::cross(u, glm::cross(u, v) + q.w * v);
    }
    inline glm::dvec3 World(const CameraSnapshotDesc& d, double x, double y, double depth)
    {
        const double h = d.projection == Projection::Perspective
                             ? depth * std::tan(d.verticalFovRadians / 2)
                             : d.orthographicHeight / 2;
        return d.position + Rotate(d.orientation,
                                   {x * h * double(d.viewport.pixelSize.x) / d.viewport.pixelSize.y,
                                    y * h, -depth});
    }
    inline glm::dvec2 Pixel(const CameraSnapshotDesc& d, glm::dvec3 world)
    {
        auto q = d.orientation;
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
        const auto v = Rotate(q, world - d.position);
        const double h = d.projection == Projection::Perspective
                             ? -v.z * std::tan(d.verticalFovRadians / 2)
                             : d.orthographicHeight / 2;
        return {(1 + v.x / (h * double(d.viewport.pixelSize.x) / d.viewport.pixelSize.y)) *
                    d.viewport.pixelSize.x / 2,
                (1 - v.y / h) * d.viewport.pixelSize.y / 2};
    }
    template <glm::length_t N> double Max(glm::vec<N, double> v)
    {
        double result = 0;
        for (int i = 0; i < N; ++i)
            result = (std::max)(result, std::abs(v[i]));
        return result;
    }
    inline double WorldLimit(glm::dvec3 p)
    {
        return 2e-4 + 2e-5 * (std::max)(1.0, Max(p));
    }

    struct Checks
    {
        std::ofstream rows{"camera-math.csv"};
        unsigned count = 0, failures = 0;
        Checks()
        {
            rows << "case,check,error,limit,pass\n" << std::setprecision(17);
        }
        void Check(const std::string& name, const char* check, double error, double limit)
        {
            const bool pass = std::isfinite(error) && error <= limit;
            rows << name << ',' << check << ',' << error << ',' << limit << ',' << pass << '\n';
            ++count;
            if (!pass)
                ++failures;
        }
        void Require(const std::string& name, const char* check, bool value)
        {
            Check(name, check, value ? 0 : 1, 0);
        }
    };

    inline bool Cpu(EntityRenderId identity)
    {
        Checks c;
        struct Case
        {
            const char* name;
            bool ortho;
            double field, nearPoint, farPoint, w, h;
            unsigned pw, ph;
            bool b;
        };
        const Case cases[]{{"P1", false, 45, .1, 1000, 1280, 720, 1280, 720, false},
                           {"P2", false, 60, .5, 100, 800, 600, 1000, 750, true},
                           {"P3", false, 30, .01, 100, 1920, 1080, 3840, 2160, false},
                           {"O1", true, 10, .1, 1000, 1280, 720, 1280, 720, false},
                           {"O2", true, 20, -.5, 100, 800, 600, 1200, 900, true},
                           {"O3", true, 4, 1, 20, 640, 480, 800, 720, true},
                           {"F1", false, 45, .1, 1000, 1279.75, 719.75, 1600, 900, true}};
        for (const auto& fixture : cases)
        {
            CameraSnapshotDesc d;
            d.projection = fixture.ortho ? Projection::Orthographic : Projection::Perspective;
            d.verticalFovRadians = fixture.field * std::numbers::pi / 180;
            d.orthographicHeight = fixture.field;
            d.nearPlane = fixture.nearPoint;
            d.farPlane = fixture.farPoint;
            d.position = fixture.b ? glm::dvec3(3, 2, 10) : glm::dvec3(0, 0, 10);
            if (fixture.b)
                d.orientation = glm::dquat(std::cos(std::numbers::pi / 12), 0,
                                           std::sin(std::numbers::pi / 12), 0) *
                                glm::dquat(std::cos(-std::numbers::pi / 24),
                                           std::sin(-std::numbers::pi / 24), 0, 0);
            d.viewport = {{17.25, 29.5}, {fixture.w, fixture.h}, {0, 0}, {fixture.pw, fixture.ph}};
            const auto snapshot = CameraSnapshot::Create(d);
            c.Require(fixture.name, "creation", bool(snapshot));
            if (!snapshot)
                continue;
            const auto& s = *snapshot;
            glm::dmat4 referenceView(1), referenceProjection(0);
            const auto right = Rotate(d.orientation, {1, 0, 0});
            const auto up = Rotate(d.orientation, {0, 1, 0});
            const auto back = Rotate(d.orientation, {0, 0, 1});
            for (int i = 0; i < 3; ++i)
            {
                referenceView[i][0] = right[i];
                referenceView[i][1] = up[i];
                referenceView[i][2] = back[i];
            }
            referenceView[3] = {-glm::dot(right, d.position), -glm::dot(up, d.position),
                                -glm::dot(back, d.position), 1};
            const double aspect = double(fixture.pw) / fixture.ph, n = d.nearPlane, f = d.farPlane;
            if (fixture.ortho)
            {
                referenceProjection[0][0] = 2 / (d.orthographicHeight * aspect);
                referenceProjection[1][1] = 2 / d.orthographicHeight;
                referenceProjection[2][2] = -2 / (f - n);
                referenceProjection[3][2] = -(f + n) / (f - n);
                referenceProjection[3][3] = 1;
            }
            else
            {
                referenceProjection[0][0] = 1 / (aspect * std::tan(d.verticalFovRadians / 2));
                referenceProjection[1][1] = 1 / std::tan(d.verticalFovRadians / 2);
                referenceProjection[2][2] = -(f + n) / (f - n);
                referenceProjection[3][2] = -2 * f * n / (f - n);
                referenceProjection[2][3] = -1;
            }
            const auto inverseIdentity = s.ViewProjection() * s.InverseViewProjection();
            const auto viewIdentity = glm::dmat4(s.View()) * s.InverseView();
            const auto projectionIdentity = glm::dmat4(s.Projection()) * s.InverseProjection();
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                {
                    c.Check(fixture.name, "view",
                            std::abs(s.View()[col][row] - referenceView[col][row]),
                            1e-5 + 2e-5 * std::abs(referenceView[col][row]));
                    c.Check(fixture.name, "projection",
                            std::abs(s.Projection()[col][row] - referenceProjection[col][row]),
                            1e-5 + 2e-5 * std::abs(referenceProjection[col][row]));
                    c.Check(fixture.name, "inverse",
                            std::abs(inverseIdentity[col][row] - (col == row ? 1 : 0)), 2e-4);
                    c.Check(fixture.name, "renderer-view-inverse",
                            std::abs(viewIdentity[col][row] - (col == row ? 1 : 0)), 2e-4);
                    c.Check(fixture.name, "renderer-projection-inverse",
                            std::abs(projectionIdentity[col][row] - (col == row ? 1 : 0)), 2e-4);
                }
            const glm::dvec2 samples[]{{0, 0}, {-.8, -.8}, {-.8, .8}, {.8, -.8}, {.8, .8}};
            glm::dvec3 firstDirection{};
            for (const auto xy : samples)
            {
                const glm::dvec2 pixel{(xy.x + 1) * fixture.pw / 2, (1 - xy.y) * fixture.ph / 2};
                const auto logical =
                    d.viewport.logicalOrigin +
                    pixel / glm::dvec2(d.viewport.pixelSize) * d.viewport.logicalSize;
                const auto mapped = s.LogicalToPixel(logical);
                c.Require(fixture.name, "logical-valid", bool(mapped));
                if (mapped)
                    c.Check(fixture.name, "logical-roundtrip", Max(*mapped - pixel), .05);
                // Binary-exact quarter positions exercise integer boundaries without decimal aliases.
                const auto lp = d.viewport.logicalOrigin + d.viewport.logicalSize * .25;
                const auto read = s.ReadbackPixelAt(lp);
                c.Require(fixture.name, "integer-readback",
                          read &&
                              *read == glm::uvec2(fixture.pw / 4, fixture.ph - 1 - fixture.ph / 4));
                const auto ray = s.PixelToRay(pixel);
                c.Require(fixture.name, "ray-valid", bool(ray));
                if (!ray)
                    continue;
                const auto nearPoint = World(d, xy.x, xy.y, n), farPoint = World(d, xy.x, xy.y, f);
                c.Check(fixture.name, "ray-normal", std::abs(glm::length(ray->direction) - 1),
                        2e-5);
                c.Check(fixture.name, "ray-nearPoint",
                        Max(ray->origin + ray->tNear * ray->direction - nearPoint),
                        WorldLimit(nearPoint));
                c.Check(fixture.name, "ray-farPoint",
                        Max(ray->origin + ray->tFar * ray->direction - farPoint),
                        WorldLimit(farPoint));
                c.Check(fixture.name, "ray-origin",
                        Max(ray->origin - (fixture.ortho ? nearPoint : d.position)),
                        WorldLimit(nearPoint));
                const auto rayPixel =
                    s.WorldToPixel(ray->origin + ray->direction * ((ray->tNear + ray->tFar) / 2));
                c.Require(fixture.name, "ray-reprojection-valid", bool(rayPixel));
                if (rayPixel)
                    c.Check(fixture.name, "ray-reprojection", Max(rayPixel->value - pixel), .05);
                if (fixture.ortho)
                {
                    c.Require(fixture.name, "ortho-interval", ray->tNear == 0);
                    c.Check(fixture.name, "ortho-parallel", Max(ray->direction + back), 2e-5);
                }
                else if (xy.x != 0)
                    c.Require(fixture.name, "offaxis-interval", ray->tNear > n && ray->tFar > f);
                const auto centerRay = s.PixelCenterToRay({fixture.pw / 2, fixture.ph / 2});
                c.Require(fixture.name, "pixel-center", bool(centerRay));
                if (centerRay)
                {
                    auto projected = s.WorldToPixel(centerRay->origin +
                                                    centerRay->direction *
                                                        ((centerRay->tNear + centerRay->tFar) / 2));
                    c.Require(fixture.name, "center-project", bool(projected));
                    if (projected)
                        c.Check(fixture.name, "half-pixel",
                                Max(projected->value -
                                    glm::dvec2(fixture.pw / 2 + .5, fixture.ph / 2 + .5)),
                                .05);
                }
                for (double depth : {n, (n + f) / 2, f})
                {
                    const auto world = World(d, xy.x, xy.y, depth);
                    const double z = fixture.ortho
                                         ? 2 * (depth - n) / (f - n) - 1
                                         : (f + n) / (f - n) - 2 * f * n / ((f - n) * depth);
                    const auto ndc = s.WorldToNdc(world);
                    const auto view = s.WorldToView(world);
                    const auto round = s.NdcToWorld({xy, z});
                    const auto projected = s.WorldToPixel(world);
                    c.Require(fixture.name, "conversion-valid", ndc && view && round && projected);
                    if (view)
                    {
                        const double h = fixture.ortho ? d.orthographicHeight / 2
                                                       : depth * std::tan(d.verticalFovRadians / 2);
                        const glm::dvec3 expected(xy.x * h * aspect, xy.y * h, -depth);
                        c.Check(fixture.name, "world-view", Max(*view - expected),
                                WorldLimit(expected));
                    }
                    if (ndc)
                    {
                        c.Check(fixture.name, "ndc", Max(ndc->value - glm::dvec3(xy, z)), 2e-5);
                        const auto returned = s.NdcToWorld(ndc->value);
                        c.Require(fixture.name, "world-roundtrip-valid", bool(returned));
                        if (returned)
                            c.Check(fixture.name, "world-roundtrip", Max(*returned - world),
                                    WorldLimit(world));
                    }
                    if (round)
                    {
                        c.Check(fixture.name, "world", Max(*round - world), WorldLimit(world));
                        const auto returned = s.WorldToNdc(*round);
                        c.Require(fixture.name, "ndc-roundtrip-valid", bool(returned));
                        if (returned)
                            c.Check(fixture.name, "ndc-roundtrip",
                                    Max(returned->value - glm::dvec3(xy, z)), 2e-5);
                    }
                    if (projected)
                        c.Check(fixture.name, "pixel", Max(projected->value - pixel), .05);
                }
            }
            const auto slice = s.SliceCorners(n + (f - n) * .1, n + (f - n) * .7);
            c.Require(fixture.name, "slice-valid", bool(slice));
            if (slice)
            {
                unsigned i = 0;
                for (int x : {-1, 1})
                    for (int y : {-1, 1})
                        for (double depth : {n + (f - n) * .1, n + (f - n) * .7})
                        {
                            const auto expected = World(d, x, y, depth);
                            c.Check(fixture.name, "slice-corner", Max((*slice)[i++] - expected),
                                    WorldLimit(expected));
                        }
            }
            auto camera = FrameCamera::FromSnapshot(identity, s);
            const auto frustum = CameraFrustum::FromCamera(camera);
            const auto clip = referenceProjection * referenceView;
            const auto row = [&](int i)
            {
                return glm::dvec4(clip[0][i], clip[1][i], clip[2][i], clip[3][i]);
            };
            for (int axis = 0; axis < 3; ++axis)
                for (int side = 0; side < 2; ++side)
                {
                    auto plane = row(3) + (side ? -row(axis) : row(axis));
                    plane /= glm::length(glm::dvec3(plane));
                    const auto actual = s.FrustumPlanes()[axis * 2 + side];
                    for (int k = 0; k < 4; ++k)
                        c.Check(fixture.name, "plane", std::abs(actual[k] - plane[k]),
                                1e-5 + 2e-5 * std::abs(plane[k]));
                }
            for (int x : {-1, 1})
                for (int y : {-1, 1})
                    for (double depth : {n, f})
                    {
                        const auto p = World(d, x, y, depth);
                        WorldBounds bounds;
                        bounds.status = BoundsStatus::Valid;
                        for (int i = 0; i < 3; ++i)
                            bounds.minimum[i] = bounds.maximum[i] = bounds.sphereCenter[i] = p[i];
                        c.Require(fixture.name, "boundary-retention",
                                  frustum.Test(bounds) != BoundsVisibility::Outside);
                    }
            for (const auto xy : {glm::dvec2(0, 0), glm::dvec2(1.01, 0), glm::dvec2(0, 1.01)})
            {
                const auto p = World(d, xy.x, xy.y, (n + f) / 2);
                WorldBounds bounds;
                bounds.status = BoundsStatus::Valid;
                for (int i = 0; i < 3; ++i)
                    bounds.minimum[i] = bounds.maximum[i] = bounds.sphereCenter[i] = p[i];
                c.Require(fixture.name, "renderer-frustum",
                          (frustum.Test(bounds) == BoundsVisibility::Outside) ==
                              (xy.x > 1 || xy.y > 1));
            }
            auto builder = RenderFrameBuilder::Create({.cameras = 1});
            for (double depth :
                 {n - 4e-4 * (std::max)(1.0, std::abs(n)), n + 4e-4 * (std::max)(1.0, std::abs(n)),
                  f - 4e-4 * (std::max)(1.0, std::abs(f)), f + 4e-4 * (std::max)(1.0, std::abs(f))})
            {
                const auto point = World(d, 0, 0, depth);
                WorldBounds bounds;
                bounds.status = BoundsStatus::Valid;
                for (int i = 0; i < 3; ++i)
                    bounds.minimum[i] = bounds.maximum[i] = bounds.sphereCenter[i] = point[i];
                c.Require(fixture.name, "renderer-depth-classification",
                          (frustum.Test(bounds) == BoundsVisibility::Outside) ==
                              (depth < n || depth > f));
            }
            c.Require(fixture.name, "builder", bool(builder));
            if (builder)
            {
                c.Require(fixture.name, "derived-add", bool(builder->AddCamera(camera)));
                auto retained = std::move(*builder).Finalize();
                camera.view[0][0] += 1;
                c.Require(fixture.name, "contradiction", !camera.MatchesSnapshot());
                auto rejected = RenderFrameBuilder::Create({.cameras = 1});
                c.Require(fixture.name, "contradiction-rejected",
                          rejected && !rejected->AddCamera(camera));
                c.Require(fixture.name, "retained",
                          retained && retained->Cameras()[0].view == s.View() &&
                              retained->Cameras()[0].Snapshot()->ViewProjection() ==
                                  s.ViewProjection());
                auto replacement = d;
                replacement.position += glm::dvec3(2, -3, 1);
                replacement.orientation = glm::dquat(1, 2, 3, 4);
                replacement.projection =
                    fixture.ortho ? Projection::Perspective : Projection::Orthographic;
                replacement.nearPlane = .25;
                replacement.farPlane += 10;
                replacement.viewport.logicalSize += glm::dvec2(17, 13);
                replacement.viewport.pixelSize += glm::uvec2(17, 13);
                const auto next = CameraSnapshot::Create(replacement);
                c.Require(fixture.name, "replacement-valid", bool(next));
                if (next)
                    camera = FrameCamera::FromSnapshot(identity, *next);
                const auto* held = retained ? retained->Cameras()[0].Snapshot() : nullptr;
                c.Require(fixture.name, "retained-after-replacement",
                          held && held->View() == s.View() &&
                              held->Projection() == s.Projection() &&
                              held->ViewProjection() == s.ViewProjection() &&
                              held->Description().position == s.Description().position &&
                              held->Description().orientation == s.Description().orientation &&
                              held->Description().projection == s.Description().projection &&
                              held->Description().viewport.logicalSize == d.viewport.logicalSize &&
                              held->Description().viewport.pixelSize == d.viewport.pixelSize);
            }
            c.Require(fixture.name, "right-edge",
                      !s.ReadbackPixelAt(d.viewport.logicalOrigin + d.viewport.logicalSize));
            c.Require(fixture.name, "negative-pixel", !s.PixelToRay({-1, 0}));
            c.Require(fixture.name, "outside-report",
                      !s.WorldToNdc(World(d, 1.1, 0, (n + f) / 2))->insideFrustum);
            if (!fixture.ortho)
                c.Require(fixture.name, "behind", !s.WorldToNdc(d.position + back));
        }
        const auto invalid = [&](const char* name, CameraSnapshotDesc d, CameraErrorCode code)
        {
            auto r = CameraSnapshot::Create(d);
            c.Require(name, "typed-error", !r && r.error().code == code);
        };
        CameraSnapshotDesc d;
        d.viewport.logicalSize.x = 0;
        invalid("zero-logical", d, CameraErrorCode::EmptyViewport);
        d = {};
        d.viewport.pixelSize.y = 0;
        invalid("zero-pixel", d, CameraErrorCode::EmptyViewport);
        d = {};
        d.viewport.logicalSize.y = -1;
        invalid("negative-size", d, CameraErrorCode::InvalidViewport);
        d = {};
        d.viewport.logicalSize.x = std::numeric_limits<double>::quiet_NaN();
        invalid("nan-size", d, CameraErrorCode::InvalidViewport);
        d = {};
        d.viewport.pixelOrigin.x = 8193;
        invalid("pixel-overflow", d, CameraErrorCode::InvalidViewport);
        d = {};
        d.orientation = {0, 0, 0, 0};
        invalid("zero-quaternion", d, CameraErrorCode::InvalidPose);
        d = {};
        d.position.x = std::numeric_limits<double>::infinity();
        invalid("infinite-pose", d, CameraErrorCode::InvalidPose);
        d = {};
        d.orientation.y = std::numeric_limits<double>::quiet_NaN();
        invalid("nan-pose", d, CameraErrorCode::InvalidPose);
        d = {};
        d.verticalFovRadians = std::numbers::pi;
        invalid("fov-pi", d, CameraErrorCode::InvalidProjection);
        d = {};
        d.nearPlane = 0;
        invalid("nearPoint-zero", d, CameraErrorCode::InvalidProjection);
        d = {};
        d.farPlane = d.nearPlane;
        invalid("unordered", d, CameraErrorCode::InvalidProjection);
        d = {};
        d.farPlane = std::numeric_limits<double>::infinity();
        invalid("infinite-farPoint", d, CameraErrorCode::InvalidProjection);
        d = {};
        d.farPlane = 1e100;
        invalid("unrepresentable-finite-farPoint", d, CameraErrorCode::NonInvertible);
        d = {};
        d.verticalFovRadians = std::numeric_limits<double>::quiet_NaN();
        invalid("nan-fov", d, CameraErrorCode::InvalidProjection);
        d.verticalFovRadians = 0;
        invalid("zero-fov", d, CameraErrorCode::InvalidProjection);
        d = {};
        d.projection = Projection::Orthographic;
        d.orthographicHeight = std::numeric_limits<double>::infinity();
        invalid("infinite-ortho-height", d, CameraErrorCode::InvalidProjection);
        d.orthographicHeight = 0;
        invalid("zero-ortho-height", d, CameraErrorCode::InvalidProjection);
        const auto valid = CameraSnapshot::Create({});
        c.Require("invalid-conversions", "valid-fixture", bool(valid));
        if (valid)
        {
            const auto nonfinite = glm::dvec3(std::numeric_limits<double>::quiet_NaN(), 0, 0);
            const auto view = valid->WorldToView(nonfinite);
            const auto ndc = valid->NdcToWorld(nonfinite);
            const auto pixel = valid->WorldToPixel(nonfinite);
            c.Require("invalid-conversions", "nonfinite-world-view",
                      !view && view.error().code == CameraErrorCode::NonFinite);
            c.Require("invalid-conversions", "nonfinite-ndc",
                      !ndc && ndc.error().code == CameraErrorCode::NonFinite);
            c.Require("invalid-conversions", "nonfinite-pixel",
                      !pixel && pixel.error().code == CameraErrorCode::NonFinite);
            for (const auto logical : {glm::dvec2(1280, 0), glm::dvec2(0, 720), glm::dvec2(-1, 0),
                                       glm::dvec2(nonfinite)})
            {
                const auto readback = valid->ReadbackPixelAt(logical);
                c.Require("invalid-conversions", "typed-outside",
                          !readback && readback.error().code == CameraErrorCode::OutsideViewport);
            }
            const auto before = valid->ViewProjection();
            d = {};
            d.viewport.logicalSize.y = 0;
            invalid("retained-invalid-replacement", d, CameraErrorCode::EmptyViewport);
            c.Require("invalid-conversions", "retained-after-failure",
                      before == valid->ViewProjection());
        }
        c.Require("scale", "invalid-scale", !ToFramebufferPixels({100, 100}, {0, 1}));
        c.Require("scale", "negative-scale", !ToFramebufferPixels({100, 100}, {-1, 1}));
        c.Require("scale", "nonfinite-scale",
                  !ToFramebufferPixels({100, 100}, {std::numeric_limits<float>::infinity(), 1}));
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_CPU checks={} failures={}", c.count,
                                   c.failures);
        return c.failures == 0 && bool(c.rows);
    }

    struct Runtime
    {
        unsigned updates = 0, segment = 0, stable = 0, observed = 0;
        unsigned applied = 99, readbacks = 0;
        bool cpu = false, pendingPick = false, waitingRestore = false;
        glm::uvec2 previousSize{};
        glm::dvec2 initialLogical{};
        std::ofstream rows{"camera-frames.csv"};
        Runtime()
        {
            rows
                << "segment,frame,logical_w,logical_h,pixel_w,pixel_h,scale,source_id,regenerated_id,background_id\n";
        }
    };
    inline Runtime runtime;
    inline double Scale()
    {
        return runtime.segment == 3 ? 1.25 : runtime.segment == 4 ? 1.5 : 1;
    }
    inline bool Ortho()
    {
        return runtime.segment == 1 || runtime.segment == 4;
    }
    inline bool Rotated()
    {
        return runtime.segment == 2;
    }
    inline constexpr const char* Targets[]{"Phase08 Source", "Phase08 Regenerated"};
    inline constexpr glm::dvec3 Points[]{{-6, 2, 10}, {6, 2, 7}};

    inline bool Interior(const CameraSnapshotDesc& d, unsigned target, glm::dvec2 p)
    {
        const double x = target ? 6 : -6, y = 2, z = target ? 7 : 10, h = target ? 1.5 : 3;
        const glm::dvec3 corners[]{
            {x - 2, y - h, z}, {x + 2, y - h, z}, {x + 2, y + h, z}, {x - 2, y + h, z}};
        double margin = std::numeric_limits<double>::infinity();
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto a = Pixel(d, corners[i]), b = Pixel(d, corners[(i + 1) % 4]);
            const auto v = b - a;
            const double t = std::clamp(glm::dot(p - a, v) / glm::dot(v, v), 0.0, 1.0);
            margin = (std::min)(margin, glm::length(p - (a + t * v)));
        }
        return margin >= 4 && p.x >= 4 && p.y >= 4 && p.x < d.viewport.pixelSize.x - 4 &&
               p.y < d.viewport.pixelSize.y - 4;
    }
    inline bool Capture(const RenderTarget& target, unsigned segment)
    {
        const auto& d = target.Buffer().Description();
        std::vector<std::byte> rgba(std::size_t(d.Width) * d.Height * 4);
        if (!target.ReadColor(rgba))
            return false;
        std::ofstream file("camera-view-" + std::to_string(segment) + ".ppm", std::ios::binary);
        file << "P6\n" << d.Width << ' ' << d.Height << "\n255\n";
        for (unsigned y = d.Height; y > 0; --y)
            for (unsigned x = 0; x < d.Width; ++x)
                file.write(reinterpret_cast<const char*>(rgba.data() + ((y - 1) * d.Width + x) * 4),
                           3);
        return bool(file);
    }
}
