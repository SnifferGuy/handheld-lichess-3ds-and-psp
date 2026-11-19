#pragma once
#include <cstdint>
#include <string_view>
namespace handheld {
using Ink=uint32_t;
constexpr Ink rgba(unsigned r,unsigned g,unsigned b) { return r | g<<8 | b<<16 | 0xff000000u; }
struct Sprite { int16_t x,y,w,h,advance,dx,dy; };
void rect(int x,int y,int w,int h,Ink);
void sprite(int x,int y,const Sprite&,Ink tint=0);
void blitSprite(Ink* target,int stride,int width,int height,int x,int y,const Sprite&,Ink tint);
void roundedRect(int x,int y,int w,int h,Ink);
void setTextRight(int right);
int textWidth(std::string_view,int scale=1);
void text(int x,int y,std::string_view,Ink,int scale=1);
void piece(int x,int y,int tile,int type);
}
