#version 450

layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
layout(location = 6) in mediump uint Scale;
#ifdef TEXTURED
layout(location = 2) in mediump uvec4 Window;
layout(location = 3) in mediump ivec3 Param;
layout(location = 4) in ivec4 UV;
layout(location = 5) in mediump uvec4 UVRange;

layout(location = 1) out mediump vec2 vUV;
layout(location = 2) flat out mediump ivec3 vParam;
layout(location = 3) flat out mediump ivec2 vBaseUV;
layout(location = 4) flat out mediump ivec4 vWindow;
layout(location = 5) flat out mediump ivec4 vTexLimits;
#endif
layout(location = 0) out mediump vec4 vColor;

const vec2 FB_SIZE = vec2(1024, 512);
//const vec4 texture_limits = vec4(0.0, 0.0, 1024.0, 1024.0);

void main()
{
   vec2 off = vec2(0.5, 0.5) * float(Scale == 1);
#ifdef TEXTURED
   vec2 uvoff = vec2(0.5, 0.5) * float(Scale == 1 || Position.w != 1.0);
#endif
   // From center of pixel so 0.5..1023.5, 0.5..511.5, then divide and adjust to screen coord
   gl_Position = vec4((Position.xy + off) / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
   vColor = Color;
#ifdef TEXTURED
   // What am I doing?
   vUV = vec2(UV.xy) + uvoff;
   vParam = Param;
   vBaseUV = UV.zw;
   vWindow = ivec4(Window);
   vTexLimits = ivec4(UVRange);
#endif
}
