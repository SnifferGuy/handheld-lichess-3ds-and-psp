#include "screen.hpp"
#include "screen_atlas.hpp"
#include <algorithm>
#include <cstdlib>

namespace handheld {
namespace {
int textRight=480;
const Sprite& nextGlyph(std::string_view& input,int scale) {
    const auto ch=static_cast<unsigned char>(input.front());
    input.remove_prefix(1);
    if (ch>=128) {
        // The atlas has printable ASCII. Consume one UTF-8 sequence per fallback glyph.
        int remaining=ch>=0xf0 && ch<=0xf4?3:ch>=0xe0 && ch<=0xef?2:ch>=0xc2 && ch<=0xdf?1:0;
        while (remaining-- && !input.empty() && (static_cast<unsigned char>(input.front())&0xc0)==0x80)
            input.remove_prefix(1);
    }
    return art::fonts[std::clamp(scale,1,4)-1][(ch>=32 && ch<127?ch:'?')-32];
}
}
void setTextRight(int right) { textRight=right; }
int textWidth(std::string_view input,int scale) {
    int width=0;
    while (!input.empty()) width+=nextGlyph(input,scale).advance;
    return width;
}
void text(int x,int y,std::string_view input,Ink ink,int scale) {
    while (!input.empty()) {
        const auto& glyph=nextGlyph(input,scale);
        if (x+std::max<int>(glyph.advance,glyph.dx+glyph.w)>textRight) return;
        if (glyph.w && glyph.h) sprite(x+glyph.dx,y+glyph.dy,glyph,ink);
        x+=glyph.advance;
    }
}
void roundedRect(int x,int y,int w,int h,Ink ink) {
    rect(x+2,y,w-4,1,ink);
    rect(x+1,y+1,w-2,1,ink);
    rect(x,y+2,w,h-4,ink);
    rect(x+1,y+h-2,w-2,1,ink);
    rect(x+2,y+h-1,w-4,1,ink);
}
void piece(int x,int y,int tile,int type) {
    if (!type) return;
    const auto& artPiece=art::pieces[tile==32][(type<0?6:0)+std::abs(type)-1];
    sprite(x+artPiece.dx,y+artPiece.dy,artPiece);
}
#ifndef __3DS__
void blitSprite(Ink* target,int stride,int width,int height,int x,int y,const Sprite& s,Ink tint) {
    for (int row=std::max(0,-y);row<s.h && y+row<height;++row) {
        const auto* source=art::pixels+4*((s.y+row)*art::width+s.x);
        for (int col=std::max(0,-x);col<s.w && x+col<width;++col) {
            const auto* pixel=source+4*col;
            const unsigned alpha=pixel[3];
            if (!alpha) continue;
            auto& destination=target[(y+row)*stride+x+col];
            Ink output=0xff000000u;
            for (unsigned channel=0;channel<3;++channel) {
                const unsigned shift=channel*8;
                const unsigned from=tint?(tint>>shift)&255:pixel[channel];
                const unsigned to=(destination>>shift)&255;
                output|=((from*alpha+to*(255-alpha)+127)/255)<<shift;
            }
            destination=output;
        }
    }
}
#endif
}