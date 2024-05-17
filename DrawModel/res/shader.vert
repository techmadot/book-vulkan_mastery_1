#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inTexcoord0;

layout(location=0) out vec3 outNormal;
layout(location=1) out vec2 outTexcoord0;

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
  vec4 worldPosition = matWorld * vec4(inPos, 1);
  vec3 worldNormal = mat3(matWorld) * inNormal;
  gl_Position = matProj * matView * worldPosition;
  
  outNormal = worldNormal * 0.5 + 0.5;
  outTexcoord0 = inTexcoord0;
}
