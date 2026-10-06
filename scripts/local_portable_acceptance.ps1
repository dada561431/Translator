param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$Probe,
    [Parameter(Mandatory=$true)][string]$Image,
    [switch]$ReuseQaCopy
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$qaRoot = [IO.Path]::GetFullPath((Join-Path $root '.cache/acceptance-qa'))
$copy = Join-Path $qaRoot ('Portable Test ' + [char]0x4e2d + [char]0x6587 + '/TranslatorPortable')
$reportRoot = Join-Path $qaRoot ('reports/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if (Test-Path -LiteralPath $copy) {
    if (-not $ReuseQaCopy) { throw 'QA destination already exists; inspect it, do not overwrite.' }
    if ((Get-FileHash -LiteralPath (Join-Path $copy 'runtime-manifest.json')).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $Package 'runtime-manifest.json')).Hash -or
        (Get-FileHash -LiteralPath (Join-Path $copy 'TranslatorPaddleProbe.exe')).Hash -ne
        (Get-FileHash -LiteralPath $Probe).Hash) { throw 'Existing QA copy is not this candidate/probe.' }
} else {
    New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $Package).Path -Destination $copy -Recurse
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $Probe).Path -Destination (Join-Path $copy 'TranslatorPaddleProbe.exe')
}
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
$imagePath = (Resolve-Path -LiteralPath $Image).Path

function Invoke-Isolated([string]$exe, [string[]]$arguments, [string]$reportName) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.WorkingDirectory = $reportRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Environment.Clear()
    foreach ($name in @('SYSTEMROOT','WINDIR','TEMP','TMP','USERPROFILE','LOCALAPPDATA','APPDATA','NUMBER_OF_PROCESSORS')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $info.Environment[$name] = $value }
    }
    $info.Environment['PATH'] = Join-Path $env:WINDIR 'System32'
    foreach ($name in @('PYTHONHOME','PYTHONPATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH')) {
        $info.Environment[$name] = 'Z:/invalid-translator-acceptance'
    }
    foreach ($arg in $arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        $process.Kill($true)
        throw 'QA process timed out'
    }
    $stdout.Result | Set-Content -LiteralPath (Join-Path $reportRoot ($reportName + '.stdout.txt'))
    $stderr.Result | Set-Content -LiteralPath (Join-Path $reportRoot ($reportName + '.stderr.txt'))
    if ($process.ExitCode -ne 0) { throw "QA process failed: $reportName, exit $($process.ExitCode)" }
}

# Only the newly created QA copy gets a temporary inherited deny-write ACL.
$originalAcl = Get-Acl -LiteralPath $copy
$acl = Get-Acl -LiteralPath $copy
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$rule = [Security.AccessControl.FileSystemAccessRule]::new($sid, 'Write', 'ContainerInherit,ObjectInherit', 'None', 'Deny')
$acl.AddAccessRule($rule)
try {
    Set-Acl -LiteralPath $copy -AclObject $acl
    $denied = $false
    try { [IO.File]::WriteAllText((Join-Path $copy 'ocr/models/qa-write-check.tmp'), 'must fail') }
    catch [UnauthorizedAccessException] { $denied = $true }
    if (-not $denied) { throw 'Deny-write test failed; do not label this a read-only test.' }
    Invoke-Isolated (Join-Path $copy 'Translator.exe') @('--self-check', '--report', (Join-Path $reportRoot 'readonly-self-check.json')) 'self-check'
    Invoke-Isolated (Join-Path $copy 'TranslatorPaddleProbe.exe') @('--live', (Join-Path $reportRoot 'lifecycle.json'), $imagePath, '--restart') 'lifecycle'
    $self = Get-Content -LiteralPath (Join-Path $reportRoot 'readonly-self-check.json') -Raw | ConvertFrom-Json
    $live = Get-Content -LiteralPath (Join-Path $reportRoot 'lifecycle.json') -Raw | ConvertFrom-Json
    if (-not $self.success -or $self.requests.Count -ne 20 -or $self.helper_pid -le 0 -or
        @($self.requests.pid | Select-Object -Unique).Count -ne 1 -or -not $self.qwindows -or
        -not $live.restart_same_pid -or -not $live.stop_halts_capture -or -not $live.original_visible) {
        throw 'Acceptance report contains a failure'
    }
    foreach ($pidValue in @($self.helper_pid, $live.samples[0].helper_pid)) {
        if (Get-Process -Id $pidValue -ErrorAction SilentlyContinue) { throw "Orphan helper $pidValue" }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $copy 'runtime-manifest.json') -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.files | Where-Object { $_.path.StartsWith('ocr/models/') }) {
        if ((Get-FileHash -LiteralPath (Join-Path $copy $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
            throw 'Bundled model changed'
        }
    }
    if (Get-ChildItem -LiteralPath $copy -Recurse -Filter '*.pyc' | Select-Object -First 1) { throw 'Bytecode written to installation' }
    [pscustomobject]@{ local_isolation_only=$true; read_only=$denied; model_hashes_unchanged=$true; no_pyc=$true; no_orphans=$true; self_check=$self; lifecycle=$live } |
        ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $reportRoot 'acceptance.json')
    Write-Output 'Local relocated Unicode/spaces/read-only acceptance passed. This is NOT clean-machine or physically offline acceptance.'
} finally {
    Set-Acl -LiteralPath $copy -AclObject $originalAcl
}
