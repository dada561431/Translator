param([string]$OutputDirectory = '.cache/phase8d1a/tts')
$ErrorActionPreference = 'Stop'
# Explicit offline QA preparation. No production TTS, downloads, or network.
$root = (Resolve-Path '.cache').Path
$target = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputDirectory))
if (-not $target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'QA corpus output must stay under .cache'
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
$corpus = Get-Content -LiteralPath 'tools/streaming_asr/corpus-phase8d1a.json' -Raw -Encoding UTF8 | ConvertFrom-Json
Add-Type -AssemblyName System.Speech
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
    foreach ($entry in $corpus) {
        $voice = if ($entry.language -eq 'zh') { 'Microsoft Huihui Desktop' } else { 'Microsoft Zira Desktop' }
        $speaker.SelectVoice($voice)
        $path = Join-Path $target ($entry.id + '.wav')
        if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing corpus: $path" }
        $speaker.SetOutputToWaveFile($path, $format)
        $speaker.Speak($entry.reference)
        $speaker.SetOutputToNull()
        Write-Output "$($entry.id): $voice -> $path"
    }
} finally { $speaker.Dispose() }
