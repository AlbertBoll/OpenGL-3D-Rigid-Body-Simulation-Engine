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

#ifdef GENGINE_LIFECYCLE_ORACLE_EVIDENCE
#if !defined(GENGINE_LIFECYCLE_VALIDATION) || defined(GENGINE_LIFECYCLE_CONTROL)
#error Phase 17 oracle evidence requires the qualifying lifecycle lane.
#endif
#include <array>
#include <cstdint>
#include <locale>
#endif

#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
#if !defined(GENGINE_LIFECYCLE_VALIDATION) || defined(GENGINE_LIFECYCLE_CONTROL) || defined(GENGINE_CONFIG_RELEASE)
#error Phase 17 focus tracing is admitted only in the diagnostic Debug lifecycle lane.
#endif
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <sdl2/SDL_system.h>
#endif

namespace PreEditorCamera
{
    using namespace GEngine;
    using Projection = Component::CameraProjection;

#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
    namespace FocusTrace
    {
        // A reservation ordinal orders observer entry, not native event occurrence.
        // Slots are written once and become readable only through committed/release.
        constexpr unsigned Capacity = 8192;
        enum : unsigned { Started = 1, SdlEnqueue = 2, WindowsMessage = 3, WinEvent = 4,
                          NativePredicate = 5, OuterGate = 6, Finished = 7, ClockPair = 8, ActivationBranch = 11 };
        struct Record
        {
            std::uint64_t ordinal{}, qpcBegin{}, qpcEnd{}, sourceTime{}, a{}, b{}, c{}, d{};
            std::uint64_t ownWindow{}, window{}, foreground{}, threadFocus{};
            std::uint64_t foregroundActive{}, foregroundFocus{}, targetActive{}, targetFocus{};
            unsigned kind{}, origin{}, callerThread{}, sourceThread{}, sourceClock{};
            unsigned ownProcess{}, ownThread{}, windowProcess{}, windowThread{};
            unsigned foregroundProcess{}, foregroundThread{}, focusProcess{}, focusThread{};
            unsigned updates{}, segment{}, stable{}, observed{}, frameBefore{}, frameAfter{};
            unsigned sdlWindow{}, keyboardWindow{}, mouseWindow{}, flags{}, error{};
            int engine = -1, valid = -1, visible = -1, foregroundSelf = -1, threadFocusSelf = -1;
            int lockBefore = -1, lockAfter = -1, lockReturn = -1, result = -1;
            int relative = -1, foregroundGui = -1, targetGui = -1;
        };
        static_assert(std::is_trivially_copyable_v<Record>);
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic<unsigned>::is_always_lock_free);
        struct Slot { Record record{}; std::atomic<bool> committed{false}; };
        // Trivial static storage survives Finish and any callback already in flight.
        inline std::array<Slot, Capacity> slots{};
        inline std::atomic<std::uint64_t> next{0}, overflow{0}, watchCalls{0}, messageCalls{0}, winEventCalls{0};
        inline std::atomic<unsigned> writers{0}, frameVersion{0}, updates{0}, segment{0}, stable{0}, observed{0};
        inline std::atomic<bool> enabled{false}, begun{false}, finished{false}, clockFailure{false};
        inline thread_local unsigned injectionOrigin = 0;
        inline SDL_Window* sdlWindow = nullptr;
        inline HWND ownWindow = nullptr;
        inline DWORD ownProcess = 0, ownThread = 0;
        inline Uint32 ownSdlWindow = 0;
        inline LARGE_INTEGER frequency{};
        inline std::array<HWINEVENTHOOK, 4> hooks{};
        inline std::array<DWORD, 4> hookErrors{}, unhookErrors{};
        inline bool watchRequested = false, messageRequested = false;
        inline unsigned setupError = 0;

