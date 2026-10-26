param(
    [string]$PackageRoot = 'D:\devtools\leafyrino-usercard-stage1\test-package',
    [string]$ResultsRoot = 'D:\devtools\leafyrino-usercard-stage1\headless-results'
)

$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path -LiteralPath $PackageRoot).Path
$resultsPath = [IO.Path]::GetFullPath($ResultsRoot)
foreach ($testPath in @($packagePath, $resultsPath)) {
    if (-not $testPath.StartsWith('D:\devtools\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Offline checks are restricted to independent test paths under D:\devtools.'
    }
}
if ((Get-Item -LiteralPath $packagePath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'The test package must be a real directory, not a junction.'
}
$null = New-Item -ItemType Directory -Path $resultsPath -Force
$previewCases = @(
    @{ name = 'preview-dpi100'; executable = 'Leafyrino-usercard-preview.exe'; scale = '1'; checks = 14; flags = '--case a --width 960 --height 670 --scripted-checks --quit-after 1600' },
    @{ name = 'preview-dpi150'; executable = 'Leafyrino-usercard-preview.exe'; scale = '1.5'; checks = 14; flags = '--case a --width 960 --height 670 --scripted-checks --quit-after 1600' },
    @{ name = 'preview-autoclose-dpi100'; executable = 'Leafyrino-usercard-preview.exe'; scale = '1'; checks = 16; flags = '--auto-close --case a --width 960 --height 670 --scripted-checks --quit-after 1600' },
    @{ name = 'preview-autoclose-dpi150'; executable = 'Leafyrino-usercard-preview.exe'; scale = '1.5'; checks = 16; flags = '--auto-close --case a --width 800 --height 540 --scripted-checks --quit-after 1600' },
    @{ name = 'baseline-dpi100'; executable = 'Leafyrino-usercard-baseline-preview.exe'; scale = '1'; checks = 0; flags = '--case a --width 960 --height 670 --quit-after 1300' },
    @{ name = 'baseline-dpi150'; executable = 'Leafyrino-usercard-baseline-preview.exe'; scale = '1.5'; checks = 0; flags = '--case a --width 600 --height 760 --quit-after 1300' },
    @{ name = 'pending-shutdown'; executable = 'Leafyrino-usercard-preview.exe'; scale = '1'; checks = 0; flags = '--case a --width 600 --height 760 --pending-shutdown --quit-after 1000' }
)
$testResults = @()
foreach ($previewCase in $previewCases) {
    $executablePath = Join-Path $packagePath $previewCase.executable
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw ('Test binary is missing: ' + $executablePath)
    }
    $reportPath = Join-Path $resultsPath ($previewCase.name + '.json')
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $executablePath
    $startInfo.WorkingDirectory = $packagePath
    $startInfo.Arguments = $previewCase.flags + ' --report "' + $reportPath + '"'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
    $startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
    $startInfo.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
    $startInfo.EnvironmentVariables['QT_SCALE_FACTOR'] = $previewCase.scale
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($startInfo)
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(20000)) {
        # A hang is a failed test, never a reason to pretend normal shutdown
        # succeeded. Leave the exact PID in the failure for manual diagnosis.
        throw ('Offline test did not exit: ' + $previewCase.name + ', PID ' + $process.Id)
    }
    $timer.Stop()
    $stdout = $outputTask.GetAwaiter().GetResult()
    $stderr = $errorTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $resultsPath ($previewCase.name + '.stdout.log')), $stdout)
    [IO.File]::WriteAllText((Join-Path $resultsPath ($previewCase.name + '.stderr.log')), $stderr)
    $report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $allChecks = @($report.checks)
    $failedChecks = @($allChecks | Where-Object { -not $_.ok })
    $expectedRatio = [double]::Parse($previewCase.scale, [Globalization.CultureInfo]::InvariantCulture)
    $valid = $process.ExitCode -eq 0 -and
        $allChecks.Count -eq $previewCase.checks -and $failedChecks.Count -eq 0 -and
        -not $report.networkManagerInitialized -and -not $report.accountsLoaded -and
        [Math]::Abs([double]$report.devicePixelRatio - $expectedRatio) -lt 0.01
    $result = [PSCustomObject]@{
        name = $previewCase.name
        ok = $valid
        pid = $process.Id
        exit = $process.ExitCode
        checks = $allChecks.Count
        failedChecks = $failedChecks.Count
        devicePixelRatio = $report.devicePixelRatio
        totalProcessMs = $timer.ElapsedMilliseconds
        report = $reportPath
    }
    $testResults += $result
    $process.Dispose()
    $result | Format-Table -AutoSize
}
$testResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $resultsPath 'SUMMARY.json') -Encoding utf8
if (@($testResults | Where-Object { -not $_.ok }).Count -gt 0) {
    throw 'One or more offline preview checks failed. See SUMMARY.json and the individual reports.'
}
