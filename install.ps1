& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'Continue'

    function Read-YesNo([string]$Question) {
        while ($true) {
            $answer = Read-Host "$Question [Y/N]"
            if ($null -eq $answer) {
                return $false
            }
            switch -Regex ($answer.Trim()) {
                '^(?i:y|yes)$' { return $true }
                '^(?i:n|no)?$' { return $false }
                default { Write-Host 'Please enter Y or N.' }
            }
        }
    }

    function Test-ArchiveIntegrity(
        [string]$Path,
        [long]$ExpectedSize,
        [string]$ExpectedDigest
    ) {
        if ($ExpectedSize -le 0 -or $ExpectedSize -gt 1GB -or
            $ExpectedDigest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
            throw 'The release does not provide a valid archive size and SHA-256 digest.'
        }
        $expectedHash = $Matches[1]
        if ((Get-Item -LiteralPath $Path).Length -ne $ExpectedSize) {
            throw 'The downloaded archive has an unexpected size.'
        }
        $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if (![string]::Equals($actualHash, $expectedHash, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The downloaded archive does not match the GitHub SHA-256 digest.'
        }
    }

    function Expand-QxGramArchive([string]$ArchivePath, [string]$Destination) {
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $allowed = @(
            'QxGram.exe',
            'Updater.exe',
            'modules/x64/d3d/d3dcompiler_47.dll',
            'LICENSE',
            'LEGAL'
        )
        $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
        try {
            if ($archive.Entries.Count -ne $allowed.Count) {
                throw 'The release archive has an unexpected file list.'
            }
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            $totalSize = 0L
            foreach ($entry in $archive.Entries) {
                if ($allowed -cnotcontains $entry.FullName -or !$seen.Add($entry.FullName)) {
                    throw 'The release archive contains an unexpected or duplicate path.'
                }
                if ($entry.Length -lt 0 -or $entry.Length -gt 1GB) {
                    throw 'A file in the release archive is too large.'
                }
                $totalSize += $entry.Length
                if ($totalSize -gt 2GB) {
                    throw 'The extracted release would be too large.'
                }
            }
            foreach ($name in $allowed) {
                $entry = $archive.GetEntry($name)
                if (!$entry) {
                    throw "The release archive is missing $name."
                }
                Write-Host "Extracting $name..."
                $target = Join-Path $Destination ($name.Replace('/', [IO.Path]::DirectorySeparatorChar))
                $parent = [IO.Path]::GetDirectoryName($target)
                [IO.Directory]::CreateDirectory($parent) | Out-Null
                $inputStream = $entry.Open()
                try {
                    $outputStream = [IO.File]::Open(
                        $target,
                        [IO.FileMode]::CreateNew,
                        [IO.FileAccess]::Write,
                        [IO.FileShare]::None)
                    try {
                        $inputStream.CopyTo($outputStream)
                    } finally {
                        $outputStream.Dispose()
                    }
                } finally {
                    $inputStream.Dispose()
                }
            }
        } finally {
            $archive.Dispose()
        }
    }

    function New-QxGramDesktopShortcut([string]$Executable, [string]$Desktop) {
        if (![IO.Directory]::Exists($Desktop)) {
            throw 'The desktop directory could not be found.'
        }
        $link = Join-Path $Desktop 'QxGram.lnk'
        $temporary = Join-Path $Desktop ('.qxgram-' + [guid]::NewGuid().ToString('N') + '.lnk')
        if (Test-Path -LiteralPath $link) {
            throw "The existing shortcut was not overwritten: $link"
        }
        try {
            $shell = New-Object -ComObject WScript.Shell
            $shortcut = $shell.CreateShortcut($temporary)
            $shortcut.TargetPath = $Executable
            $shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($Executable)
            $shortcut.IconLocation = $Executable + ',0'
            $shortcut.Description = 'QxGram'
            $shortcut.Save()
            [IO.File]::Move($temporary, $link)
        } finally {
            if ([IO.File]::Exists($temporary)) {
                [IO.File]::Delete($temporary)
            }
        }
        return $link
    }

    function Remove-InstallerStaging([string]$Stage) {
        $directories = @(
            $Stage,
            (Join-Path $Stage 'app'),
            (Join-Path $Stage 'app\modules'),
            (Join-Path $Stage 'app\modules\x64'),
            (Join-Path $Stage 'app\modules\x64\d3d'),
            (Join-Path $Stage 'app\TelegramForcePortable')
        )
        foreach ($directory in $directories) {
            $item = Get-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
            if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                Write-Warning "Temporary files were left in $Stage because a link was found."
                return
            }
        }
        $files = @(
            'release.zip',
            'app\QxGram.exe',
            'app\Updater.exe',
            'app\modules\x64\d3d\d3dcompiler_47.dll',
            'app\LICENSE',
            'app\LEGAL'
        )
        try {
            foreach ($name in $files) {
                $path = Join-Path $Stage $name
                if ([IO.File]::Exists($path)) {
                    [IO.File]::Delete($path)
                }
            }
            [array]::Reverse($directories)
            foreach ($directory in $directories) {
                if ([IO.Directory]::Exists($directory)) {
                    [IO.Directory]::Delete($directory, $false)
                }
            }
        } catch {
            Write-Warning "Some temporary files remain in $Stage. Existing data was not deleted."
        }
    }

    if ($env:OS -ne 'Windows_NT' -or ![Environment]::Is64BitOperatingSystem) {
        throw 'QxGram releases currently support 64-bit Windows.'
    }
    $architecture = if ($env:PROCESSOR_ARCHITEW6432) {
        $env:PROCESSOR_ARCHITEW6432
    } else {
        $env:PROCESSOR_ARCHITECTURE
    }
    if ($architecture -ne 'AMD64') {
        throw 'The current QxGram release requires Windows x64.'
    }
    if (!$env:LOCALAPPDATA -or ![IO.Path]::IsPathRooted($env:LOCALAPPDATA) -or
        ![IO.Directory]::Exists($env:LOCALAPPDATA)) {
        throw 'The LocalAppData directory is unavailable.'
    }

    $localAppData = [IO.Path]::GetFullPath($env:LOCALAPPDATA)
    $installation = Join-Path $localAppData 'QxGram'
    if (Test-Path -LiteralPath $installation) {
        throw "The installation path already exists and was not changed: $installation. Open the existing client to update it."
    }

    $stage = Join-Path $localAppData ('.qxgram-install-' + [guid]::NewGuid().ToString('N'))
    $createdStage = $false
    $oldProtocol = [Net.ServicePointManager]::SecurityProtocol
    try {
        [Net.ServicePointManager]::SecurityProtocol = $oldProtocol -bor [Net.SecurityProtocolType]::Tls12
        $headers = @{
            'User-Agent' = 'QxGram-Windows-Installer'
            'Accept' = 'application/vnd.github+json'
        }
        Write-Host 'Checking the latest QxGram release...'
        $release = Invoke-RestMethod -UseBasicParsing -Headers $headers -TimeoutSec 30 `
            -Uri 'https://api.github.com/repos/01Cifer/QxGram/releases/latest'
        if ($release.draft -or $release.prerelease -or
            $release.tag_name -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+)$') {
            throw 'GitHub did not return a supported stable QxGram release.'
        }
        $version = $Matches[1]
        $assetName = "QxGram-win64-$version.zip"
        $assets = @($release.assets | Where-Object { $_.name -ceq $assetName })
        if ($assets.Count -ne 1) {
            throw "The latest release does not contain exactly one $assetName."
        }
        $asset = $assets[0]
        $expectedUrl = "https://github.com/01Cifer/QxGram/releases/download/$($release.tag_name)/$assetName"
        if ($asset.browser_download_url -cne $expectedUrl -or $asset.state -ne 'uploaded') {
            throw 'The release archive has an unexpected download URL or state.'
        }
        $expectedSize = [long]$asset.size
        if ($expectedSize -le 0 -or $expectedSize -gt 1GB -or
            $asset.digest -notmatch '^sha256:[0-9a-fA-F]{64}$') {
            throw 'The release archive is missing a valid size or SHA-256 digest.'
        }

        New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
        $createdStage = $true
        $download = Join-Path $stage 'release.zip'
        $app = Join-Path $stage 'app'
        $request = [Net.HttpWebRequest]::Create($expectedUrl)
        $request.UserAgent = 'QxGram-Windows-Installer'
        $request.Timeout = 30000
        $request.ReadWriteTimeout = 60000
        $response = $request.GetResponse()
        try {
            if ($response.ResponseUri.Scheme -ne 'https' -or
                ($response.ContentLength -ge 0 -and $response.ContentLength -ne $expectedSize)) {
                throw 'The archive download returned an unexpected URL or size.'
            }
            $inputStream = $response.GetResponseStream()
            try {
                $outputStream = [IO.File]::Open(
                    $download,
                    [IO.FileMode]::CreateNew,
                    [IO.FileAccess]::Write,
                    [IO.FileShare]::None)
                try {
                    $buffer = [byte[]]::new(1MB)
                    $downloaded = 0L
                    $timer = [Diagnostics.Stopwatch]::StartNew()
                    $lastUpdate = -1000L
                    Write-Progress -Activity "Downloading QxGram $version" -Status '0%' -PercentComplete 0
                    while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                        $downloaded += $count
                        if ($downloaded -gt $expectedSize) {
                            throw 'The archive download exceeded its expected size.'
                        }
                        $outputStream.Write($buffer, 0, $count)
                        if ($timer.ElapsedMilliseconds - $lastUpdate -ge 150 -or $downloaded -eq $expectedSize) {
                            $percent = [int][math]::Floor(100.0 * $downloaded / $expectedSize)
                            $status = '{0:N1} / {1:N1} MiB ({2}%)' -f ($downloaded / 1MB), ($expectedSize / 1MB), $percent
                            Write-Progress -Activity "Downloading QxGram $version" -Status $status -PercentComplete $percent
                            $lastUpdate = $timer.ElapsedMilliseconds
                        }
                    }
                    if ($downloaded -ne $expectedSize) {
                        throw 'The archive download ended before all bytes arrived.'
                    }
                } finally {
                    $outputStream.Dispose()
                }
            } finally {
                $inputStream.Dispose()
            }
        } finally {
            $response.Dispose()
            Write-Progress -Activity "Downloading QxGram $version" -Completed
        }

        Write-Host 'Verifying the download...'
        Test-ArchiveIntegrity $download $expectedSize $asset.digest
        Expand-QxGramArchive $download $app
        $executable = Join-Path $app 'QxGram.exe'
        $fileVersion = [Version](Get-Item -LiteralPath $executable).VersionInfo.FileVersion
        $releaseVersion = [Version]$version
        if ($fileVersion.Major -ne $releaseVersion.Major -or
            $fileVersion.Minor -ne $releaseVersion.Minor -or
            $fileVersion.Build -ne $releaseVersion.Build) {
            throw 'The downloaded executable does not match the release version.'
        }
        [IO.Directory]::CreateDirectory((Join-Path $app 'TelegramForcePortable')) | Out-Null
        [IO.Directory]::Move($app, $installation)
    } finally {
        [Net.ServicePointManager]::SecurityProtocol = $oldProtocol
        if ($createdStage) {
            Remove-InstallerStaging $stage
        }
    }

    $installedExe = Join-Path $installation 'QxGram.exe'
    Write-Host "QxGram $version installed in $installation"
    Write-Host 'This installation uses its own separate account data.'
    if (Read-YesNo 'Create a desktop shortcut?') {
        try {
            $desktop = [Environment]::GetFolderPath('DesktopDirectory')
            $link = New-QxGramDesktopShortcut $installedExe $desktop
            Write-Host "Shortcut created: $link"
        } catch {
            Write-Warning "The shortcut was not created: $($_.Exception.Message)"
        }
    }
    if (Read-YesNo 'Launch QxGram now?') {
        try {
            Start-Process -FilePath $installedExe -WorkingDirectory $installation -ErrorAction Stop
        } catch {
            Write-Warning "QxGram could not be started: $($_.Exception.Message)"
        }
    }
}
