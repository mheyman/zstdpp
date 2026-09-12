param(
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$Reference,
    [Parameter(Mandatory = $true)][ValidateRange(1, 22)][int]$Level,
    [string]$Corpus = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path 'out/build/msvc-release-eval/eval/results/corpus.bin'),
    [ValidateRange(1, 1000000)][int]$Iterations = 1000,
    [ValidateRange(3, 101)][int]$Trials = 9,
    [string]$OutputDirectory = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path 'out/paired-benchmark'),
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingFile([string]$Path, [string]$Description) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Description is not a file: $resolved"
    }
    return $resolved.Path
}

function Read-Metrics([string]$Path) {
    $fields = @(Get-Content -LiteralPath $Path -Raw).Trim().Split(',')
    if ($fields.Count -ne 5) { throw "Invalid worker metrics: $Path" }
    $elapsed = [uint64]$fields[0]
    $inputBytes = [uint64]$fields[2]
    $outputBytes = [uint64]$fields[3]
    $iterationsWritten = [uint64]$fields[4]
    if ($elapsed -eq 0 -or $inputBytes -eq 0 -or $iterationsWritten -eq 0) {
        throw "Invalid zero-valued worker metrics: $Path"
    }
    [pscustomobject]@{
        ElapsedNanoseconds = $elapsed
        InputBytes = $inputBytes
        OutputBytes = $outputBytes
        Iterations = $iterationsWritten
        MiBPerSecond = ([double]$inputBytes * [double]$iterationsWritten * 1e9 /
            [double]$elapsed / 1MB)
    }
}

function Invoke-Worker([string]$Executable, [string]$Frame, [string]$Metrics, [string]$CorpusPath) {
    & $Executable @($CorpusPath, $Frame, $Iterations, $Metrics)
    if ($LASTEXITCODE -ne 0) { throw "Worker failed: $Executable" }
    return Read-Metrics $Metrics
}

$candidatePath = Resolve-ExistingFile $Candidate 'Candidate executable'
$referencePath = Resolve-ExistingFile $Reference 'Reference executable'
$corpusPath = Resolve-ExistingFile $Corpus 'Corpus'
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$rows = @()
foreach ($trial in 1..$Trials) {
    $order = if (($trial % 2) -eq 1) { @('Candidate', 'Reference') } else { @('Reference', 'Candidate') }
    $metrics = @{}
    $frames = @{}
    foreach ($kind in $order) {
        $executable = if ($kind -eq 'Candidate') { $candidatePath } else { $referencePath }
        $frame = Join-Path $outputPath "$kind-$Level-$trial.zst"
        $metric = Join-Path $outputPath "$kind-$Level-$trial.csv"
        $metrics[$kind] = Invoke-Worker $executable $frame $metric $corpusPath
        $frames[$kind] = $frame
    }
    if ($metrics.Candidate.InputBytes -ne $metrics.Reference.InputBytes -or
        $metrics.Candidate.OutputBytes -ne $metrics.Reference.OutputBytes -or
        -not ((Get-FileHash -LiteralPath $frames.Candidate).Hash -eq
            (Get-FileHash -LiteralPath $frames.Reference).Hash)) {
        throw "Candidate/reference output mismatch at trial $trial"
    }
    $rows += [pscustomobject]@{
        Trial = $trial
        CandidateMiBPerSecond = [math]::Round($metrics.Candidate.MiBPerSecond, 2)
        ReferenceMiBPerSecond = [math]::Round($metrics.Reference.MiBPerSecond, 2)
        GainPercent = [math]::Round(($metrics.Candidate.MiBPerSecond /
            $metrics.Reference.MiBPerSecond - 1.0) * 100.0, 2)
        CandidateElapsedNanoseconds = $metrics.Candidate.ElapsedNanoseconds
        ReferenceElapsedNanoseconds = $metrics.Reference.ElapsedNanoseconds
        FrameBytes = $metrics.Candidate.OutputBytes
    }
}

$reportPath = Join-Path $outputPath "paired-level-$Level.csv"
$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation
$gains = @($rows | Sort-Object GainPercent | Select-Object -ExpandProperty GainPercent)
$medianIndex = [int][math]::Floor($gains.Count / 2)
$summary = [pscustomobject]@{
    Level = $Level
    Trials = $Trials
    Iterations = $Iterations
    CandidateMedianMiBPerSecond = ($rows | Sort-Object CandidateMiBPerSecond | Select-Object -ExpandProperty CandidateMiBPerSecond)[$medianIndex]
    ReferenceMedianMiBPerSecond = ($rows | Sort-Object ReferenceMiBPerSecond | Select-Object -ExpandProperty ReferenceMiBPerSecond)[$medianIndex]
    MedianGainPercent = $gains[$medianIndex]
    GainRange = "$($gains[0]) to $($gains[-1])"
    FrameBytes = $rows[0].FrameBytes
    Report = $reportPath
}
$summary | Format-List

if (-not $KeepArtifacts) {
    foreach ($row in $rows) {
        foreach ($kind in @('Candidate', 'Reference')) {
            Remove-Item -LiteralPath (Join-Path $outputPath "$kind-$Level-$($row.Trial).zst") -Force
            Remove-Item -LiteralPath (Join-Path $outputPath "$kind-$Level-$($row.Trial).csv") -Force
        }
    }
}
