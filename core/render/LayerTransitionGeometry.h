// SPDX-License-Identifier: GPL-3.0-or-later
// Written for Mikage (https://github.com/Mikage-Next/krkrsdl3); see MIKAGE-NOTICE.md.
#pragma once
#include "LayerTransition.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace layer_transition {
inline constexpr size_t MaxParameterBytes=64*1024*1024;
inline int32_t I32(const TVPLayerTransitionBytes& b,size_t i) {
    int32_t v; std::memcpy(&v,b.data()+i*4,4); return v;
}
inline uint16_t U16(const TVPLayerTransitionBytes& b,size_t i) {
    uint16_t v; std::memcpy(&v,b.data()+i*2,2); return v;
}
inline bool Fixed(int start,int step,int64_t delta,int& pixel) {
    const int64_t v=int64_t(start)+int64_t(step)*delta;
    if(v<std::numeric_limits<int32_t>::min() || v>std::numeric_limits<int32_t>::max()) return false;
    pixel=int(v>>16); return true;
}
// Shared preflight for the production facade, native backend and device double.
// Table scans may be skipped ONLY for an already validated immutable cache hit.
inline TVPLayerTransitionResult Validate(const TVPLayerTransitionOperation& op,
        int tw,int th,int sw1,int sh1,int sw2,int sh2,bool scanTable=true) {
    const auto& p=op.params;
    if(p.kind<=0 || p.kind>=int(TVPLayerTransitionKind::Count)) return TVPLayerTransitionResult::Unsupported;
    if(p.frameWidth<=0 || p.frameHeight<=0 || p.width<0 || p.height<0 ||
       p.left<0 || p.top<0 || int64_t(p.left)+p.width>p.frameWidth ||
       int64_t(p.top)+p.height>p.frameHeight || p.destLeft<0 || p.destTop<0 ||
       int64_t(p.destLeft)+p.width>tw || int64_t(p.destTop)+p.height>th ||
       sw1<p.frameWidth || sw2<p.frameWidth || sh1<p.frameHeight || sh2<p.frameHeight)
        return TVPLayerTransitionResult::InvalidGeometry;
    if(p.ratio<0 || p.ratio>255) return TVPLayerTransitionResult::InvalidParameters;
    const size_t tableBytes=op.table ? op.table->size() : 0,rowsBytes=op.rows ? op.rows->size() : 0;
    if(tableBytes>MaxParameterBytes || rowsBytes>MaxParameterBytes-tableBytes)
        return TVPLayerTransitionResult::AllocationFailed;
    if(!p.width || !p.height) return TVPLayerTransitionResult::Applied;
    if(p.kind==int(TVPLayerTransitionKind::Mosaic)) {
        if(p.blockSize<=0 || p.offsetX>0 || p.offsetY>0 ||
           int64_t(p.offsetX)<=-int64_t(p.blockSize) || int64_t(p.offsetY)<=-int64_t(p.blockSize))
            return TVPLayerTransitionResult::InvalidParameters;
        for(int axis=0;axis<2;++axis) {
            const int offset=axis?p.offsetY:p.offsetX,start=axis?p.top:p.left,size=axis?p.height:p.width;
            for(int x:{start,start+size-1}) {
                const int64_t distance=int64_t(x)-offset;
                const int64_t base=(distance/p.blockSize)*p.blockSize;
                if(distance>INT32_MAX || base>INT32_MAX || base+offset<INT32_MIN ||
                   base+offset+p.blockSize/2>INT32_MAX) return TVPLayerTransitionResult::InvalidParameters;
            }
        }
    } else if(p.kind==int(TVPLayerTransitionKind::Wave)) {
        if(p.flags>2 || rowsBytes!=size_t(p.height)*4) return TVPLayerTransitionResult::InvalidParameters;
        for(int y=0;y<p.height;++y) {
            const int64_t d=I32(*op.rows,y);
            if(int64_t(p.left)-d<std::numeric_limits<int32_t>::min() ||
               int64_t(p.left)+p.width-1-d>std::numeric_limits<int32_t>::max())
                return TVPLayerTransitionResult::InvalidParameters;
        }
    } else if(p.kind==int(TVPLayerTransitionKind::Ripple)) {
        if(p.centerX<0 || p.centerX>=p.frameWidth || p.centerY<0 || p.centerY>=p.frameHeight ||
           p.mapWidth!=std::max(p.centerX,p.frameWidth-p.centerX) ||
           p.mapHeight!=std::max(p.centerY,p.frameHeight-p.centerY) ||
           (p.blockSize!=16 && p.blockSize!=32 && p.blockSize!=64 && p.blockSize!=128) || tableBytes%2)
            return TVPLayerTransitionResult::InvalidParameters;
        const uint64_t map=uint64_t(p.mapWidth)*p.mapHeight,words=tableBytes/2;
        if(map>words || p.driftOffset<0 || uint64_t(p.driftOffset)<map ||
           uint64_t(p.driftOffset)+p.blockSize*32>words) return TVPLayerTransitionResult::InvalidParameters;
        if(scanTable) {
            for(size_t i=0;i<map;++i) if(U16(*op.table,i)>=p.blockSize*32)
                return TVPLayerTransitionResult::InvalidParameters;
            for(size_t i=map;i<words;++i) {
                const auto drift=U16(*op.table,i);
                int dx=int(drift>>8),dy=int(drift&255);
                if(dx>=128) dx-=256; if(dy>=128) dy-=256;
                if(std::abs(dx)>=p.frameWidth || std::abs(dy)>=p.frameHeight)
                    return TVPLayerTransitionResult::InvalidParameters;
            }
        }
    } else if(p.kind==int(TVPLayerTransitionKind::Turn)) {
        if(tableBytes!=(64*64*8+64)*4 || p.offsetX!=2) return TVPLayerTransitionResult::InvalidParameters;
        for(int bx:{p.left/64,(p.left+p.width-1)/64}) for(int by:{p.top/64,(p.top+p.height-1)/64}) {
            const int64_t raw=int64_t(p.phase)-int64_t(bx-by)*p.offsetX;
            if(raw<INT32_MIN || raw>INT32_MAX) return TVPLayerTransitionResult::InvalidParameters;
        }
        const auto& table=*op.table;
        for(int y=p.top;y<p.top+p.height;++y) for(int bx=p.left/64;bx<=(p.left+p.width-1)/64;++bx) {
            const int64_t raw=int64_t(p.phase)-int64_t(bx-y/64)*p.offsetX;
            int phase=int(std::clamp<int64_t>(raw,0,63));
            if(phase==0 || phase==63) continue;
            const size_t i=size_t(phase*64+y%64)*8;
            int start=I32(table,i),len=I32(table,i+1);
            if(start<0 || len<0 || int64_t(start)+len>64) return TVPLayerTransitionResult::InvalidParameters;
            const int l=std::max(p.left-bx*64,start),r=std::min(p.left+p.width-bx*64,start+len);
            for(int x:{l,r-1}) if(l<r) {
                int sx,sy;
                if(!Fixed(I32(table,i+2),I32(table,i+6),x-start,sx) ||
                   !Fixed(I32(table,i+3),I32(table,i+7),x-start,sy)) return TVPLayerTransitionResult::InvalidParameters;
                sx+=bx*64; sy+=(y/64)*64;
                if(sy>=p.frameHeight) continue; // original bottom-block background
                const int sw=phase<32?sw1:sw2,sh=phase<32?sh1:sh2;
                if(sx<0 || sy<0 || sx>=sw || sy>=sh) return TVPLayerTransitionResult::InvalidGeometry;
            }
            const int gloss=I32(table,64*64*8+phase);
            if(gloss<0 || gloss>255) return TVPLayerTransitionResult::InvalidParameters;
        }
    } else if(p.kind==int(TVPLayerTransitionKind::ScanLine)) {
        // Mikage (GPL-3.0-or-later): extNagano transition checks.
        if(tableBytes || rowsBytes || p.offsetX<0 || p.offsetX>p.frameWidth)
            return TVPLayerTransitionResult::InvalidParameters;
    } else if(p.kind==int(TVPLayerTransitionKind::RGBFade) || p.kind==int(TVPLayerTransitionKind::BlurFade)) {
        if(tableBytes || rowsBytes || p.flags>2) return TVPLayerTransitionResult::InvalidParameters;
    } else if(p.kind==int(TVPLayerTransitionKind::ZoomFade)) {
        if(tableBytes || rowsBytes!=(size_t(p.height)*2+size_t(p.width)*2)*4)
            return TVPLayerTransitionResult::InvalidParameters;
        const auto& rows=*op.rows;
        for(int i=0;i<p.height*2;++i) {
            const int32_t v=I32(rows,i);
            if(v<0 || v>=p.frameHeight) return TVPLayerTransitionResult::InvalidParameters;
        }
        for(int i=0;i<p.width*2;++i) {
            const int32_t v=I32(rows,size_t(p.height)*2+i);
            if(v<0 || v>=p.frameWidth) return TVPLayerTransitionResult::InvalidParameters;
        }
    } else if(p.kind==int(TVPLayerTransitionKind::ImageWipe)) {
        constexpr int Limit=1<<28;
        if(tableBytes || p.flags>1 || p.blockSize<0 || p.blockSize>Limit || p.mapWidth<0 || p.mapHeight<0 ||
           int64_t(p.mapWidth)+p.mapHeight>p.blockSize || p.offsetX<-Limit || p.offsetX>Limit ||
           rowsBytes!=(uint64_t(p.height)+uint64_t(p.height)*uint64_t(p.mapHeight))*4)
            return TVPLayerTransitionResult::InvalidParameters;
        const auto& rows=*op.rows;
        for(int n=0;n<p.height;++n) {
            const int edge=I32(rows,n);
            if(edge<0 || edge>p.blockSize) return TVPLayerTransitionResult::InvalidParameters;
            // Rule columns read by the band must lie inside the kept columns.
            const int64_t lo=std::max<int64_t>(p.left,p.offsetX),hi=std::min<int64_t>(int64_t(p.left)+p.width,int64_t(p.offsetX)+edge);
            if(lo<hi && (lo-p.offsetX<p.mapWidth || hi-p.offsetX>int64_t(p.mapWidth)+p.mapHeight))
                return TVPLayerTransitionResult::InvalidParameters;
        }
    } else if(p.kind==int(TVPLayerTransitionKind::Spin)) {
        // Mikage (GPL-3.0-or-later): extNagano spin/multiripple checks.
        if(tableBytes || rowsBytes!=size_t(p.width)*3*4) return TVPLayerTransitionResult::InvalidParameters;
        const auto& rows=*op.rows;
        const int64_t hc=p.frameHeight>>1;
        const int64_t maxY=std::max<int64_t>(std::abs(int64_t(p.top)-hc),std::abs(int64_t(p.top)+p.height-1-hc));
        for(int i=0;i<p.width;++i) {
            const int sel=I32(rows,size_t(i)*3),col=I32(rows,size_t(i)*3+1),slope=I32(rows,size_t(i)*3+2);
            if(sel<-1 || sel>1 || slope<-(1<<20) || slope>(1<<20) || int64_t(std::abs(slope))*maxY>INT32_MAX/2)
                return TVPLayerTransitionResult::InvalidParameters;
            if(sel>=0 && (col<0 || col>=p.frameWidth)) return TVPLayerTransitionResult::InvalidParameters;
        }
    } else if(p.kind==int(TVPLayerTransitionKind::MultiRipple)) {
        const uint64_t map=(uint64_t(p.frameWidth)*p.frameHeight*2+3)/4;
        if(p.driftOffset<1 || int64_t(p.frameWidth)*p.frameHeight>INT32_MAX/4 ||
           tableBytes!=(map+uint64_t(p.driftOffset)*2)*4 || rowsBytes<8)
            return TVPLayerTransitionResult::InvalidParameters;
        const auto& rows=*op.rows;
        const int count=I32(rows,0),base=I32(rows,1);
        if(count<0 || count>20 || base<0 || base>(1<<20) || rowsBytes!=(2+size_t(count)*4)*4)
            return TVPLayerTransitionResult::InvalidParameters;
        for(int i=0;i<count;++i) {
            const size_t r=2+size_t(i)*4;
            const int cx=I32(rows,r),cy=I32(rows,r+1),phase=I32(rows,r+2),drift=I32(rows,r+3);
            if(cx<0 || cx>=p.frameWidth || cy<0 || cy>=p.frameHeight || phase<0 || phase>(1<<28) ||
               drift<0 || drift>255) return TVPLayerTransitionResult::InvalidParameters;
        }
        if(scanTable) {
            // Wave/blend values are summed for up to 20 ripples; keep the sums inside int32.
            for(size_t i=0;i<size_t(p.driftOffset)*2;++i) {
                const int v=I32(*op.table,map+i);
                if(v<-(1<<22) || v>(1<<22)) return TVPLayerTransitionResult::InvalidParameters;
            }
        }
    } else {
        if(rowsBytes!=size_t(p.frameHeight)*26*4) return TVPLayerTransitionResult::InvalidParameters;
        const auto& rows=*op.rows;
        for(int y=p.top;y<p.top+p.height;++y) {
            const size_t base=size_t(y)*26;
            const int count=I32(rows,base);
            if(count<1 || count>5) return TVPLayerTransitionResult::InvalidParameters;
            std::array<std::pair<int,int>,5> coverage{};
            for(int j=0;j<count;++j) {
                const size_t r=base+11+j*3;
                int left=I32(rows,r),right=I32(rows,r+1),type=I32(rows,r+2);
                if(type<0 || type>2 || left<0 || right<left || right>p.frameWidth)
                    return TVPLayerTransitionResult::InvalidParameters;
                coverage[j]={left,right};
                if(type==0) continue;
                const int l=std::max(p.left,left),rr=std::min(p.left+p.width,right);
                const size_t line=base+(type==1?1:6);
                for(int x:{l,rr-1}) if(l<rr) {
                    int sx,sy;
                    if(!Fixed(I32(rows,line+1),I32(rows,line+3),int64_t(x)-I32(rows,line),sx) ||
                       !Fixed(I32(rows,line+2),I32(rows,line+4),int64_t(x)-I32(rows,line),sy))
                        return TVPLayerTransitionResult::InvalidParameters;
                    if(sx<0 || sy<0 || sx>=(type==1?sw1:sw2) || sy>=(type==1?sh1:sh2))
                        return TVPLayerTransitionResult::InvalidGeometry;
                }
            }
            std::sort(coverage.begin(),coverage.begin()+count);
            int covered=p.left;
            for(int i=0;i<count;++i) {
                if(coverage[i].second<=covered) continue;
                if(coverage[i].first>covered) break;
                covered=coverage[i].second;
            }
            if(covered<p.left+p.width) return TVPLayerTransitionResult::InvalidParameters;
        }
    }
    return TVPLayerTransitionResult::Applied;
}
}