        struct PreserveError
        {
            DWORD value = GetLastError();
            ~PreserveError() { SetLastError(value); }
        };
        inline std::uint64_t Handle(HWND handle) noexcept
        { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle)); }
        inline std::uint64_t Pair(unsigned a, unsigned b) noexcept
        { return (std::uint64_t(a) << 32) | b; }
        inline std::uint64_t Clock() noexcept
        {
            LARGE_INTEGER value{};
            if (!QueryPerformanceCounter(&value)) clockFailure.store(true);
            return static_cast<std::uint64_t>(value.QuadPart);
        }
        struct Write
        {
            Slot* slot = nullptr;
            explicit Write(unsigned kind) noexcept
            {
                PreserveError preserve;
                // The counter must precede enabled: Finish also detects a producer
                // paused after observing true but before reserving its slot.
                writers.fetch_add(1);
                if (!enabled.load()) return;
                const auto index = next.fetch_add(1);
                if (index >= Capacity) { overflow.fetch_add(1); return; }
                slot = &slots[static_cast<unsigned>(index)];
                auto& r = slot->record;
                r.ordinal = index + 1; r.qpcBegin = Clock(); r.kind = kind;
                r.callerThread = GetCurrentThreadId(); r.origin = injectionOrigin;
                r.ownWindow = Handle(ownWindow); r.ownProcess = ownProcess; r.ownThread = ownThread;
                r.frameBefore = frameVersion.load();
                r.updates = updates.load(); r.segment = segment.load();
                r.stable = stable.load(); r.observed = observed.load();
                r.frameAfter = frameVersion.load();
            }
            Write(const Write&) = delete;
            Write& operator=(const Write&) = delete;
            ~Write()
            {
                PreserveError preserve;
                if (slot)
                {
                    slot->record.qpcEnd = Clock();
                    slot->committed.store(true, std::memory_order_release);
                }
                writers.fetch_sub(1);
            }
            Record* Get() noexcept { return slot ? &slot->record : nullptr; }
        };
        inline void SetFrame(unsigned update, unsigned currentSegment, unsigned currentStable, unsigned currentObserved) noexcept
        {
            frameVersion.fetch_add(1);
            updates.store(update); segment.store(currentSegment);
            stable.store(currentStable); observed.store(currentObserved);
            frameVersion.fetch_add(1);
        }
        inline void Mark(unsigned kind, std::uint64_t a = 0, std::uint64_t b = 0,
                         std::uint64_t c = 0, std::uint64_t d = 0) noexcept
        {
            Write write(kind);
            if (auto* r = write.Get()) { r->a = a; r->b = b; r->c = c; r->d = d; }
        }
        class Injection
        {
            unsigned previous;
        public:
            explicit Injection(unsigned origin) noexcept : previous(injectionOrigin) { injectionOrigin = origin; }
            ~Injection() { injectionOrigin = previous; }
            Injection(const Injection&) = delete;
            Injection& operator=(const Injection&) = delete;
        };
        inline void Identity(HWND window, std::uint64_t& handle, unsigned& process, unsigned& thread) noexcept
        {
            handle = Handle(window);
            DWORD pid = 0;
            thread = window ? GetWindowThreadProcessId(window, &pid) : 0;
            process = pid;
        }
        inline void NativeSample(Record& r, HWND eventWindow) noexcept
        {
            Identity(eventWindow, r.window, r.windowProcess, r.windowThread);
            const auto foreground = GetForegroundWindow();
            Identity(foreground, r.foreground, r.foregroundProcess, r.foregroundThread);
            // GetFocus describes this callback's caller thread, never global focus.
            Identity(GetFocus(), r.threadFocus, r.focusProcess, r.focusThread);
        }
        inline void Sample(unsigned kind, int engine = -1) noexcept
        {
            PreserveError preserve;
            Write write(kind);
            if (auto* r = write.Get())
            {
                NativeSample(*r, ownWindow);
                r->engine = engine; r->valid = ownWindow && IsWindow(ownWindow);
                r->visible = ownWindow && IsWindowVisible(ownWindow);
                r->foregroundSelf = r->foreground == r->ownWindow;
                r->threadFocusSelf = r->threadFocus == r->ownWindow;
                GUITHREADINFO foregroundInfo{}; foregroundInfo.cbSize = sizeof(foregroundInfo);
                if (r->foregroundThread)
                {
                    r->foregroundGui = GetGUIThreadInfo(r->foregroundThread, &foregroundInfo) != 0;
                    if (r->foregroundGui)
                    { r->foregroundActive = Handle(foregroundInfo.hwndActive); r->foregroundFocus = Handle(foregroundInfo.hwndFocus); }
                }
                GUITHREADINFO targetInfo{}; targetInfo.cbSize = sizeof(targetInfo);
                r->targetGui = GetGUIThreadInfo(ownThread, &targetInfo) != 0;
                if (r->targetGui)
                { r->targetActive = Handle(targetInfo.hwndActive); r->targetFocus = Handle(targetInfo.hwndFocus); }
                // SDL getters are confined to the owning application thread.
                if (r->callerThread == ownThread)
                {
                    r->sdlWindow = ownSdlWindow; r->flags = SDL_GetWindowFlags(sdlWindow);
                    auto* keyboard = SDL_GetKeyboardFocus(); auto* mouse = SDL_GetMouseFocus();
                    r->keyboardWindow = keyboard ? SDL_GetWindowID(keyboard) : 0;
                    r->mouseWindow = mouse ? SDL_GetWindowID(mouse) : 0;
                    r->relative = int(SDL_GetRelativeMouseMode());
                }
            }
        }
        inline void Gate(bool native, int engine) noexcept
        {
            Write write(OuterGate);
            if (auto* r = write.Get())
            { r->a = native; r->engine = engine; r->result = native && engine == 1; }
        }
        inline void ClockAnchor() noexcept
        {
            PreserveError preserve;
            Write write(ClockPair);
            if (auto* r = write.Get())
            {
                r->result = r->callerThread == ownThread;
                if (r->result)
                {
                    // Each clock read falls within this record's QPC interval;
                    // they are not simultaneous and retain millisecond granularity.
                    r->a = GetTickCount64(); r->b = GetTickCount();
                    r->c = SDL_GetTicks64(); r->d = SDL_GetTicks();
                }
            }
        }
        inline int SDLCALL Watch(void*, SDL_Event* event) noexcept
        {
            PreserveError preserve;
            Write write(SdlEnqueue);
            if (auto* r = write.Get())
            {
                watchCalls.fetch_add(1); r->sourceClock = 1; r->sourceTime = event->common.timestamp;
                r->a = event->type;
                switch (event->type)
                {
                case SDL_WINDOWEVENT:
                    r->sdlWindow = event->window.windowID; r->b = event->window.event;
                    r->c = static_cast<Uint32>(event->window.data1); r->d = static_cast<Uint32>(event->window.data2); break;
                case SDL_KEYDOWN: case SDL_KEYUP:
                    r->sdlWindow = event->key.windowID; r->b = event->key.keysym.scancode;
                    r->c = static_cast<Uint32>(event->key.keysym.sym);
                    r->d = event->key.state | (unsigned(event->key.repeat) << 8) | (unsigned(event->key.keysym.mod) << 16); break;
                case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
                    r->sdlWindow = event->button.windowID; r->b = event->button.button;
                    r->c = Pair(static_cast<unsigned>(event->button.x), static_cast<unsigned>(event->button.y));
                    r->d = event->button.state | (unsigned(event->button.clicks) << 8); break;
                case SDL_MOUSEMOTION:
                    r->sdlWindow = event->motion.windowID;
                    r->b = Pair(static_cast<unsigned>(event->motion.x), static_cast<unsigned>(event->motion.y));
                    r->c = Pair(static_cast<unsigned>(event->motion.xrel), static_cast<unsigned>(event->motion.yrel));
                    r->d = event->motion.state; break;
                case SDL_MOUSEWHEEL:
                    r->sdlWindow = event->wheel.windowID;
                    r->b = Pair(static_cast<unsigned>(event->wheel.x), static_cast<unsigned>(event->wheel.y));
                    r->c = event->wheel.direction; break;
                case SDL_TEXTINPUT: r->sdlWindow = event->text.windowID; break;
                case SDL_TEXTEDITING: r->sdlWindow = event->edit.windowID; break;
                }
                NativeSample(*r, r->sdlWindow == ownSdlWindow ? ownWindow : nullptr);
            }
            return 1; // AddEventWatch ignores this; no event is filtered or mutated.
        }
        inline void SDLCALL Message(void*, void* window, unsigned message, Uint64 wparam, Sint64 lparam) noexcept
        {
            switch (message)
            {
            case WM_ACTIVATE: case WM_ACTIVATEAPP: case WM_NCACTIVATE: case WM_MOUSEACTIVATE:
            case WM_SETFOCUS: case WM_KILLFOCUS: case WM_SHOWWINDOW: case WM_SIZE:
            case WM_WINDOWPOSCHANGED: case WM_SYSCOMMAND:
            case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCLBUTTONDBLCLK:
            case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: break;
            default: return;
            }
            PreserveError preserve;
            Write write(WindowsMessage);
            if (auto* r = write.Get())
            {
                messageCalls.fetch_add(1); r->sourceClock = 2;
                r->sourceTime = static_cast<DWORD>(GetMessageTime());
                r->a = message; r->b = wparam; r->c = static_cast<std::uint64_t>(lparam);
                NativeSample(*r, static_cast<HWND>(window));
            }
        }
        inline void CALLBACK NativeEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object,
                                         LONG child, DWORD sourceThread, DWORD sourceTime) noexcept
        {
            if ((event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE) && object != OBJID_WINDOW) return;
            PreserveError preserve;
            Write write(WinEvent);
            if (auto* r = write.Get())
            {
                winEventCalls.fetch_add(1); r->sourceClock = 3;
                r->sourceTime = sourceTime; r->sourceThread = sourceThread;
                r->a = event; r->b = static_cast<std::uint64_t>(static_cast<std::int64_t>(object));
                r->c = static_cast<std::uint64_t>(static_cast<std::int64_t>(child));
                NativeSample(*r, window);
            }
        }
        inline bool Begin(SDL_Window* window) noexcept
        {
            if (begun.exchange(true)) return false;
            SDL_SysWMinfo native{}; SDL_VERSION(&native.version);
            if (!window || !SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_WINDOWS)
            { setupError = 1; return false; }
            sdlWindow = window; ownWindow = native.info.win.window;
            ownSdlWindow = SDL_GetWindowID(window); ownProcess = GetCurrentProcessId(); ownThread = GetCurrentThreadId();
            if (!QueryPerformanceFrequency(&frequency)) { setupError = 2; return false; }
            enabled.store(true);
            Mark(Started);
            ClockAnchor();
            watchRequested = true; SDL_AddEventWatch(Watch, nullptr);
            messageRequested = true; SDL_SetWindowsMessageHook(Message, nullptr);
            constexpr DWORD first[4]{EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS, EVENT_OBJECT_SHOW, EVENT_SYSTEM_MINIMIZESTART};
            constexpr DWORD last[4]{EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS, EVENT_OBJECT_HIDE, EVENT_SYSTEM_MINIMIZEEND};
            bool installed = true;
            for (unsigned i = 0; i != hooks.size(); ++i)
            {
                hooks[i] = SetWinEventHook(first[i], last[i], nullptr, NativeEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
                if (!hooks[i]) { hookErrors[i] = GetLastError(); installed = false; }
            }
            if (!installed) setupError = 3;
            return installed;
        }
        inline void JsonString(std::ostream& out, const char* value)
        {
            constexpr char hex[] = "0123456789abcdef";
            out << '"';
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value ? value : ""); *p; ++p)
            {
                if (*p == '"' || *p == '\\') out << '\\' << char(*p);
                else if (*p < 32) out << "\\u00" << hex[*p >> 4] << hex[*p & 15];
                else out << char(*p);
            }
            out << '"';
        }
        inline void Finish(const char* reason)
        {
            if (!begun.load() || finished.exchange(true)) return;
            ClockAnchor();
            Mark(Finished);
            enabled.store(false);
            const auto freezeQpc = Clock();
            const auto atFreeze = writers.load();
            if (watchRequested) SDL_DelEventWatch(Watch, nullptr);
            if (messageRequested) SDL_SetWindowsMessageHook(nullptr, nullptr);
            bool unhooked = true;
            for (unsigned i = 0; i != hooks.size(); ++i)
                if (hooks[i] && !UnhookWinEvent(hooks[i]))
                { unhookErrors[i] = GetLastError(); unhooked = false; }
            // Never wait for writers or pump pending native/SDL events. A producer
            // may still commit later; read each slot only if already published.
            const auto inFlight = writers.load();
            const auto attempted = next.load();
            const auto count = attempted < Capacity ? static_cast<unsigned>(attempted) : Capacity;
            unsigned gaps = 0, records = 0, frameGaps = 0;
            std::ofstream csv("focus-trace.csv", std::ios::out | std::ios::noreplace);
            csv << "ordinal,qpc_begin,qpc_end,kind,origin,source_clock,source_time,source_thread,caller_thread,updates,segment,stable,observed,frame_before,frame_after,own_hwnd,own_pid,own_tid,event_hwnd,event_pid,event_tid,foreground_hwnd,foreground_pid,foreground_tid,caller_focus_hwnd,caller_focus_pid,caller_focus_tid,foreground_gui_ok,foreground_gui_active,foreground_gui_focus,target_gui_ok,target_gui_active,target_gui_focus,sdl_window,keyboard_window,mouse_window,flags,relative,engine,valid,visible,foreground_self,thread_focus_self,lock_before,lock_after,lock_return,result,error,a,b,c,d\n";
            for (unsigned i = 0; i != count; ++i)
            {
                if (!slots[i].committed.load(std::memory_order_acquire)) { ++gaps; continue; }
                const auto& r = slots[i].record;
                ++records;
                if (r.frameBefore != r.frameAfter || (r.frameBefore & 1)) ++frameGaps;
                csv << r.ordinal << ',' << r.qpcBegin << ',' << r.qpcEnd << ',' << r.kind << ',' << r.origin << ','
                    << r.sourceClock << ',' << r.sourceTime << ',' << r.sourceThread << ',' << r.callerThread << ','
                    << r.updates << ',' << r.segment << ',' << r.stable << ',' << r.observed << ',' << r.frameBefore << ',' << r.frameAfter << ','
                    << r.ownWindow << ',' << r.ownProcess << ',' << r.ownThread << ',' << r.window << ',' << r.windowProcess << ',' << r.windowThread << ','
                    << r.foreground << ',' << r.foregroundProcess << ',' << r.foregroundThread << ',' << r.threadFocus << ',' << r.focusProcess << ',' << r.focusThread << ','
                    << r.foregroundGui << ',' << r.foregroundActive << ',' << r.foregroundFocus << ',' << r.targetGui << ',' << r.targetActive << ',' << r.targetFocus << ','
                    << r.sdlWindow << ',' << r.keyboardWindow << ',' << r.mouseWindow << ',' << r.flags << ',' << r.relative << ',' << r.engine << ','
                    << r.valid << ',' << r.visible << ',' << r.foregroundSelf << ',' << r.threadFocusSelf << ','
                    << r.lockBefore << ',' << r.lockAfter << ',' << r.lockReturn << ',' << r.result << ',' << r.error << ','
                    << r.a << ',' << r.b << ',' << r.c << ',' << r.d << '\n';
            }
            csv.flush(); const bool csvOk = bool(csv);
            const auto dropped = overflow.load();
            const auto sdlRecords = watchCalls.load(), messageRecords = messageCalls.load(), nativeRecords = winEventCalls.load();
            const bool owner = GetCurrentThreadId() == ownThread;
            const bool complete = !setupError && unhooked && owner && !atFreeze && !inFlight && !gaps && !frameGaps && !dropped &&
                                  !clockFailure.load() && csvOk && sdlRecords && messageRecords && nativeRecords;
            std::ofstream metadata("focus-trace-metadata.json", std::ios::out | std::ios::noreplace);
            metadata << "{\n  \"schema\": 1,\n  \"diagnostic_only\": true,\n  \"acceptance_eligible\": false,\n  \"reason\": ";
            JsonString(metadata, reason);
            metadata << ",\n  \"buffer_complete\": " << (complete ? "true" : "false")
                     << ",\n  \"native_history_complete\": false,\n  \"capacity\": " << Capacity
                     << ",\n  \"reserved\": " << attempted << ",\n  \"records\": " << records << ",\n  \"uncommitted_slots\": " << gaps
                     << ",\n  \"writers_at_freeze\": " << atFreeze << ",\n  \"writers_after_unhook\": " << inFlight
                     << ",\n  \"mixed_frame_contexts\": " << frameGaps << ",\n  \"overflow\": " << dropped
                     << ",\n  \"qpc_frequency\": " << frequency.QuadPart << ",\n  \"freeze_qpc\": " << freezeQpc
                     << ",\n  \"clock_failure\": " << (clockFailure.load() ? "true" : "false")
                     << ",\n  \"csv_write_ok\": " << (csvOk ? "true" : "false") << ",\n  \"finish_on_owner_thread\": " << (owner ? "true" : "false")
                     << ",\n  \"setup_error\": " << setupError << ",\n  \"sdl_watch_requested\": " << (watchRequested ? "true" : "false")
                     << ",\n  \"windows_message_hook_requested\": " << (messageRequested ? "true" : "false")
                     << ",\n  \"sdl_watch_records\": " << sdlRecords << ",\n  \"windows_message_records\": " << messageRecords
                     << ",\n  \"winevent_records\": " << nativeRecords << ",\n  \"hook_errors\": [";
            for (unsigned i = 0; i != hooks.size(); ++i) metadata << (i ? "," : "") << hookErrors[i];
            metadata << "],\n  \"unhook_errors\": [";
            for (unsigned i = 0; i != hooks.size(); ++i) metadata << (i ? "," : "") << unhookErrors[i];
            metadata << "],\n  \"kinds\": {\"1\":\"begin\",\"2\":\"SDL enqueue\",\"3\":\"SDL Windows message hook\",\"4\":\"WinEvent notification\",\"5\":\"exact native predicate\",\"6\":\"outer gate\",\"7\":\"finish\",\"8\":\"clock anchor\",\"11\":\"activation branch: a=1 already focused; a=2 caption sent; b=SendInput return\"},\n"
                     << "  \"schema_notes\": [\n"
                     << "    \"Ordinal is observer reservation order; QPC begin/end bounds observer execution. Source event time is separate and may precede delivery. No atomic global state or event order is implied.\",\n"
                     << "    \"source_clock: 0 unavailable; 1 SDL millisecond event timestamp; 2 GetMessageTime last retrieved message; 3 WinEvent source milliseconds. Only WinEvent supplies source_thread; zero elsewhere means unavailable. Millisecond counters may wrap.\",\n"
                     << "    \"kind8 anchors a=GetTickCount64,b=GetTickCount,c=SDL_GetTicks64,d=SDL_GetTicks within qpc_begin/end; result=1 only on owner thread. Map source millisecond clocks using both anchors, retaining read intervals, quantization and 32-bit wrap ambiguity. Do not assume common epochs or an exact simultaneous timestamp.\",\n"
                     << "    \"kind2 payload a=SDL type; window b=event,c=data1,d=data2; key b=scancode,c=symbol,d=state|repeat<<8|mod<<16; button b=button,c=x:y,d=state|clicks<<8; motion b=x:y,c=xrel:yrel,d=state; wheel b=x:y,c=direction. Signed pairs use high:low uint32 bits. Text contents are not captured.\",\n"
                     << "    \"kind3 a=message,b=wParam,c=lParam; only activation/focus/show/size/windowpos/system-command/caption-left-button/key messages are observed. No nonqueued-message completeness is claimed. kind4 a=event,b=signed object,c=signed child; show/hide limited to OBJID_WINDOW.\",\n"
                     << "    \"kind5 records actual native operands, cached lease before/after and raw LOCK return. -1 means skipped/unavailable. valid is handle-nonnull for kind5, IsWindow only for supplementary samples. error is GetLastError captured immediately only after LOCK returns zero; no extra lock or focus query is substituted.\",\n"
                     << "    \"kind6 a=native result, engine=-1 when short-circuited. result is the unchanged conjunction. Supplementary Sample records never substitute for exact gate operands.\",\n"
                     << "    \"origin is thread-local execution context, not native causal attribution. Origin20 spans NativeTap and event pumping. Direct push contexts21-26 require matching root112/113 payload records before attributing an SDL enqueue to fixture injection.\",\n"
                     << "    \"GetFocus is caller-thread local. HWND PID/TID lookups and foreground/target GetGUIThreadInfo fields are supplementary observations at callback/sample time, not historical or atomic facts. SDL getters occur only on the owner thread.\",\n"
                     << "    \"SDL watches observe enqueue, not dequeue/dispatch. Their return is ignored. SDL registration APIs return void; requested is not verified installation. Nonzero callback counts demonstrate activity, not complete coverage.\",\n"
                     << "    \"WinEvent OUTOFCONTEXT delivery may be asynchronous/reentrant. Freeze does not pump or wait for pending native events or writers; pending undelivered notifications remain unobservable. Gaps, overflow, missing observer activity, failed hooks or mixed frame snapshots make buffer_complete false.\",\n"
                     << "    \"Files are written once after freeze with create-only mode. Missing or unwritable metadata is incomplete evidence. No static/global destructor performs trace IO; explicit application teardown may finalize once before SDL/InputManager destruction.\"\n  ]\n}\n";
            metadata.flush();
        }
    }
