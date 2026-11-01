#version 450 core
uniform sampler2D selectedMask;
uniform sampler2DMS sceneDepth;
uniform ivec2 regionOrigin,regionSize;
uniform vec2 pixelsPerDip;
uniform int sampleCount;
layout(location=0) out vec4 color;
vec2 coverage(ivec2 p) {
  if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,regionSize))) return vec2(0);
  return texelFetch(selectedMask,p,0).rg;
}
void main() {
  ivec2 p=ivec2(gl_FragCoord.xy)-regionOrigin;
  float center=coverage(p).r;
  if(center>=0.9999) discard;
  float depth=1.0;
for(int s=0;s<sampleCount;++s) depth=min(depth,texelFetch(sceneDepth,p,s).r);
  float ring=0.0,glow=0.0;
  ivec2 radius=ivec2(ceil(6.0*pixelsPerDip));
  for(int y=-radius.y;y<=radius.y;++y) for(int x=-radius.x;x<=radius.x;++x) {
    float distance=length(vec2(x,y)/pixelsPerDip);
    if(distance>6.0) continue;
    vec2 c=coverage(p+ivec2(x,y));
    if(c.r<=0.0 || depth+0.000002<c.g/c.r) continue;
    ring=max(ring,c.r*(1.0-smoothstep(1.5,2.5,distance)));
    glow=max(glow,c.r*0.22*pow(max(0.0,1.0-distance/6.0),2.0));
  }
  float alpha=max(ring,glow)*(1.0-center);
  if(alpha<=0.0001) discard;
  color=vec4(1.0,0.72,0.08,alpha);
}