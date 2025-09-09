if (!(Get-Command clang-format -ErrorAction SilentlyContinue)) {
    Write-Host "Error: clang-format not found"
    Read-Host
    exit 1
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# Format all C++ files in the rtsp project directory, excluding build directories
Get-ChildItem -Path $ProjectRoot -Recurse -Include *.h,*.cpp | Where-Object {
    $_.FullName -notmatch '\\build\\'
} | ForEach-Object {
    Write-Host "Formatting: $($_.FullName)"
    clang-format -i $_.FullName
}

Write-Host "Code formatting completed!"