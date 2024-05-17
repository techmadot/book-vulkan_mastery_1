#version 450
layout(quads, equal_spacing, ccw) in;

layout(location=0) flat out vec3 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4 matView;
  mat4 matProj;
  vec4 tessParams;
  float time;
};

// 三角関数で適当に移動.
vec3 GetWavePos(vec4 inPosition)
{
  float dist = length(inPosition);
  float waveHeight = sin(inPosition.x+time) * cos(inPosition.z+time);
  inPosition.y += waveHeight;
  return inPosition.xyz;
}

float pseudoRandom(vec2 coord) {
  return clamp(fract(sin(dot(coord.xy, vec2(12.9898, 78.233))) * 43758.5453), 0, 1);
}


void main()
{
  // 再分割された頂点位置を求める.
  vec3 domain = gl_TessCoord;
  vec4 p0 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, domain.x);
  vec4 p1 = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, domain.x);
  vec4 basePosition = mix(p0, p1, domain.y);

  // 生成された頂点を移動＆座標変換.
  vec3 newPosition = GetWavePos(basePosition);
  gl_Position = matProj * matView * vec4(newPosition, 1);

  uint index = 0;
  if(tessParams.z > 0.5)
  {
    index = uint(pseudoRandom(domain.xy) * 10);
  }

  vec3 colors[10] = {
    vec3(0),
    vec3(1),
    vec3(1,0,0),
    vec3(0,1,0),
    vec3(0,0,1),
    vec3(1,1,0),
    vec3(0,1,1),
    vec3(1,0,1),
    vec3(1,.5,0),
    vec3(0.5),
  };
  outColor = colors[ index ];
}