#endif

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
        {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
            FocusTrace::Mark(FocusTrace::ActivationBranch, 1);
#endif
            return true;
        }
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
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        FocusTrace::Mark(FocusTrace::ActivationBranch, 2, sent,
                         static_cast<unsigned>(actual.x), static_cast<unsigned>(actual.y));
#endif
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_CAPTION_ACTIVATE x={} y={} sent={}",
                                   actual.x, actual.y, sent);
        return sent == 2;
    }

    inline bool AcquireNativeForeground()
    {
        const auto handle = nativeFixture.window;
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        FocusTrace::Write write(FocusTrace::NativePredicate);
        auto* trace = write.Get();
        if (trace)
        {
            trace->valid = handle != nullptr;
            trace->window = FocusTrace::Handle(handle);
            trace->lockBefore = nativeFixture.locked;
        }
        const auto finish = [&](bool result)
        {
            if (trace)
            {
                FocusTrace::PreserveError preserve;
                trace->result = result; trace->lockAfter = nativeFixture.locked;
                // Identity lookups follow all evaluated gate operands. They use
                // captured HWNDs and never repeat GetForegroundWindow/GetFocus.
                FocusTrace::Identity(handle, trace->window, trace->windowProcess, trace->windowThread);
                FocusTrace::Identity(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(trace->foreground)),
                    trace->foreground, trace->foregroundProcess, trace->foregroundThread);
                FocusTrace::Identity(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(trace->threadFocus)),
                    trace->threadFocus, trace->focusProcess, trace->focusThread);
            }
            return result;
        };
        // Keep Candidate04's native calls, short-circuit order and cached lease.
        // Skipped fields remain -1; the trace never manufactures an operand.
        if (!handle) return finish(false);
        const BOOL visible = IsWindowVisible(handle);
        if (trace) trace->visible = visible != 0;
        if (!visible) return finish(false);
        const HWND foreground = GetForegroundWindow();
        if (trace)
        { trace->foreground = FocusTrace::Handle(foreground); trace->foregroundSelf = foreground == handle; }
        if (foreground != handle) return finish(false);
        const HWND focus = GetFocus();
        if (trace)
        { trace->threadFocus = FocusTrace::Handle(focus); trace->threadFocusSelf = focus == handle; }
        if (focus != handle) return finish(false);
        if (!nativeFixture.locked)
        {
            const BOOL locked = LockSetForegroundWindow(LSFW_LOCK);
            const DWORD error = locked ? 0 : GetLastError();
            nativeFixture.locked = locked != 0;
            if (trace) { trace->lockReturn = locked; trace->error = error; }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_FOREGROUND locked={}",
                                       nativeFixture.locked);
        }
        return finish(nativeFixture.locked);
