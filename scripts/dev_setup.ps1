$ErrorActionPreference = "Stop"

Write-Host "Installing OmniDrop Python worker dependencies..."
python -m pip install -r "$PSScriptRoot\..\python\requirements.txt"

Write-Host "Configuring CMake..."
cmake -S "$PSScriptRoot\.." -B "$PSScriptRoot\..\build" -DCMAKE_BUILD_TYPE=Release

Write-Host "Building OmniDrop..."
cmake --build "$PSScriptRoot\..\build" --config Release --parallel

Write-Host "Running C++ tests..."
ctest --test-dir "$PSScriptRoot\..\build" -C Release --output-on-failure

Write-Host "Running Python worker tests..."
python -m unittest discover -s "$PSScriptRoot\..\tests" -p "test_worker.py"
