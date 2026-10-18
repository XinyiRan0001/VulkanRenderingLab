#version 450
layout(binding=0) uniform sampler2D pyramid;
layout(binding=1) uniform sampler2D rawDepth;
layout(std140,binding=2) uniform Settings { ivec4 region; ivec4 query; vec4 depths; ivec4 view; vec4 pixels; ivec4 selection; } s;
layout(std430,binding=3) readonly buffer Result { vec4 result; };
layout(location=0) out vec4 outColor;
float border(vec2 p,vec2 a,vec2 b,float thickness) {
    if(any(lessThan(p,a)) || any(greaterThan(p,b))) return 0;
    return min(min(p.x-a.x,b.x-p.x),min(p.y-a.y,b.y-p.y))<thickness ? 1.0:0.0;
}
// Tiny code-native 3x5 ASCII font; diagnostic text needs no font assets.
uint glyph(int c) {
 switch(c) {
 case 48: return 31599u;
 case 49: return 29850u;
 case 50: return 29671u;
 case 51: return 31207u;
 case 52: return 18925u;
 case 53: return 31183u;
 case 54: return 31695u;
 case 55: return 9383u;
 case 56: return 31727u;
 case 57: return 31215u;
 case 65: return 23530u;
 case 66: return 15083u;
 case 67: return 29263u;
 case 68: return 15211u;
 case 69: return 29391u;
 case 70: return 4815u;
 case 71: return 31567u;
 case 72: return 23533u;
 case 73: return 29847u;
 case 74: return 31524u;
 case 75: return 23277u;
 case 76: return 29257u;
 case 77: return 23549u;
 case 78: return 24573u;
 case 79: return 31599u;
 case 80: return 5103u;
 case 81: return 20335u;
 case 82: return 23275u;
 case 83: return 31183u;
 case 84: return 9367u;
 case 85: return 31597u;
 case 86: return 11117u;
 case 87: return 24557u;
 case 88: return 23213u;
 case 89: return 9389u;
 case 90: return 29351u;
 case 46: return 8192u;
 case 47: return 4772u;
 case 58: return 1040u;
 case 45: return 448u;
 default: return 0u;
 }
}
bool letter(vec2 p,vec2 origin,int c) {
    ivec2 q=ivec2(floor((p-origin)/2.0));
    if(any(lessThan(q,ivec2(0))) || q.x>=3 || q.y>=5) return false;
    return ((glyph(c)>>uint(q.y*3+q.x))&1u)!=0u;
}
bool label(vec2 p,vec2 origin,int which) {
 // PYRAMID MIP
 if(which==0) { const int chars[11]=int[](80,89,82,65,77,73,68,32,77,73,80); for(int i=0;i<11;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // RAW DEPTH
 if(which==1) { const int chars[9]=int[](82,65,87,32,68,69,80,84,72); for(int i=0;i<9;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // SPHERE
 if(which==2) { const int chars[6]=int[](83,80,72,69,82,69); for(int i=0;i<6;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // QUERY MIP
 if(which==3) { const int chars[9]=int[](81,85,69,82,89,32,77,73,80); for(int i=0;i<9;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // NEAR
 if(which==4) { const int chars[4]=int[](78,69,65,82); for(int i=0;i<4;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // MAX
 if(which==5) { const int chars[3]=int[](77,65,88); for(int i=0;i<3;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // EPS 0.000100
 if(which==6) { const int chars[12]=int[](69,80,83,32,48,46,48,48,48,49,48,48); for(int i=0;i<12;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // HIZ ON
 if(which==7) { const int chars[6]=int[](72,73,90,32,79,78); for(int i=0;i<6;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // HIZ OFF
 if(which==8) { const int chars[7]=int[](72,73,90,32,79,70,70); for(int i=0;i<7;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // FRUSTUM REJECT
 if(which==9) { const int chars[14]=int[](70,82,85,83,84,85,77,32,82,69,74,69,67,84); for(int i=0;i<14;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // BYPASS - KEEP
 if(which==10) { const int chars[13]=int[](66,89,80,65,83,83,32,45,32,75,69,69,80); for(int i=0;i<13;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // VISIBLE
 if(which==11) { const int chars[7]=int[](86,73,83,73,66,76,69); for(int i=0;i<7;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // OCCLUDED
 if(which==12) { const int chars[8]=int[](79,67,67,76,85,68,69,68); for(int i=0;i<8;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // CYAN BOUND / COLOR QUERY CELLS
 if(which==13) { const int chars[30]=int[](67,89,65,78,32,66,79,85,78,68,32,47,32,67,79,76,79,82,32,81,85,69,82,89,32,67,69,76,76,83); for(int i=0;i<30;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // PGUP/DN MIP  TAB SELECT
 if(which==14) { const int chars[23]=int[](80,71,85,80,47,68,78,32,77,73,80,32,32,84,65,66,32,83,69,76,69,67,84); for(int i=0;i<23;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // N/A
 if(which==15) { const int chars[3]=int[](78,47,65); for(int i=0;i<3;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // NEAR/EYE BYPASS
 if(which==16) { const int chars[15]=int[](78,69,65,82,47,69,89,69,32,66,89,80,65,83,83); for(int i=0;i<15;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 // OFFSCREEN BYPASS
 if(which==17) { const int chars[16]=int[](79,70,70,83,67,82,69,69,78,32,66,89,80,65,83,83); for(int i=0;i<16;++i) if(letter(p,origin+vec2(i*8,0),chars[i])) return true; }
 return false;
}
bool number(vec2 p,vec2 origin,uint value,int digits) {
    for(int i=0;i<digits;++i) {
        uint divisor=1u; for(int j=0;j<digits-i-1;++j) divisor*=10u;
        if(letter(p,origin+vec2(i*8,0),48+int((value/divisor)%10u))) return true;
    }
    return false;
}
bool decimalDepth(vec2 p,vec2 origin,float value) {
    uint fixedValue=uint(round(clamp(value,0.0,1.0)*1000000.0));
    return number(p,origin,fixedValue/1000000u,1) || letter(p,origin+vec2(8,0),46) || number(p,origin+vec2(16,0),fixedValue%1000000u,6);
}
void main() {
    vec2 origin=vec2(s.view.z-440,18),size=vec2(420,390);
    vec2 canvas=gl_FragCoord.xy-origin;
    if(any(lessThan(canvas,vec2(0))) || any(greaterThanEqual(canvas,size))) discard;
    vec3 state=result.w==3.0 ? vec3(1,.18,.1) : (result.w==0.0 ? vec3(1,.65,.1) : (result.w==1.0 ? vec3(.6,.6,.6):vec3(.2,1,.35)));
    vec3 textColor=vec3(.92);
    bool text=label(canvas,vec2(10,10),s.view.y<0?1:0);
    if(s.view.y>=0) text=text || number(canvas,vec2(110,10),uint(s.view.y),2);
    text=text || label(canvas,vec2(10,286),2) || number(canvas,vec2(66,286),uint(s.selection.x),6);
    text=text || label(canvas,vec2(138,286),3) || (s.query.y!=0?number(canvas,vec2(218,286),uint(s.query.x),2):label(canvas,vec2(218,286),15));
    text=text || label(canvas,vec2(10,302),4) || (s.query.y!=0?decimalDepth(canvas,vec2(50,302),result.x):label(canvas,vec2(50,302),15));
    text=text || label(canvas,vec2(138,302),5) || (s.query.y!=0?decimalDepth(canvas,vec2(170,302),result.y):label(canvas,vec2(170,302),15));
    text=text || label(canvas,vec2(10,318),6) || label(canvas,vec2(138,318),s.query.w!=0?7:8);
    text=text || label(canvas,vec2(10,350),13) || label(canvas,vec2(10,366),14);
    int decision=int(result.w);
    bool statusText=label(canvas,vec2(10,334),decision==0?9:(decision==1?(s.selection.z==1?16:17):(decision==3?12:11)));
    vec2 p=canvas-vec2(8,30),imageSize=vec2(404,240);
    bool inImage=all(greaterThanEqual(p,vec2(0))) && all(lessThan(p,imageSize));
    if(!inImage) {
        vec3 color=text?textColor:vec3(.035,.045,.06);
        if(statusText) color=state;
        if(border(canvas,vec2(0),size-1,2)>0.0) color=state;
        outColor=vec4(color,1);return;
    }
    vec2 uv=p/imageSize;
    bool raw=s.view.y<0;
    int mip=max(0,s.view.y);
    ivec2 dimensions=raw ? textureSize(rawDepth,0) : textureSize(pyramid,mip);
    ivec2 cell=min(ivec2(uv*dimensions),dimensions-1);
    float depth=raw ? texelFetch(rawDepth,cell,0).r : texelFetch(pyramid,cell,mip).r;
    float distance=abs(s.depths.z/(depth+s.depths.y));
    float shade=depth>=.999999 ? 1.0 : 1.0-exp(-distance*.12);
    vec3 color=vec3(shade);
    vec2 extent=raw ? vec2(s.view.zw) : vec2(textureSize(pyramid,0));
    vec2 source=uv*extent;
    if(!raw && (source.x>=s.view.z || source.y>=s.view.w)) color=vec3(.25,.1,.3); // far-depth padding
    if(!raw && max(dimensions.x,dimensions.y)<=128) {
        vec2 f=fract(uv*dimensions);
        if(min(f.x*imageSize.x/dimensions.x,f.y*imageSize.y/dimensions.y)<.65) color*=.65;
    }
    if(s.query.y!=0) {
        float scale=exp2(float(s.query.x));
        vec2 a=vec2(s.region.xy)*scale/extent*imageSize;
        vec2 b=vec2(s.region.zw+1)*scale/extent*imageSize;
        if(all(greaterThanEqual(p,a)) && all(lessThan(p,b))) color=mix(color,state,.22);
        if(border(p,a,b,1.6)>0.0) color=state; // actual sampled coverage
        vec2 pa=s.pixels.xy/extent*imageSize,pb=(s.pixels.zw+1)/extent*imageSize;
        if(border(p,pa,pb,1.1)>0.0) color=vec3(0,1,1); // expanded projected bound
    }
    if(border(p,vec2(0),imageSize-1,2)>0.0) color=state;
    outColor=vec4(color,1);
}