#else
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
#endif
    }

    inline bool NativePointVisible(glm::dvec2 logical)
    {
        POINT point{static_cast<LONG>(logical.x), static_cast<LONG>(logical.y)};
        return nativeFixture.window && ClientToScreen(nativeFixture.window, &point) &&
               GetAncestor(WindowFromPoint(point), GA_ROOT) == nativeFixture.window;
    }

#ifdef GENGINE_LIFECYCLE_ORACLE_EVIDENCE
    namespace OracleEvidence
    {
        struct Predicates
        {
            unsigned evaluatedMask = 0;
            std::array<int, 5> values{-1, -1, -1, -1, -1};
            bool Record(unsigned index, bool value) noexcept
            {
                evaluatedMask |= 1u << index;
                values[index] = value ? 1 : 0;
                return value;
            }
        };
        struct InteriorValues
        {
            Predicates conditions;
            std::array<glm::dvec2, 4> edgeStart{}, edgeEnd{};
            std::array<double, 4> edgeDistance{}, edgeParameter{};
            double margin = 0;
        };
        struct NativeValues
        {
            HWND own{}, hit{}, root{};
            POINT client{}, screen{};
            int windowPresent = -1, converted = -1, rootMatches = -1;
            DWORD conversionError = 0;
        };
        struct Evaluation
        {
            Predicates conditions;
            glm::dvec2 pixelDelta{};
            double pixelError = 0;
            InteriorValues interior;
            NativeValues native;
        };
        struct Frame
        {
            unsigned updates{}, segment{}, stable{}, observed{}, targetIndex{};
            std::uint64_t entityIndex{}, entityGeneration{}, entityRegistry{};
            float scale = 1;
            glm::uvec2 requestedNative{}, sampledNative{}, pickingExtent{}, convertedExtent{};
            glm::dvec2 requestedViewportWindow{};
        };
    }

    // The overload observes the same native predicate once, in its original order.
    // Identity and other supplemental Win32 queries are deferred until failure.
    inline bool NativePointVisible(glm::dvec2 logical, OracleEvidence::NativeValues& evidence)
    {
        POINT point{static_cast<LONG>(logical.x), static_cast<LONG>(logical.y)};
        evidence.client = point;
        evidence.own = nativeFixture.window;
        evidence.windowPresent = nativeFixture.window ? 1 : 0;
        if (!evidence.windowPresent) return false;
        const auto converted = ClientToScreen(nativeFixture.window, &point);
        if (!converted) evidence.conversionError = GetLastError();
        evidence.converted = converted ? 1 : 0;
        evidence.screen = point;
        if (!converted) return false;
        const auto hit = WindowFromPoint(point);
        const auto root = GetAncestor(hit, GA_ROOT);
        const bool matches = root == nativeFixture.window;
        evidence.hit = hit; evidence.root = root; evidence.rootMatches = matches ? 1 : 0;
        return matches;
    }
