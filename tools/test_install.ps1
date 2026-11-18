$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Name) {
    if (!$Condition) {
        throw "FAILED: $Name"
    }
    Write-Host "PASS: $Name"
}

function Assert-Throws([scriptblock]$Action, [string]$Name) {
    $failed = $false
    try {
        & $Action
    } catch {
        $failed = $true
    }
    Assert-True $failed $Name
}

function New-TestArchive([string]$Path, [string[]]$Names) {
    $archive = [IO.Compression.ZipFile]::Open(
        $Path,
        [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $Names) {
            $entry = $archive.CreateEntry($name)
            $stream = $entry.Open()
            try {
                $bytes = [Text.Encoding]::UTF8.GetBytes("fixture:$name")
                $stream.Write($bytes, 0, $bytes.Length)
            } finally {
                $stream.Dispose()
            }
        }
    } finally {
        $archive.Dispose()
    }
}

$installer = Join-Path (Split-Path $PSScriptRoot -Parent) 'install.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $installer,
    [ref]$tokens,
    [ref]$errors)
Assert-True ($errors.Count -eq 0) 'installer parses'

$helpers = @(
    'Read-YesNo',
    'Test-ArchiveIntegrity',
    'Expand-QxGramArchive',
    'New-QxGramDesktopShortcut',
    'Remove-InstallerStaging'
)
foreach ($name in $helpers) {
    $matches = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $name
    }, $true))
    Assert-True ($matches.Count -eq 1) "helper exists: $name"
    Invoke-Expression $matches[0].Extent.Text
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Join-Path $env:TEMP ('qx-installer-unit-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -ErrorAction Stop | Out-Null
$expected = @(
    'QxGram.exe',
    'Updater.exe',
    'modules/x64/d3d/d3dcompiler_47.dll',
    'LICENSE',
    'LEGAL'
)
$archive = Join-Path $root 'good.zip'
New-TestArchive $archive $expected
$size = (Get-Item -LiteralPath $archive).Length
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
Test-ArchiveIntegrity $archive $size ("sha256:" + $hash)
Write-Host 'PASS: valid archive size and digest'
Assert-Throws { Test-ArchiveIntegrity $archive ($size + 1) ("sha256:" + $hash) } 'wrong archive size rejected'
Assert-Throws { Test-ArchiveIntegrity $archive $size ('sha256:' + ('0' * 64)) } 'wrong archive digest rejected'
Assert-Throws { Test-ArchiveIntegrity $archive $size 'missing' } 'missing archive digest rejected'

$unpacked = Join-Path $root 'unpacked'
Expand-QxGramArchive $archive $unpacked
foreach ($name in $expected) {
    $path = Join-Path $unpacked ($name.Replace('/', [IO.Path]::DirectorySeparatorChar))
    Assert-True ([IO.File]::ReadAllText($path) -eq "fixture:$name") "correctly extracted $name"
}

$traversal = Join-Path $root 'traversal.zip'
New-TestArchive $traversal @('../escape.txt', 'Updater.exe',
    'modules/x64/d3d/d3dcompiler_47.dll', 'LICENSE', 'LEGAL')
Assert-Throws { Expand-QxGramArchive $traversal (Join-Path $root 'bad-unpacked') } 'path traversal rejected'
Assert-True (!(Test-Path -LiteralPath (Join-Path $root 'escape.txt'))) 'path traversal wrote nothing outside'

$duplicate = Join-Path $root 'duplicate.zip'
New-TestArchive $duplicate @('QxGram.exe', 'QxGram.exe',
    'modules/x64/d3d/d3dcompiler_47.dll', 'LICENSE', 'LEGAL')
Assert-Throws { Expand-QxGramArchive $duplicate (Join-Path $root 'duplicate-unpacked') } 'duplicate archive paths rejected'

$desktop = Join-Path $root 'Desktop'
New-Item -ItemType Directory -Path $desktop | Out-Null
$exe = Join-Path $unpacked 'QxGram.exe'
$link = New-QxGramDesktopShortcut $exe $desktop
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($link)
Assert-True ($shortcut.TargetPath -eq $exe) 'shortcut points to installed executable'
Assert-True ($shortcut.WorkingDirectory -eq $unpacked) 'shortcut keeps installation working directory'
$linkHash = (Get-FileHash -LiteralPath $link -Algorithm SHA256).Hash
Assert-Throws { New-QxGramDesktopShortcut $exe $desktop } 'existing shortcut is not overwritten'
Assert-True ((Get-FileHash -LiteralPath $link -Algorithm SHA256).Hash -eq $linkHash) 'existing shortcut remains unchanged'

$stage = Join-Path $root 'staging'
$data = Join-Path $stage 'app\TelegramForcePortable\tdata'
New-Item -ItemType Directory -Path $data -Force | Out-Null
$sentinel = Join-Path $data 'synthetic-sentinel'
[IO.File]::WriteAllText($sentinel, 'do not delete')
Remove-InstallerStaging $stage
Assert-True ([IO.File]::ReadAllText($sentinel) -eq 'do not delete') 'temporary cleanup preserves unexpected account data'

$script:Answers = @('invalid', 'Y', 'N')
$script:AnswerIndex = 0
function Read-Host([string]$Prompt) {
    $result = $script:Answers[$script:AnswerIndex]
    $script:AnswerIndex++
    return $result
}
Assert-True (Read-YesNo 'Test') 'yes answer accepted after invalid input'
Assert-True (!(Read-YesNo 'Test')) 'no answer accepted'

Write-Host "INSTALLER_UNIT_TESTS=PASS fixture=$root"
