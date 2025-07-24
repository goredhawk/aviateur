Remove-Item -Path "generated" -Recurse

New-Item -Path "generated" -ItemType Directory

New-Variable -Name "GLSLC" -Visibility Public -Value "$env:VULKAN_SDK/Bin/glslc.exe"

# Compile shaders.
& $GLSLC yuv.vert -o generated/yuv_vert.spv
& $GLSLC yuv.frag -o generated/yuv_frag.spv

Copy-Item "yuv.frag" "generated"
Copy-Item "yuv.vert" "generated"

Set-Location "generated"

# Generate headers.
python ../convert_files_to_header.py vert
python ../convert_files_to_header.py frag
python ../convert_files_to_header.py spv

# Remove intermediate files.
Get-ChildItem -Recurse -File | Where { ($_.Extension -ne ".h") } | Remove-Item

# Wait for input.
Write-Host "All jobs finished."
$Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
