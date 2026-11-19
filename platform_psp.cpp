#include "app.hpp"
#include "screen.hpp"
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspkernel.h>
#include <pspnet_apctl.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspsdk.h>
#include <psputility.h>
#include <algorithm>

PSP_MODULE_INFO("Lichess Handheld",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);

namespace {
uint32_t* pixels=nullptr;
SceUID workers[2]={-1,-1};
struct Work { void (*entry)(void*); void* arg; } work[2];
std::atomic<bool> exiting{false}, resumed{false};
bool netStarted=false;
int profile=0;
uint64_t nextAttempt=0;
int workEntry(SceSize,void* arg) { auto& job=**static_cast<Work**>(arg); job.entry(job.arg); return 0; }
int exitCallback(int,int,void*) { exiting.store(true); return 0; }
int powerCallback(int,int flags,void*) {
    if (flags&PSP_POWER_CB_RESUME_COMPLETE) resumed.store(true);
    return 0;
}
int callbackThread(SceSize,void*) {
    const auto exit=sceKernelCreateCallback("Chess exit",exitCallback,nullptr);
    if (exit>=0) sceKernelRegisterExitCallback(exit);
    const auto power=sceKernelCreateCallback("Chess power",powerCallback,nullptr);
    if (power>=0) scePowerRegisterCallback(-1,power);
    while (!exiting.load()) sceKernelSleepThreadCB();
    return 0;
}
}
namespace chess {
bool wifiReady(uint64_t now) {
    if (!netStarted && now>=nextAttempt) {
        nextAttempt=now+3000;
        if (sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON)<0) return false;
        if (sceUtilityLoadNetModule(PSP_NET_MODULE_INET)<0) {
            sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
            return false;
        }
        if (pspSdkInetInit()!=0) {
            sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
            sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
            return false;
        }
        netStarted=true;
        for (int n=1;n<=99;++n) {
            if (sceUtilityCheckNetParam(n)==0) { profile=n; break; }
        }
        nextAttempt=0;
    }
    int state=0;
    if (!netStarted || !profile || sceNetApctlGetState(&state)<0) return false;
    if (state==4) return true;
    if (!state && now>=nextAttempt) {
        sceNetApctlConnect(profile);
        nextAttempt=now+3000;
    }
    return false;
}
std::string wifiAddress() {
    union SceNetApctlInfo info{};
    return sceNetApctlGetInfo(8,&info)==0?info.ip:"";
}
uint64_t sessionSeed() {
    uint64_t tick=0;
    sceRtcGetCurrentTick(&tick);
    return tick ^ (sceKernelGetSystemTimeWide()<<23);
}
}
namespace handheld {
uint64_t milliseconds() { return sceKernelGetSystemTimeWide()/1000; }
bool launchWorker(void (*entry)(void*),void* arg,unsigned slot) {
    if (slot>=2 || workers[slot]>=0) return false;
    auto& worker=workers[slot];
    work[slot]={entry,arg};
    worker=sceKernelCreateThread("Chess search",workEntry,0x24,64*1024,PSP_THREAD_ATTR_USER,nullptr);
    if (worker<0) return false;
    auto* job=&work[slot];
    if (sceKernelStartThread(worker,sizeof(job),&job)>=0) return true;
    sceKernelDeleteThread(worker);
    worker=-1;
    return false;
}
void joinWorker(unsigned slot) {
    if (slot>=2) return;
    auto& worker=workers[slot];
    if (worker<0) return;
    sceKernelWaitThreadEnd(worker,nullptr);
    sceKernelDeleteThread(worker);
    worker=-1;
}
void rect(int x,int y,int w,int h,Ink ink) {
    const int right=std::min(480,x+w), bottom=std::min(272,y+h);
    x=std::max(0,x); y=std::max(0,y);
    if (right<=x) return;
    for (;y<bottom;++y) std::fill(pixels+y*512+x,pixels+y*512+right,ink);
}
void sprite(int x,int y,const Sprite& s,Ink tint) {
    blitSprite(pixels,512,480,272,x,y,s,tint);
}
}
int main() {
    using namespace handheld;
    const auto callbacks=sceKernelCreateThread("Chess callbacks",callbackThread,0x11,4096,
        PSP_THREAD_ATTR_USER,nullptr);
    if (callbacks>=0) sceKernelStartThread(callbacks,0,nullptr);
    sceDisplaySetMode(0,480,272);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    const auto vram=reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
    unsigned frame=0, previous=0;
    uint64_t repeatAt=0;
    {
        App app("ms0:/PSP/GAME/HandheldChess/lichess-token.txt","ms0:/PSP/GAME/HandheldChess/cacert.pem",
                "ms0:/PSP/GAME/HandheldChess/appearance.txt");
        while (!exiting.load() && !app.quit) {
            if (resumed.exchange(false)) app.onResume();
            SceCtrlData pad{};
            sceCtrlPeekBufferPositive(&pad,1);
            const auto now=milliseconds();
            const unsigned down=pad.Buttons&~previous;
            previous=pad.Buttons;
            constexpr unsigned directions=PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT;
            unsigned repeat=down;
            if (down&directions) repeatAt=now+230;
            else if ((pad.Buttons&directions) && now>=repeatAt) {
                repeat|=pad.Buttons&directions;
                repeatAt=now+70;
            }
            Input input;
            if (repeat&PSP_CTRL_UP) input.keys|=Up;
            if (repeat&PSP_CTRL_DOWN) input.keys|=Down;
            if (repeat&PSP_CTRL_LEFT) input.keys|=Left;
            if (repeat&PSP_CTRL_RIGHT) input.keys|=Right;
            if (down&PSP_CTRL_CROSS) input.keys|=Confirm;
            if (down&PSP_CTRL_SQUARE) input.keys|=Back;
            if (down&PSP_CTRL_CIRCLE) input.keys|=Chat;
            if (down&PSP_CTRL_SELECT) input.keys|=AppearanceKey;
            if (repeat&PSP_CTRL_LTRIGGER) input.keys|=HistoryPrev;
            if (repeat&PSP_CTRL_RTRIGGER) input.keys|=HistoryNext;
            if (down&(PSP_CTRL_START|PSP_CTRL_TRIANGLE)) input.keys|=Menu;
            if (pad.Buttons&PSP_CTRL_START) input.keys|=Fast;
            app.update(input,false);
            const auto target=vram+frame*512*272*sizeof(uint32_t);
            pixels=reinterpret_cast<uint32_t*>(target|0x40000000u);
            app.draw(false,false);
            sceDisplayWaitVblankStart();
            sceDisplaySetFrameBuf(reinterpret_cast<void*>(target),512,PSP_DISPLAY_PIXEL_FORMAT_8888,
                PSP_DISPLAY_SETBUF_IMMEDIATE);
            frame^=1;
        }
    }
    if (netStarted) {
        sceNetApctlDisconnect();
        pspSdkInetTerm();
        sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
        sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    }
    sceKernelExitGame();
    return 0;
}