#endif

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
#ifdef GENGINE_LIFECYCLE_ORACLE_EVIDENCE
    inline bool Interior(const CameraSnapshotDesc& d, unsigned target, glm::dvec2 p,
                         OracleEvidence::InteriorValues& evidence)
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
            const double distance = glm::length(p - (a + t * v));
            margin = (std::min)(margin, distance);
            evidence.edgeStart[i] = a; evidence.edgeEnd[i] = b;
            evidence.edgeDistance[i] = distance; evidence.edgeParameter[i] = t;
        }
        evidence.margin = margin;
        return evidence.conditions.Record(0, margin >= 4) && evidence.conditions.Record(1, p.x >= 4) &&
               evidence.conditions.Record(2, p.y >= 4) && evidence.conditions.Record(3, p.x < d.viewport.pixelSize.x - 4) &&
               evidence.conditions.Record(4, p.y < d.viewport.pixelSize.y - 4);
    }

    namespace OracleEvidence
    {
        inline void Number(std::ostream& out, double value)
        {
            if (std::isfinite(value)) out << value;
            else out << "null";
        }
        template <glm::length_t N, typename T, glm::qualifier Q>
        void Vector(std::ostream& out, const glm::vec<N, T, Q>& value)
        {
            out << '[';
            for (glm::length_t i = 0; i != N; ++i)
            { if (i) out << ','; Number(out, double(value[i])); }
            out << ']';
        }
        inline void Point(std::ostream& out, POINT value)
        { out << '[' << value.x << ',' << value.y << ']'; }
        inline void String(std::ostream& out, std::string_view value)
        {
            // Camera error operations are fixed literals. Cap even unexpected input.
            out << '"';
            for (std::size_t i = 0; i != (std::min)(value.size(), std::size_t(128)); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(value[i]);
                if (c == '"' || c == '\\') out << '\\' << char(c);
                else if (c >= 32 && c < 127) out << char(c);
                else out << '?';
            }
            out << '"';
        }
        inline void Conditions(std::ostream& out, const Predicates& value)
        {
            out << "{\"evaluated_mask\":" << value.evaluatedMask << ",\"values\":[";
            for (unsigned i = 0; i != value.values.size(); ++i)
            { if (i) out << ','; out << value.values[i]; }
            out << "]}";
        }
        inline void Descriptor(std::ostream& out, const CameraSnapshotDesc& d)
        {
            out << "{\"projection\":" << int(d.projection) << ",\"position\":"; Vector(out, d.position);
            out << ",\"orientation_wxyz\":["; Number(out, d.orientation.w); out << ',';
            Number(out, d.orientation.x); out << ','; Number(out, d.orientation.y); out << ',';
            Number(out, d.orientation.z); out << "],\"vertical_fov_radians\":"; Number(out, d.verticalFovRadians);
            out << ",\"orthographic_height\":"; Number(out, d.orthographicHeight);
            out << ",\"near\":"; Number(out, d.nearPlane); out << ",\"far\":"; Number(out, d.farPlane);
            out << ",\"visible_layers\":" << d.visibleLayers << ",\"logical_origin\":"; Vector(out, d.viewport.logicalOrigin);
            out << ",\"logical_size\":"; Vector(out, d.viewport.logicalSize);
            out << ",\"pixel_origin\":"; Vector(out, d.viewport.pixelOrigin);
            out << ",\"pixel_size\":"; Vector(out, d.viewport.pixelSize); out << '}';
        }
        struct NativeIdentity
        {
            std::uint64_t window{};
            DWORD process{}, thread{};
        };
        inline NativeIdentity SampleIdentity(HWND window)
        {
            NativeIdentity result;
            result.window = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
            result.thread = window ? GetWindowThreadProcessId(window, &result.process) : 0;
            return result;
        }
        inline void Identity(std::ostream& out, const NativeIdentity& value)
        {
            out << "{\"hwnd\":" << value.window << ",\"pid\":" << value.process << ",\"tid\":" << value.thread << '}';
        }
        inline void Error(std::ostream& out, const CameraError& error)
        {
            out << "{\"code\":" << int(error.code) << ",\"operation\":";
            String(out, error.operation); out << '}';
        }
        inline bool Dump(const Frame& frame, const CameraSnapshot& snapshot, const CameraSnapshotDesc& reference,
                         glm::dvec3 world, glm::dvec2 expected, glm::dvec2 logical,
                         const std::expected<CameraPixelPoint, CameraError>& projected,
                         const std::expected<CameraRay, CameraError>& ray, const Evaluation& evidence) noexcept
        {
            // Owner-thread, one bounded record, only after the existing condition failed.
            // Explicit failure-path IO only; a write failure never replaces the gate failure.
            static bool attempted = false;
            if (attempted) return false;
            attempted = true;
            struct PreserveError { DWORD value = GetLastError(); ~PreserveError() { SetLastError(value); } } preserveError;
            try
            {
                // Sample identities before opening or writing the evidence file.
                const auto ownIdentity = SampleIdentity(nativeFixture.window);
                const auto hitIdentity = SampleIdentity(evidence.native.hit);
                const auto rootIdentity = SampleIdentity(evidence.native.root);
                LARGE_INTEGER qpc{}, frequency{};
                QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&frequency);
                RECT client{};
                const BOOL clientOk = nativeFixture.window ? GetClientRect(nativeFixture.window, &client) : FALSE;
                const DWORD clientError = !clientOk && nativeFixture.window ? GetLastError() : 0;
                const auto* viewport = ImGui::FindWindowByName("Viewport");
                std::ofstream out("lifecycle-oracle-failure.json", std::ios::out | std::ios::noreplace);
                if (!out) return false;
                out.imbue(std::locale::classic());
                out << std::setprecision(17)
                    << "{\n\"schema\":1,\"failure\":\"Projection/ray/interior/visibility oracle\","
                       "\"operand_order\":[\"projected_missing\",\"ray_missing\",\"pixel_error_gt_0.05\",\"interior_false\",\"native_visibility_false\"],"
                       "\"operand_values\":\"-1 skipped; 0 false; 1 true\",\"nonfinite_numbers\":\"null\","
                       "\"projection_and_ray_computations\":\"both executed before the short-circuit condition\","
                       "\"updates\":" << frame.updates << ",\"segment\":" << frame.segment << ",\"stable\":" << frame.stable
                    << ",\"observed\":" << frame.observed << ",\"target_index\":" << frame.targetIndex
                    << ",\"entity\":[" << frame.entityIndex << ',' << frame.entityGeneration << ',' << frame.entityRegistry
                    << "],\"scale\":"; Number(out, frame.scale);
                out << ",\"requested_native_logical\":"; Vector(out, frame.requestedNative);
                out << ",\"sampled_native_logical_before_condition\":"; Vector(out, frame.sampledNative);
                out << ",\"requested_viewport_window\":"; Vector(out, frame.requestedViewportWindow);
                out << ",\"picking_extent\":"; Vector(out, frame.pickingExtent);
                out << ",\"converted_logical_extent\":"; Vector(out, frame.convertedExtent);
                out << ",\"descriptor\":"; Descriptor(out, snapshot.Description());
                out << ",\"reference_descriptor\":"; Descriptor(out, reference);
                out << ",\"world_point\":"; Vector(out, world);
                out << ",\"expected_pixel\":"; Vector(out, expected);
                out << ",\"logical_point\":"; Vector(out, logical);
                out << ",\"condition\":"; Conditions(out, evidence.conditions);
                out << ",\"projected\":{\"success\":" << (projected ? "true" : "false");
                if (projected)
                {
                    out << ",\"pixel\":"; Vector(out, projected->value);
                    out << ",\"ndc_depth\":"; Number(out, projected->ndcDepth);
                    out << ",\"inside_frustum\":" << (projected->insideFrustum ? "true" : "false");
                }
                else { out << ",\"error\":"; Error(out, projected.error()); }
                out << "},\"ray\":{\"success\":" << (ray ? "true" : "false");
                if (ray)
                {
                    out << ",\"origin\":"; Vector(out, ray->origin);
                    out << ",\"direction\":"; Vector(out, ray->direction);
                    out << ",\"t_near\":"; Number(out, ray->tNear);
                    out << ",\"t_far\":"; Number(out, ray->tFar);
                }
                else { out << ",\"error\":"; Error(out, ray.error()); }
                out << "},\"pixel_comparison\":";
                if (evidence.conditions.evaluatedMask & (1u << 2))
                { out << "{\"delta\":"; Vector(out, evidence.pixelDelta); out << ",\"max_error\":"; Number(out, evidence.pixelError); out << '}'; }
                else out << "null";
                out << ",\"interior\":";
                if (evidence.conditions.evaluatedMask & (1u << 3))
                {
                    out << "{\"condition_order\":[\"margin_ge_4\",\"x_ge_4\",\"y_ge_4\",\"x_lt_width_minus_4\",\"y_lt_height_minus_4\"],\"conditions\":";
                    Conditions(out, evidence.interior.conditions);
                    out << ",\"margin\":"; Number(out, evidence.interior.margin); out << ",\"edges\":[";
                    for (unsigned i = 0; i != 4; ++i)
                    {
                        if (i) out << ',';
                        out << "{\"a\":"; Vector(out, evidence.interior.edgeStart[i]);
                        out << ",\"b\":"; Vector(out, evidence.interior.edgeEnd[i]);
                        out << ",\"parameter\":"; Number(out, evidence.interior.edgeParameter[i]);
                        out << ",\"distance\":"; Number(out, evidence.interior.edgeDistance[i]); out << '}';
                    }
                    out << "]}";
                }
                else out << "null";
                out << ",\"native_predicate\":";
                if (evidence.conditions.evaluatedMask & (1u << 4))
                {
                    out << "{\"window_present\":" << evidence.native.windowPresent
                        << ",\"client_to_screen\":" << evidence.native.converted
                        << ",\"conversion_error_on_failure\":" << evidence.native.conversionError
                        << ",\"root_matches\":" << evidence.native.rootMatches << ",\"client_point\":";
                    Point(out, evidence.native.client); out << ",\"screen_point\":";
                    if (evidence.native.converted == 1) Point(out, evidence.native.screen); else out << "null";
                    out << ",\"own_hwnd\":" << static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(evidence.native.own))
                        << ",\"hit_hwnd\":" << static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(evidence.native.hit))
                        << ",\"root_hwnd\":" << static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(evidence.native.root)) << '}';
                }
                else out << "null";
                out << ",\"supplemental_after_failure\":{\"note\":\"not atomic with the predicate; identity lookup may observe retirement or handle reuse\","
                    << "\"qpc\":" << qpc.QuadPart << ",\"qpc_frequency\":" << frequency.QuadPart
                    << ",\"caller_tid\":" << GetCurrentThreadId() << ",\"own_identity\":";
                Identity(out, ownIdentity);
                out << ",\"hit_identity\":"; Identity(out, hitIdentity);
                out << ",\"root_identity\":"; Identity(out, rootIdentity);
                out << ",\"get_client_rect\":" << (clientOk ? "true" : "false") << ",\"client_rect_error\":" << clientError
                    << ",\"client_rect\":[" << client.left << ',' << client.top << ',' << client.right << ',' << client.bottom
                    << "],\"imgui_viewport\":";
                if (viewport)
                {
                    const auto vec = [&](ImVec2 v) { Vector(out, glm::dvec2(v.x, v.y)); };
                    out << "{\"position\":"; vec(viewport->Pos); out << ",\"size\":"; vec(viewport->Size);
                    out << ",\"size_full\":"; vec(viewport->SizeFull);
                    out << ",\"content_size_previous_frame\":"; vec(viewport->ContentSize);
                    out << ",\"content_region_min\":"; vec(viewport->ContentRegionRect.Min);
                    out << ",\"content_region_max\":"; vec(viewport->ContentRegionRect.Max);
                    out << ",\"scroll\":"; vec(viewport->Scroll);
                    out << ",\"scrollbar_sizes\":"; vec(viewport->ScrollbarSizes); out << '}';
                }
                else out << "null";
                out << "}}\n";
                out.flush();
                return bool(out);
            }
            catch (...) { return false; }
        }
    }
#endif
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
