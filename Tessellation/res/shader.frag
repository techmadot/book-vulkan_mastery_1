#version 450

layout(location=0) flat in vec3 inColor;
layout(location=0) out vec4 outColor;

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
  outColor.xyz = inColor;
  outColor.w = 1;
  //outColor = vec4(inColor, 1.0);

  // vec3 toLightDir = -normalize(lightDir.xyz);
  // float dotNL = max(dot(toLightDir, inNormal), 0.5);
  // vec4 diffuse = texture(gTexDiffuse, inTexcoord0);

  // if(mode == 1)
  // {
  //   // AlphaMask
  //   if(diffuse.a  < 0.5) {
  //     discard;
  //   }
  // }

  // diffuse.xyz *= dotNL;
  // outColor = diffuse;
}
