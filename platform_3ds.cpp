#include "app.hpp"
#include "screen.hpp"
#include "screen_atlas.hpp"
#include <3ds.h>
#include <citro2d.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <cstdlib>

namespace {
Thread workers[2]={nullptr,nullptr};
u32* socketBuffer=nullptr;
bool socketStarted=false, resumed=false;
uint64_t nextSocketInit=0;
void wake(APT_HookType hook,void*) {
    if (hook==APTHOOK_ONWAKEUP || hook==APTHOOK_ONRESTORE) resumed=true;
}
}
namespace chess {
bool wifiReady(uint64_t now) {
    if (!socketStarted && now>=nextSocketInit) {
        nextSocketInit=now+1000;
        if (!socketBuffer) socketBuffer=static_cast<u32*>(memalign(0x1000,0x100000));
        if (socketBuffer && R_SUCCEEDED(socInit(socketBuffer,0x100000))) socketStarted=true;
    }
    if (!socketStarted) return false;
    const auto ip=uint32_t(gethostid());
    return ip!=0 && ip!=UINT32_MAX && osGetWifiStrength()!=0;
}
std::string wifiAddress() {
    in_addr ip{};
    ip.s_addr=gethostid();
    const auto* s=inet_ntoa(ip);
    return s?s:"";
}
uint64_t sessionSeed() {
    uint64_t seed=0;
    if (R_SUCCEEDED(psInit())) {
        const auto result=PS_GenerateRandomBytes(&seed,sizeof(seed));
        psExit();
        if (R_SUCCEEDED(result) && seed) return seed;
    }
    return svcGetSystemTick() ^ (osGetTime()<<17);
}
}
namespace handheld {
uint64_t milliseconds() { return svcGetSystemTick()/268123; }
bool launchWorker(void (*entry)(void*),void* arg,unsigned slot) {
    if (slot>=2 || workers[slot]) return false;
    workers[slot]=threadCreate(entry,arg,64*1024,0x32,-2,false);
    return workers[slot]!=nullptr;
}
void joinWorker(unsigned slot) {
    if (slot>=2) return;
    auto& worker=workers[slot];
    if (!worker) return;
    threadJoin(worker,UINT64_MAX);
    threadFree(worker);
    worker=nullptr;
}
void rect(int x,int y,int w,int h,Ink ink) { C2D_DrawRectSolid(x,y,0,w,h,ink); }
static C3D_Tex atlas;
void sprite(int x,int y,const Sprite& s,Ink tint) {
    const Tex3DS_SubTexture uv{
        u16(s.w),u16(s.h),float(s.x)/art::width,1.0f-float(s.y)/art::height,
        float(s.x+s.w)/art::width,1.0f-float(s.y+s.h)/art::height
    };
    C2D_ImageTint colors;
    if (tint) C2D_PlainImageTint(&colors,tint,1.0f);
    C2D_DrawImageAt({&atlas,&uv},x,y,0,tint?&colors:nullptr);
}
}
int main() {
    using namespace handheld;
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE*2)) { gfxExit(); return 1; }
    if (!C2D_Init(8192)) { C3D_Fini(); gfxExit(); return 1; }
    C2D_Prepare();
    auto sheet=Tex3DS_TextureImport(art::texture,sizeof(art::texture),&atlas,nullptr,false);
    if (!sheet) { C2D_Fini(); C3D_Fini(); gfxExit(); return 1; }
    C3D_TexSetFilter(&atlas,GPU_NEAREST,GPU_NEAREST);
    auto* top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    auto* bottom=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    aptHookCookie hook{};
    aptHook(&hook,wake,nullptr);
    if (top && bottom) {
        App app("sdmc:/3ds/HandheldChess/lichess-token.txt","sdmc:/3ds/HandheldChess/cacert.pem",
                "sdmc:/3ds/HandheldChess/appearance.txt");
        while (aptMainLoop() && !app.quit) {
            if (resumed) {
                app.onResume();
                resumed=false;
            }
            hidScanInput();
            const auto down=hidKeysDown(), repeat=hidKeysDownRepeat() | down;
            Input input;
            if (repeat&KEY_UP) input.keys|=Up;
            if (repeat&KEY_DOWN) input.keys|=Down;
            if (repeat&KEY_LEFT) input.keys|=Left;
            if (repeat&KEY_RIGHT) input.keys|=Right;
            if (down&KEY_A) input.keys|=Confirm;
            if (down&KEY_B) input.keys|=Back;
            if (down&KEY_START) input.keys|=Menu;
            if (down&KEY_SELECT) input.keys|=AppearanceKey;
            if (down&KEY_X) input.keys|=Chat;
            if (repeat&KEY_L) input.keys|=HistoryPrev;
            if (repeat&KEY_R) input.keys|=HistoryNext;
            if (hidKeysHeld()&KEY_START) input.keys|=Fast;
            if (down&KEY_TOUCH) {
                touchPosition touch{};
                hidTouchRead(&touch);
                input.x=touch.px; input.y=touch.py;
            }
            app.update(input,true);
            if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
                C2D_SceneBegin(top);
                app.draw(true,true);
                C2D_SceneBegin(bottom);
                app.draw(true,false);
                C3D_FrameEnd(0);
            }
        }
    }
    aptUnhook(&hook);
    if (socketStarted) socExit();
    std::free(socketBuffer);
    Tex3DS_TextureFree(sheet);
    C3D_TexDelete(&atlas);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
