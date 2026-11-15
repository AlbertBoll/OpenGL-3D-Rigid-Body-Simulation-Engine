#pragma once
#include "Core/InitializationProgress.h"
#include "Windows/SDLWindow.h"
#include "RbsEditorWidgets.h"
#include "RbsStartup.h"
#include <sdl2/SDL.h>
#include <glad/glad.h>
#include <chrono>
#include <cstdio>

namespace Rbs::Editor
{
    // One presentation observer for the existing synchronous initialization path.
    // It cannot dispatch application edits or step physics while construction runs.
    class LoadingScreen
    {
        using Clock=std::chrono::steady_clock;
        Clock::time_point start=Clock::now(),previous=start;
        double maxGap{};
        unsigned frames{};
        int lastStage=-1;
    public:
        bool fontsInitialized{},cancelled{};
        static void Observe(void* user,const GEngine::InitializationProgress::State& state)
        {static_cast<LoadingScreen*>(user)->Draw(state);}
        void Draw(const GEngine::InitializationProgress::State& state)
        {
            using namespace GEngine;
            if(!state.window)return;
            auto* window=static_cast<SDLWindow*>(state.window);
            SDL_Event event;
            while(SDL_PollEvent(&event))
            {
                window->GetImGuiWindow()->HandleSDLEvent(event);
                if(event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE))
                {cancelled=true;InitializationProgress::state.cancelled=true;return;}
                if(event.type==SDL_WINDOWEVENT)window->RefreshDimensions();
            }
            if(!window->BeginRender()){InitializationProgress::state.cancelled=true;return;}
            if(!fontsInitialized){Initialize();fontsInitialized=true;}
            const auto now=Clock::now();const double elapsed=std::chrono::duration<double>(now-start).count();
            maxGap=std::max(maxGap,std::chrono::duration<double>(now-previous).count());previous=now;++frames;
            if(lastStage!=int(state.stage))
            {
                (void)TryPrintDiagnosticLine(stdout,"RBS_LOADING stage={} elapsed_ms={:.3f} detail={}",
                    unsigned(state.stage),elapsed*1000,state.detail.data());
                std::fflush(stdout);lastStage=int(state.stage);
            }
            GLint framebuffer{},viewport[4];glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
            window->RefreshDimensions();const auto pixels=window->GetFramebufferPixelSize();
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,pixels.Width,pixels.Height);
            glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glStencilMask(0xFF);
            glClearColor(20.f/255,26.f/255,32.f/255,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            if(!window->BeginUI()){InitializationProgress::state.cancelled=true;return;}
            BeginFrame();
            auto* view=ImGui::GetMainViewport();auto* draw=ImGui::GetForegroundDrawList(view);
            const auto size=view->Size,origin=view->Pos;
            draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,26,32,255));
            const float width=std::max(Px(220),std::min(Px(690),size.x-Px(72)));
            const float left=origin.x+(size.x-width)*.5f,top=origin.y+size.y*.5f-Px(126);
            const float identityWidth=Px(228),identityLeft=origin.x+(size.x-identityWidth)*.5f;
            const ImVec2 tile{identityLeft,top};
            draw->AddRectFilled(tile,{tile.x+Px(48),tile.y+Px(48)},IM_COL32(49,124,194,255),Px(10));
            // Simple geometric GEngine mark, drawn once per frame; no parsing or GPU asset churn.
            const ImVec2 a{tile.x+Px(24),tile.y+Px(10)},b{tile.x+Px(37),tile.y+Px(17)},c{tile.x+Px(24),tile.y+Px(24)},d{tile.x+Px(11),tile.y+Px(17)};
            const auto white=IM_COL32(221,237,252,255);
            const ImVec2 ring[]{a,b,c,d};
            for(int i=0;i<4;++i)draw->AddLine(ring[i],ring[(i+1)%4],white,Px(1.5f));
            for(auto p:{b,c,d})draw->AddLine(p,{p.x,p.y+Px(15)},white,Px(1.5f));
            draw->AddLine({d.x,d.y+Px(15)},{c.x,c.y+Px(15)},white,Px(1.5f));draw->AddLine({c.x,c.y+Px(15)},{b.x,b.y+Px(15)},white,Px(1.5f));
            draw->AddText(Bold[3],Px(30),{tile.x+Px(64),top-Px(2)},IM_COL32(230,237,246,255),"GEngine");
            draw->AddText({tile.x+Px(64),top+Px(34)},IM_COL32(145,159,177,255),"Initializing Editor");
            const float y=top+Px(100);
            draw->AddText({left,y},IM_COL32(205,216,229,255),state.detail.data());
            char units[64]{};
            if(state.total)
            {
                std::snprintf(units,sizeof(units),"%llu / %llu bodies",static_cast<unsigned long long>(state.completed),static_cast<unsigned long long>(state.total));
                draw->AddText({left+width-ImGui::CalcTextSize(units).x,y},IM_COL32(141,165,190,255),units);
            }
            const ImVec2 bar{left,y+Px(30)};const float h=Px(4);
            draw->AddRectFilled(bar,{bar.x+width,bar.y+h},IM_COL32(43,53,66,255),h*.5f);
            if(state.total)draw->AddRectFilled(bar,{bar.x+width*std::min(1.f,float(state.completed)/float(state.total)),bar.y+h},IM_COL32(70,151,229,255),h*.5f);
            else
            {
                const float segment=width*.22f,offset=float(std::fmod(elapsed*.45,1.0))*(width+segment)-segment;
                draw->PushClipRect(bar,{bar.x+width,bar.y+h},true);
                draw->AddRectFilled({bar.x+offset,bar.y},{bar.x+offset+segment,bar.y+h},IM_COL32(70,151,229,255),h*.5f);draw->PopClipRect();
            }
            draw->AddText({left,y+Px(46)},IM_COL32(125,143,163,255),state.total?"Processing authored body requests":"Preparing resources; duration depends on the active stage");
            constexpr const char* names[]{"Renderer","Physics","Scene","Editor"};
            const float stageWidth=std::min(width,Px(480)),stageLeft=left+(width-stageWidth)*.5f;
            for(int i=0;i<4;++i)
            {
                const bool done=(state.finished&(1u<<i))!=0,active=int(state.stage)==i;
                const ImVec2 center{stageLeft+stageWidth*(float(i)+.1f)/4.f,y+Px(99)};
                const auto color=done?IM_COL32(98,192,164,255):active?IM_COL32(84,160,234,255):IM_COL32(82,99,119,255);
                draw->AddCircle(center,Px(5),color,16,Px(1.4f));
                if(done){draw->AddLine({center.x-Px(2),center.y},{center.x,center.y+Px(2)},color,Px(1.3f));draw->AddLine({center.x,center.y+Px(2)},{center.x+Px(3),center.y-Px(2)},color,Px(1.3f));}
                draw->AddText({center.x+Px(13),center.y-ImGui::GetFontSize()*.5f},done||active?IM_COL32(172,190,210,255):IM_COL32(105,123,145,255),names[i]);
            }
            EndFrame();if(!window->EndUI()){InitializationProgress::state.cancelled=true;return;}
            window->SwapBuffer();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        }
        void Finish()
        {
            (void)TryPrintDiagnosticLine(stdout,"RBS_LOADING_COMPLETE elapsed_ms={:.3f} frames={} max_pump_gap_ms={:.3f}",
                std::chrono::duration<double,std::milli>(Clock::now()-start).count(),frames,maxGap*1000);
            std::fflush(stdout);
        }
    };
}
