#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec2 inTexcoord0;

layout(location=0) out vec2 outTexcoord0;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
};

void main()
{
  gl_Position = matProj * matView * vec4(inPos, 1);
  outTexcoord0 = inTexcoord0;
}
