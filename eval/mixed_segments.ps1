param(
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$Reference,
    [ValidateRange(1, 22)][int]$Level = 3,
    [ValidateRange(3, 101)][int]$Trials = 3,
    [ValidateRange(1, 1000000)][int]$Iterations = 200,
    [string]$Corpus = (Join-Path (Get-Location) 'out/build/msvc-release-eval/eval/results/corpus.bin'),
    [string]$OutputDirectory = (Join-Path (Get-Location) 'out/mixed-segments')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($Corpus))
if ($bytes.Length -eq 0 -or ($bytes.Length % 4) -ne 0) {
    throw 'Mixed corpus must be non-empty and divisible into four equal segments.'
}
$root = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $root | Out-Null
$segmentSize = [int]($bytes.Length / 4)
$names = @('zero-heavy', 'repeated-text', 'structured', 'seeded-random')
$harness = Join-Path $PSScriptRoot 'paired_benchmark.ps1'
for ($index = 0; $index -lt 4; ++$index) {
    $segment = [byte[]]::new($segmentSize)
    [Array]::Copy($bytes, $index * $segmentSize, $segment, 0, $segmentSize)
    # Evaluation workers are compiled for a 1 MiB corpus. Repeat the isolated
    # quarter so each run retains one regime while honoring that contract.
    $workerInput = [byte[]]::new($bytes.Length)
    for ($repeat = 0; $repeat -lt 4; ++$repeat) {
        [Array]::Copy($segment, 0, $workerInput, $repeat * $segmentSize, $segmentSize)
    }
    $segmentPath = Join-Path $root ($names[$index] + '.bin')
    [IO.File]::WriteAllBytes($segmentPath, $workerInput)
    $reportPath = Join-Path $root $names[$index]
    & powershell -NoProfile -ExecutionPolicy Bypass -File $harness `
        -Candidate $Candidate -Reference $Reference -Level $Level -Corpus $segmentPath `
        -Iterations $Iterations -Trials $Trials -OutputDirectory $reportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Segment '$($names[$index])' failed parity or benchmarking."
    }
}
Write-Output "Reports written to $root"
