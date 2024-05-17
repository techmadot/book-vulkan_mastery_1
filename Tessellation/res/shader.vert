#version 450

layout(location=0) in vec3 inPos;

out gl_PerVertex
{
  vec4 gl_Position;
};

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
};

void main()
{
  gl_Position = vec4(inPos, 1);
}
