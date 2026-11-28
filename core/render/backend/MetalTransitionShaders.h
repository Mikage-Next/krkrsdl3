// SPDX-License-Identifier: GPL-3.0-or-later
// Written for Mikage (https://github.com/Mikage-Next/krkrsdl3); see MIKAGE-NOTICE.md.
#pragma once
#include "MetalLayerShaders.h"

inline constexpr char TVP_TRANSITION_MSL_DEFINITIONS[] =
#define TVP_LAYER_TRANSITION(name, id, text) "constant int TVP_TRANSITION_" #name "=" #id ";\n"
#define TVP_LAYER_TRANSITION_COUNT(count)
#include "../LayerTransitionDefinitions.def"
#undef TVP_LAYER_TRANSITION_COUNT
#undef TVP_LAYER_TRANSITION
;

inline constexpr char kMetalTransitionShaders[] = R"MSL(
struct TransitionParams {
    int kind,frameWidth,frameHeight,ratio;
    uint flags,color;
    int blockSize,offsetX,offsetY,phase,centerX,centerY,mapWidth,mapHeight,driftOffset;
    int left,top,width,height,destLeft,destTop;
};
// BEGIN TRANSITION SCALAR: extracted unchanged by the portable tests. The
// texture adapter only transports bytes; it does not calculate effect pixels.
uint transitionRead(texture2d<float,access::read> s,int x,int y) {
    return layerPack(layerBytes(s,int2(x,y)));
}
uint transitionBlend(uint a,uint b,int ratio) {
    uint result=0;
    for(uint shift=0;shift<32;shift+=8) {
        uint v=(a>>shift)&255u;
        result|=((v+(((((b>>shift)&255u)-v)*uint(ratio))>>8))&255u)<<shift;
    }
    return result;
}
uint transitionPixel(int x,int y,constant TransitionParams& p,
    const device int* rows,const device ushort* table,const device uchar* alpha,
    texture2d<float,access::read> s1,texture2d<float,access::read> s2) {
    int sx=x,sy=y;
    if(p.kind==TVP_TRANSITION_Mosaic) {
        sx=clamp(((x-p.offsetX)/p.blockSize)*p.blockSize+p.offsetX+p.blockSize/2,0,p.frameWidth-1);
        sy=clamp(((y-p.offsetY)/p.blockSize)*p.blockSize+p.offsetY+p.blockSize/2,0,p.frameHeight-1);
        return transitionBlend(transitionRead(s1,sx,sy),transitionRead(s2,sx,sy),p.ratio);
    }
    if(p.kind==TVP_TRANSITION_Wave) {
        sx=x-rows[y-p.top];
        if(sx<0 || sx>=p.frameWidth) return p.color;
        uint a=transitionRead(s1,sx,y),b=transitionRead(s2,sx,y);
        if(p.flags==1u) return constAlphaSDDestAlpha(a,b,uint(p.ratio),alpha);
        if(p.flags==2u) return univTransBlendARGB(a,b,uint(p.ratio));
        return constAlphaSD(a,b,uint(p.ratio));
    }
    if(p.kind==TVP_TRANSITION_Ripple) {
        int mx=x<p.centerX ? p.centerX-x-1 : x-p.centerX;
        int my=y<p.centerY ? p.centerY-y-1 : y-p.centerY;
        uint drift=table[p.driftOffset+int(table[my*p.mapWidth+mx])];
        int dx=int((drift>>8)&255u),dy=int(drift&255u);
        if(dx>=128) dx-=256; if(dy>=128) dy-=256;
        sx=x+(x<p.centerX ? dx : -dx);
        sy=y+(y<p.centerY ? dy : -dy);
        if(sx<0) sx=-sx; if(sy<0) sy=-sy;
        if(sx>=p.frameWidth) sx=p.frameWidth-1-(sx-p.frameWidth);
        if(sy>=p.frameHeight) sy=p.frameHeight-1-(sy-p.frameHeight);
        return constAlphaSD(transitionRead(s1,sx,sy),transitionRead(s2,sx,sy),uint(p.ratio));
    }
    if(p.kind==TVP_TRANSITION_Turn) {
        int bx=x/64,by=y/64;
        int phase=clamp(p.phase-(bx-by)*p.offsetX,0,63);
        if(phase==0) return transitionRead(s1,x,y);
        if(phase==63) return transitionRead(s2,x,y);
        const device int* t=reinterpret_cast<const device int*>(table);
        const device int* line=t+(phase*64+(y%64))*8;
        int local=x%64;
        if(local<line[0] || local>=line[0]+line[1]) return p.color;
        sx=bx*64+((line[2]+line[6]*(local-line[0]))>>16);
        sy=by*64+((line[3]+line[7]*(local-line[0]))>>16);
        if(sy>=p.frameHeight) return p.color;
        uint value=phase<32 ? transitionRead(s1,sx,sy) : transitionRead(s2,sx,sy);
        int gloss=t[64*64*8+phase];
        return gloss ? transitionBlend(value,0x00ffffffu,gloss) : value;
    }
    // Mikage (GPL-3.0-or-later): extNagano kinds; same integer math as the plugin CPU code.
    if(p.kind==TVP_TRANSITION_ScanLine) {
        int pos=p.offsetX,w=p.frameWidth;
        if((y&1)==0) return x<pos ? transitionRead(s2,x+(w-pos),y) : transitionRead(s1,x-pos,y);
        return x<w-pos ? transitionRead(s1,x+pos,y) : transitionRead(s2,x-(w-pos),y);
    }
    if(p.kind==TVP_TRANSITION_RGBFade) {
        if(p.flags==1u) return transitionRead(s1,x,y);
        if(p.flags==2u) return transitionRead(s2,x,y);
        uint a=transitionRead(s1,x,y),b=transitionRead(s2,x,y);
        int opaB=int(p.color&255u),opaG=int((p.color>>8)&255u),opaR=int((p.color>>16)&255u),opaA=int(p.color>>24);
        int s1b=int(a&255u),s1g=int((a>>8)&255u),s1r=int((a>>16)&255u),s1a=int(a>>24);
        int s2b=int(b&255u),s2g=int((b>>8)&255u),s2r=int((b>>16)&255u),s2a=int(b>>24);
        int db,dg,dr,da;
        if(s2a==0) {
            db=(s1b*opaB)&255; dg=(s1g*opaG)&255; dr=(s1r*opaR)&255;
            da=s1a-((s1a*opaA)>>8);
        } else if(s1a==0) {
            db=(s1b*opaB)&255; dg=(s1g*opaG)&255; dr=(s1r*opaR)&255;
            da=(s2a*opaA)>>8;
        } else {
            db=s1b+(((s2b-s1b)*opaB)>>8); dg=s1g+(((s2g-s1g)*opaG)>>8);
            dr=s1r+(((s2r-s1r)*opaR)>>8); da=s1a+(((s2a-s1a)*opaA)>>8);
        }
        return (uint(da&255)<<24)|(uint(dr&255)<<16)|(uint(dg&255)<<8)|uint(db&255);
    }
    if(p.kind==TVP_TRANSITION_BlurFade) {
        uint a=transitionRead(s1,x,y);
        if(p.flags==1u) return a;
        uint b=transitionRead(s2,x,y);
        if(p.flags==2u) return b;
        return transitionBlend(a,b,p.ratio);
    }
    if(p.kind==TVP_TRANSITION_ZoomFade) {
        int idx=2*(y-p.top),col=2*p.height+2*(x-p.left);
        uint a=transitionRead(s1,rows[col],rows[idx]),b=transitionRead(s2,rows[col+1],rows[idx+1]);
        uint s1a=a>>24,s2a=b>>24,phase=uint(p.ratio);
        if(s2a==0u) return (a&0x00ffffffu)|(((s1a*(255u-phase))>>8)<<24);
        if(s1a==0u) return (b&0x00ffffffu)|(((s2a*phase)>>8)<<24);
        return transitionBlend(a,b,p.ratio);
    }
    if(p.kind==TVP_TRANSITION_ImageWipe) {
        int row=y-p.top,th=p.offsetX,edge=rows[row];
        bool rightFirst=p.flags!=0u; // dir!=0: left region shows source 1
        if(x<th) return rightFirst ? transitionRead(s1,x,y) : transitionRead(s2,x,y);
        if(x<th+edge) {
            uint base=rightFirst ? transitionRead(s1,x,y) : transitionRead(s2,x,y);
            int rcol=x-th;
            if(rcol<p.blockSize && rcol>=p.mapWidth && rcol<p.mapWidth+p.mapHeight) {
                uint rp=uint(rows[p.height+row*p.mapHeight+(rcol-p.mapWidth)]);
                return transitionBlend(base,rp,int(rp>>24));
            }
            return base;
        }
        return rightFirst ? transitionRead(s2,x,y) : transitionRead(s1,x,y);
    }
    if(p.kind==TVP_TRANSITION_Spin) {
        int idx=3*(x-p.left),hc=p.frameHeight>>1;
        if(rows[idx]<0) return 0u;
        int srow=((rows[idx+2]*(y-hc))>>8)+hc;
        if(srow<0 || srow>=p.frameHeight) return 0u;
        return rows[idx]==0 ? transitionRead(s1,rows[idx+1],srow) : transitionRead(s2,rows[idx+1],srow);
    }
    if(p.kind==TVP_TRANSITION_MultiRipple) {
        const device int* t=reinterpret_cast<const device int*>(table);
        int area=p.frameWidth*p.frameHeight,len=p.driftOffset;
        const device int* waves=t+(area*2+3)/4;
        const device int* blends=waves+len;
        int count=rows[0],blend=rows[1],vdisp=0;
        for(int i=0;i<count;++i) {
            const device int* r=rows+2+i*4;
            int dx=x-r[0],dy=y-r[1];
            if(dx<0) dx=-dx;
            if(dy<0) dy=-dy;
            int wc=r[2]-int(table[dy*p.frameWidth+dx]);
            if(wc>=0) {
                if(wc>=len) wc=len-1;
                blend+=blends[wc];
                vdisp-=(waves[wc]*r[3])>>8;
            }
        }
        int row=clamp(y+vdisp,0,p.frameHeight-1);
        uint a=transitionRead(s1,x,row),b=transitionRead(s2,x,row);
        if((blend&0x100)!=0) { uint swap=a; a=b; b=swap; }
        return transitionBlend(a,b,blend&255)|0xff000000u;
    }
    // Rotations consume the software's fixed-point scanline plan, including
    // region order and Copy/StretchCopy/LinTransCopy byte coordinates.
    const device int* row=rows+y*26;
    uint result=p.color;
    for(int i=0;i<row[0];++i) {
        const device int* region=row+11+i*3;
        if(x<region[0] || x>=region[1]) continue;
        if(region[2]==0) { result=p.color; continue; }
        const device int* line=row+(region[2]==1 ? 1 : 6);
        sx=(line[1]+(x-line[0])*line[3])>>16;
        sy=(line[2]+(x-line[0])*line[4])>>16;
        result=region[2]==1 ? transitionRead(s1,sx,sy) : transitionRead(s2,sx,sy);
    }
    return result;
}
// END TRANSITION SCALAR
kernel void extransLayer(uint2 tid [[thread_position_in_grid]],
    constant TransitionParams& p [[buffer(0)]],const device int* rows [[buffer(1)]],
    const device ushort* table [[buffer(2)]],const device uchar* alpha [[buffer(3)]],
    texture2d<float,access::read> source1 [[texture(0)]],
    texture2d<float,access::read> source2 [[texture(1)]],
    texture2d<float,access::write> target [[texture(2)]]) {
    if(int(tid.x)>=p.width || int(tid.y)>=p.height) return;
    uint value=transitionPixel(p.left+int(tid.x),p.top+int(tid.y),p,rows,table,alpha,source1,source2);
    target.write(float4(layerUnpack(value))/255.0,uint2(p.destLeft+int(tid.x),p.destTop+int(tid.y)));
}
)MSL";
inline std::string TVPBuildMetalTransitionShaderSource() {
    return TVPBuildMetalLayerShaderSource()+TVP_TRANSITION_MSL_DEFINITIONS+kMetalTransitionShaders;
}
