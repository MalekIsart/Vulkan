%VK_SDK_PATH%/Bin32/glslc.exe mesh.vert -o mesh.vert.spv
%VK_SDK_PATH%/Bin32/glslc.exe ggx_transparent.frag -o mesh.transparent.frag.spv
%VK_SDK_PATH%/Bin32/glslc.exe ggx_cutout.frag -o mesh.cutout.frag.spv
%VK_SDK_PATH%/Bin32/glslc.exe ggx.frag -o mesh.frag.spv
pause