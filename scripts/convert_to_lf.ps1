$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

Get-ChildItem -Path $ProjectRoot -Recurse -File |
    Where-Object { 
        $_.Extension -in @('.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.cmake', '.sh') -or 
        $_.Name -eq 'CMakeLists.txt' 
    } |
    ForEach-Object { 
        $content = Get-Content $_.FullName -Raw
        if ($content -and $content -match "`r`n") {
            ($content -replace "`r`n", "`n") | Set-Content $_.FullName -NoNewline
            Write-Host "Converted: $($_.Name)"
        }
    }

Write-Host "Completed"