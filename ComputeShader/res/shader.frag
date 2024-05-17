#version 450

layout(location=0) in vec2 inTexcoord0;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
};

layout(set=0,binding=1)
uniform sampler2D gImage;

void main()
{
  outColor = texture(gImage, inTexcoord0);
}
