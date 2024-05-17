@echo off

for %%f in (*.*) do (
  if "%%~xf"==".vert" (
    glslangValidator -S vert %%~f --target-env vulkan1.0 -o %%~f.spv
  )
  if "%%~xf"==".frag" (
    glslangValidator -S frag %%~f --target-env vulkan1.0 -o %%~f.spv
  )
)
@echo on
