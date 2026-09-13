param(
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$Reference,
    [ValidateRange(1, 22)][int]$Level = 1,
    [ValidateRange(3, 101)][int]$Trials = 3,
    [ValidateRange(1, 1000000)][int]$Iterations = 200,
    [string]$OutputDirectory = (Join-Path (Get-Location) 'out/corpus-matrix')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$size = 1MB
$root = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $root | Out-Null
$random = [Random]::new(0x5A17C0DE)
$corpora = [ordered]@{}

$zero = [byte[]]::new($size)
$corpora['zero-heavy'] = $zero
$text = [Text.Encoding]::ASCII.GetBytes('zstd++ deterministic repeated phrase; ')
$repeated = [byte[]]::new($size)
for ($i = 0; $i -lt $size; ++$i) { $repeated[$i] = $text[$i % $text.Length] }
$corpora['repeated-text'] = $repeated
$structured = [byte[]]::new($size)
for ($i = 0; $i -lt $size; ++$i) { $structured[$i] = [byte]((($i / 16) + ($i % 7)) -band 0xff) }
$corpora['structured'] = $structured
$randomBytes = [byte[]]::new($size)
$random.NextBytes($randomBytes)
$corpora['seeded-random'] = $randomBytes

$harness = Join-Path $PSScriptRoot 'paired_benchmark.ps1'
$rows = foreach ($entry in $corpora.GetEnumerator()) {
    $corpusPath = Join-Path $root ($entry.Key + '.bin')
    [IO.File]::WriteAllBytes($corpusPath, $entry.Value)
    $reportPath = Join-Path $root $entry.Key
    $result = & powershell -NoProfile -ExecutionPolicy Bypass -File $harness `
        -Candidate $Candidate -Reference $Reference -Level $Level -Corpus $corpusPath `
        -Iterations $Iterations -Trials $Trials -OutputDirectory $reportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Corpus '$($entry.Key)' failed parity or benchmarking (exit code $LASTEXITCODE)."
    }
    [pscustomobject]@{ Corpus = $entry.Key; Result = ($result -join ' ') }
}
$rows | Format-Table -AutoSize
