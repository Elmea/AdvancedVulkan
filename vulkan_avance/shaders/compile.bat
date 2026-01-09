"%VK_SDK_PATH%/Bin/glslc.exe" mesh.vert -o mesh.vert.spv
"%VK_SDK_PATH%/Bin/glslc.exe" gotanda.frag -o mesh.frag.spv
"%VK_SDK_PATH%/Bin/glslc.exe" envmap.vert -o envmap.vert.spv
"%VK_SDK_PATH%/Bin/glslc.exe" envmap.frag -o envmap.frag.spv
"%VK_SDK_PATH%/Bin/glslc.exe" Instancing_Test.vert -o Instancing_Test.vert.spv
"%VK_SDK_PATH%/Bin/glslc.exe" boid.comp -o boid.comp.spv

pause