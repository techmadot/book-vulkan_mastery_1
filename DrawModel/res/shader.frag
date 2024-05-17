#version 450

layout(location=0) in vec3 inNormal;
layout(location=1) in vec2 inTexcoord0;
layout(location=0) out vec4 outColor;

layout(set=0,binding=2)
uniform sampler2D gTexDiffuse;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
};

layout(set=0, binding=1)
uniform MeshParameters
{
  mat4 matWorld;
  //----
  vec4 baseColor; // diffuse + alpha
  vec4 specular;  // specular + shininess
  vec4 ambient;
  int mode;
};


void main()
{
  vec3 toLightDir = -normalize(lightDir.xyz);
  float dotNL = max(dot(toLightDir, inNormal), 0.5);
  vec4 diffuse = texture(gTexDiffuse, inTexcoord0);

  if(mode == 1)
  {
    // AlphaMask
    if(diffuse.a  < 0.5) {
      discard;
    }
  }

  diffuse.xyz *= dotNL;
  outColor = diffuse;
}
