#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace GEngine
{
    class Window;
    // Optional synchronous observer on the initialization owner thread. It owns no
    // window, worker, resource or lifecycle. Normal/qualification runs have no hook.
    namespace InitializationProgress
    {
        enum class Stage {Renderer,Physics,Scene,Editor};
        struct Cancelled {};
        struct State
        {
            Window* window{};
            Stage stage{};
            std::array<char,256> detail{};
            std::uint64_t completed{},total{};
            unsigned finished{};
            bool cancelled{};
        };
        inline thread_local State state;
        inline thread_local void* context{};
        inline thread_local void (*observer)(void*,const State&){};
        inline thread_local std::chrono::steady_clock::time_point last;
        inline void Pulse(bool force=false)
        {
            if(!observer)return;
            const auto now=std::chrono::steady_clock::now();
            if(force || now-last>=std::chrono::milliseconds(33))
            {last=now;observer(context,state);}
            if(state.cancelled)throw Cancelled{};
        }
        inline void Report(Stage stage,std::string_view detail,std::uint64_t completed=0,std::uint64_t total=0)
        {
            if(!observer)return;
            const bool changed=stage!=state.stage || detail!=std::string_view(state.detail.data());
            state.stage=stage;state.completed=completed;state.total=total;
            const auto n=std::min(detail.size(),state.detail.size()-1);
            std::copy_n(detail.data(),n,state.detail.data());state.detail[n]=0;Pulse(changed);
        }
        inline void Complete(Stage stage){if(observer)state.finished|=1u<<unsigned(stage);}
        inline void Attach(Window* window){if(observer){state.window=window;Report(Stage::Renderer,"Initializing OpenGL renderer");}}
        struct Scope
        {
            Scope(void* user,void(*callback)(void*,const State&),bool enabled)
            {if(enabled){state={};context=user;observer=callback;last={};}}
            ~Scope(){observer=nullptr;context=nullptr;state.window=nullptr;}
        };
    }
}
