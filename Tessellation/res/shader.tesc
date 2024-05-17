#version 450
layout(vertices=4) out;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
  vec4 tessParams;
  float time;
};

void main()
{
  if (gl_InvocationID == 0)
  {
    gl_TessLevelInner[0] = tessParams.x;
    gl_TessLevelInner[1] = tessParams.x;

    gl_TessLevelOuter[0] = tessParams.y;
    gl_TessLevelOuter[1] = tessParams.y;
    gl_TessLevelOuter[2] = tessParams.y;
    gl_TessLevelOuter[3] = tessParams.y;
  }
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
