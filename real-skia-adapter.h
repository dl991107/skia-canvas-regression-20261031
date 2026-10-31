#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "include/core/SkColorSpace.h"
#include "include/core/SkPixmap.h"
#include "fury_flat.h"
#define UNSAFE_BUFFERS(...) __VA_ARGS__
using wtf_size_t=uint32_t;
template<class T> using Vector=std::vector<T>;
inline uint64_t noise_seed=424242;
inline bool noise_enabled=true;
inline int read_calls=0,fail_read_at=0;
// Only injected failures use this wrapper; every other conversion calls real Skia.
struct FaultPixmap:SkPixmap {
 using SkPixmap::SkPixmap;
 bool readPixels(const SkImageInfo& info,void* pixels,size_t stride,int x,int y)const {
  if(++read_calls==fail_read_at)return false;
  return SkPixmap::readPixels(info,pixels,stride,x,y);
 }
};
#define SkPixmap FaultPixmap
namespace fury {inline bool GetCanvasNoiseSeed(uint64_t* seed){*seed=noise_seed;return noise_enabled;}}
namespace blink {
using String=std::string;
inline bool FuryWantsCanvasNoise(){return true;}
struct ImageDataBuffer {
 SkPixmap pixmap_;bool is_valid_=true;Vector<uint8_t> fury_noised_pixels_;
 explicit ImageDataBuffer(SkPixmap pixmap):pixmap_(pixmap){}
#include "declaration.inc"
};
String Export(ImageDataBuffer*);
}
using namespace blink;
#define CHECK(value) do{if(!(value)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#value<<'\n';return 1;}}while(false)
std::array<uint8_t,4> pixel(const SkPixmap& image,int x,int y){
 const auto* p=static_cast<const uint8_t*>(image.addr())+y*image.rowBytes()+x*4;
 return image.colorType()==kBGRA_8888_SkColorType?std::array<uint8_t,4>{p[2],p[1],p[0],p[3]}:std::array<uint8_t,4>{p[0],p[1],p[2],p[3]};
}
int main(){
 constexpr int W=280,H=80;Vector<uint8_t> white(W*H*4);size_t at=(12*W+177)*4;
 white[at]=white[at+1]=white[at+2]=255;white[at+3]=2;
 auto info=[](int w,int h,SkColorType c,SkAlphaType a){return SkImageInfo::Make(w,h,c,a,SkColorSpace::MakeSRGB());};
 ImageDataBuffer literal({info(W,H,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),white.data(),W*4});
 CHECK(Export(&literal)=="encoded");auto observed=pixel(literal.pixmap_,177,12);
 std::cout<<"literal_pixel "<<unsigned(observed[0])<<','<<unsigned(observed[1])<<','<<unsigned(observed[2])<<','<<unsigned(observed[3])<<'\n';
 CHECK((observed==std::array<uint8_t,4>{255,255,255,1}));
 size_t roundtrips=0;
 for(unsigned a=1;a<=255;++a)for(unsigned c=0;c<=a;++c){
  uint8_t src[4]={static_cast<uint8_t>(c),0,0,static_cast<uint8_t>(a)},straight[4]={},back[4]={};
  auto pm=info(1,1,kRGBA_8888_SkColorType,kPremul_SkAlphaType),up=pm.makeAlphaType(kUnpremul_SkAlphaType);
  CHECK(SkPixmap(pm,src,4).readPixels(up,straight,4,0,0));CHECK(SkPixmap(up,straight,4).readPixels(pm,back,4,0,0));CHECK(src[0]==back[0]);++roundtrips;
 }
 Vector<uint8_t> first_seed;
 for(auto color:{kRGBA_8888_SkColorType,kBGRA_8888_SkColorType})for(uint64_t seed:{424242,135791}){
  constexpr int w=32,h=32;constexpr size_t row=w*4+8;noise_seed=seed;
  Vector<uint8_t> backing(row*h,0xA5),straight(row*h),expected(row*h);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){size_t p=y*row+x*4;unsigned a=(x*37+y*17)%256;
   backing[p]=a?x%(a+1):0;backing[p+1]=a?y%(a+1):0;backing[p+2]=a?(x+y)%(a+1):0;backing[p+3]=a;
  }
  auto pm=info(w,h,color,kPremul_SkAlphaType),up=pm.makeAlphaType(kUnpremul_SkAlphaType);
  CHECK(SkPixmap(pm,backing.data(),row).readPixels(up,straight.data(),row,0,0));auto saved=straight;
  expected=backing;fury_flat::ApplyEdgeNoise(seed,base::span(backing),base::span(expected),row,0,0,w,h,0,0,w,h,w,h,color==kBGRA_8888_SkColorType);
  Vector<uint8_t> reference(row*h);CHECK(SkPixmap(pm,expected.data(),row).readPixels(up,reference.data(),row,0,0));
  ImageDataBuffer buffer({up,straight.data(),row});CHECK(Export(&buffer)=="encoded");CHECK(straight==saved);
  CHECK(buffer.pixmap_.alphaType()==up.alphaType()&&buffer.pixmap_.colorSpace()==up.colorSpace()&&buffer.pixmap_.rowBytes()==row);
  SkPixmap view(up,reference.data(),row);ImageDataBuffer repeat({up,straight.data(),row});CHECK(Export(&repeat)=="encoded");
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){CHECK(pixel(buffer.pixmap_,x,y)==pixel(view,x,y));CHECK(pixel(buffer.pixmap_,x,y)==pixel(repeat.pixmap_,x,y));}
  if(color==kRGBA_8888_SkColorType){if(seed==424242)first_seed=reference;else CHECK(first_seed!=reference);}
 }
 noise_seed=424242;uint8_t data[16]={255,255,255,2};auto valid=info(1,1,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType);
 for(int failure:{1,2}){ImageDataBuffer b({valid,data,4});read_calls=0;fail_read_at=failure;CHECK(Export(&b)=="data:,");CHECK(b.pixmap_.addr()==data&&data[0]==255);}fail_read_at=0;
 for(auto bad:{info(0,1,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),info(1,0,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),info(1,1,kRGBA_F16_SkColorType,kUnpremul_SkAlphaType),info(1,INT32_MAX,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType)}){ImageDataBuffer b({bad,data,8});CHECK(Export(&b)=="data:,");}
 ImageDataBuffer short_row({info(2,1,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),data,4});CHECK(Export(&short_row)=="data:,");
 ImageDataBuffer overflow({info(1,2,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),data,SIZE_MAX});CHECK(Export(&overflow)=="data:,");
 ImageDataBuffer absent({valid,nullptr,4});CHECK(Export(&absent)=="data:,");
 ImageDataBuffer invalid({valid,data,4});invalid.is_valid_=false;CHECK(Export(&invalid)=="data:,");
 noise_enabled=false;ImageDataBuffer native({valid,data,4});CHECK(Export(&native)=="encoded"&&native.pixmap_.addr()==data);
 std::cout<<"PASS real_skia_cpu=1 reference_roundtrips="<<roundtrips<<" formats=2 seeds=2 injected_failures=2\n";
}
