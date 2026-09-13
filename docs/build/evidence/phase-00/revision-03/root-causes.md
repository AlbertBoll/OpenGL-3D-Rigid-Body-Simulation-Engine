# Revision 03 root causes established before source changes

Lineage: Revision 02 seal `42e372560f4f0c2b650ba99ad735743a0c57e5fac3ac11aeadec89af74440885`. Initial and Revision 01/02 evidence remains immutable. The four accepted Revision 02 corrections and accepted Revision 01 TBB closure are unchanged.

## B00-RUNTIME-007: duplicate shared-resource cleanup

`EntryPoint.h` deletes the application through the virtual BaseApp destructor after Run returns. `SceneApp::~SceneApp` first frees AssetsManager, ShaderManager and ShapeManager, then performs its audio/camera cleanup. C++ subsequently invokes `BaseApp::~BaseApp`, which frees the same three shared managers. Texture and shader entries retain the freed pointers. The independently reproduced late-close probe `editor-baseline-late` used the verified checkpoint Editor layout, ran for 120 seconds without the framebuffer assertion, and then captured an access violation after WM_CLOSE: Texture destruction -> AssetsManager::FreeTextureResource/FreeAllResources -> BaseApp destructor line 28 -> SceneApp destructor line 49 -> EntryPoint. The debugger dump and source-resolved stack are retained separately from Revision 02.

Minimum correction: remove only SceneApp's three redundant shared-manager cleanup calls. Preserve its audio shutdown and camera deletion and all BaseApp cleanup. This preserves the existing base-class owner, matching the accepted Breakout correction.

## B00-RUNTIME-008: frame continues after shutdown input

The earlier framebuffer observation is independently reproducible with the documented `GEngineEditor` source CWD and exact protected `imgui.ini` checkpoint bytes (SHA-256 `d6facac12733ba8dd41ace25b4f972ece052128c3cf9afce933a46fdec266eb7`). `editor-baseline-early-repeat` and `editor-baseline-early-state` post WM_CLOSE after 45 seconds, while Editor initialization is still completing. The subsequent first frame processes the queued close: WindowManager removes the window, SDLWindow destroys the window and GL context, and the AppClose callback sets `m_Running=false`. BaseApp::Run nevertheless proceeds from ProcessInput to Update and Render.

Both independent stacks identify `RenderTarget::Invalidate`, `GEngine/src/Core/RenderTarget.cpp:266`, assertion `Framebuffer status error`, reached through OnResize(1280,720) -> SceneApp::Update:58 -> BaseApp::Run:319 -> EntryPoint:24. The live state capture explicitly prints `this->m_Running = false` and `windows->m_NumOfWindows = 0` at the assertion. The ordinary late-close run does not assert during initialization or normal rendering, and instead reaches the separate duplicate-free defect above.

This is a real shutdown-order defect exposed by an early-close probe: a frame executes application/render operations after shutdown has invalidated its context. It is not evidence requiring framebuffer or rendering architecture redesign. Minimum correction: immediately break the Run loop when ProcessInput has cleared m_Running, before Update/Render. Do not alter RenderTarget behavior, suppress the assertion, or skip BaseApp destruction.

## B00-RUNTIME-009: second redundant SceneApp deletion exposed during revalidation

After the two corrections above, `editor-fixed-early-Debug` proceeds past both earlier fault locations and reaches a later freed-pointer access violation in `std::default_delete<Actor>`, through CameraRig children -> Scene destruction -> BaseApp's owning scene member destruction. The retained derived destructor still manually deletes m_EditorCamera. The source establishes that SceneApp attaches this camera to CameraRig, whose Actor::Add(raw pointer) immediately adopts it into a unique_ptr child; the rig is then owned by BaseApp's scene. This is another redundant derived-class deletion in the same requested cleanup path, not a change to the ownership design.

Before removing that deletion, the independent late-close `editor-camera-ownership` capture prints the live m_EditorCamera pointer at SceneApp destructor entry: `0x000001e6bd183ed0`. The later failing unique_ptr delete targets exactly `0x000001e6bd183ed0`. The dump and complete source-resolved stack are retained. Minimum correction: remove the one redundant manual camera delete from SceneApp, retain the owning scene/Actor unique_ptr destruction and audio shutdown. No Actor/CameraRig/Scene ownership implementation or graph is changed, and the resource is still destroyed by its existing owner.

The first corrected early-close diagnostic script attempted to inspect BaseApp-specific locals from an Actor frame; CDB stopped that command list after recording the stack, before its optional dump/quit. Its harness exit 2 remains a failed capture with a demonstrated application AV, not a passing regression. The independent camera capture uses frame-independent diagnostics and successfully saves the dump. A separate Breakout resource trace has clean process exit code 0 and valid resource opens, but the first parser expected a different CDB exit phrase; its raw FAIL metadata is preserved and a corrected-parser repeat is separately named.

## Diagnostic limitations and source scope

`editor-baseline-early` records a sandbox launch denial (WinError 5), not an application result. The explicitly elevated repeat captured the assertion. `editor-baseline-state` successfully read the saved minidump but heap fields were absent; its unknown fields are not used as state evidence. The subsequent live-state capture resolves that limitation. All these raw attempts remain append-only.

Source ownership was frozen in local receipt `phase-00-revision-03-ownership-03.json` before editing SceneApp.cpp or BaseApp.cpp. Context expansion was limited to their entry point, event/window/SDL shutdown path, manager cleanup implementations, RenderTarget resize/assertion path, debugger capture code and prior failure evidence to establish these two causes. Every Editor probe retains generated layout bytes and restores exact verified checkpoint bytes afterward.
