if (!(Get-Command clang-format -ErrorAction SilentlyContinue)) {
    Write-Host "Error: clang-format not found"
    Read-Host
    exit 1
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# Step 1: Normalize line endings to LF (same behavior as convert_to_lf.ps1)
Get-ChildItem -Path $ProjectRoot -Recurse -File |
    Where-Object {
        $_.FullName -notmatch '\\build\\' -and (
            $_.Extension -in @('.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.cmake', '.sh') -or
            $_.Name -eq 'CMakeLists.txt'
        )
    } |
    ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        if ($content -and $content -match "`r`n") {
            ($content -replace "`r`n", "`n") | Set-Content $_.FullName -NoNewline
            Write-Host "Converted: $($_.FullName)"
        }
    }

# Step 2: Format code (exclude build directories)
Get-ChildItem -Path $ProjectRoot -Recurse -File -Include *.h,*.cpp | Where-Object {
    $_.FullName -notmatch '\\build\\'
} | ForEach-Object {
    Write-Host "Formatting: $($_.FullName)"
    clang-format -i $_.FullName
}

Write-Host "Line-ending normalization (LF) then code formatting completed!"