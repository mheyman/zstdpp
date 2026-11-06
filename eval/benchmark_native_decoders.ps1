# Benchmark native Windows decoder workers on the five-input reference-frame matrix.
# Each worker must accept: encoded expected iterations output.csv.
param(
 [Parameter(Mandatory)][string]$CppWorker,
 [Parameter(Mandatory)][string]$ReferenceWorker,
 [Parameter(Mandatory)][string]$MatrixDirectory,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [ValidateRange(0,63)][int]$Cpu=2
)
$ErrorActionPreference='Stop'
$matrix=(Resolve-Path -LiteralPath $MatrixDirectory).Path
$workers=@{cpp=(Resolve-Path -LiteralPath $CppWorker).Path;reference=(Resolve-Path -LiteralPath $ReferenceWorker).Path}
if ((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0) { throw 'OutputDirectory must be empty' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$outputRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$rows=[Collections.Generic.List[object]]::new()
$provenance=@{workers=@{};frames=@{};screenIterations=1000;screenTrials=3;confirmationIterations=30000;confirmationTrials=6;confirmedCases=@("structured-16","structured-18","mixed-3");cpu=$Cpu}
foreach($name in @('cpp','reference')) { $provenance.workers[$name]=@{path=$workers[$name];sha256=(Get-FileHash $workers[$name]).Hash} }
foreach($shape in @('zero','repeated','structured','random','mixed')) {
 $destination=Join-Path $outputRoot "matrix/$shape"
 New-Item -ItemType Directory -Force $destination | Out-Null
 $source=Join-Path $matrix $shape
 $expected=Join-Path $source 'corpus.bin'
 if ((Get-Item -LiteralPath $expected).Length -ne 1MB) { throw 'Matrix inputs must be 1 MiB' }
 $provenance.frames["$shape-corpus"]=(Get-FileHash $expected).Hash
 foreach($level in 1..22) {
  $frame=Join-Path $source "reference_l${level}.zst"
  $provenance.frames["$shape-$level"]=(Get-FileHash $frame).Hash
  $iterations=1000; $trialCount=3
  if (($shape -eq 'structured' -and $level -in @(16,18)) -or ($shape -eq 'mixed' -and $level -eq 3)) { $iterations=30000; $trialCount=6 }
  foreach($trial in 1..$trialCount) {
   $order=if(($trial+$level)%2){@('cpp','reference')}else{@('reference','cpp')}
   foreach($name in $order) {
    $report=Join-Path $destination "l${level}-${name}-${trial}.csv"
    $info=[Diagnostics.ProcessStartInfo]::new($workers[$name]); $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    foreach($argument in @($frame,$expected,"$iterations",$report)){ $info.ArgumentList.Add($argument) }
    $process=[Diagnostics.Process]::Start($info); $process.ProcessorAffinity=[IntPtr]([long]1 -shl $Cpu); $process.WaitForExit()
    if($process.ExitCode-ne 0){throw "Worker failed: $shape $level $name"}
    $v=(Get-Content $report -Raw).Trim().Split(','); if([long]$v[3]-ne (Get-Item $expected).Length -or [long]$v[4]-ne $iterations){throw 'Invalid metrics'}
    $rows.Add([pscustomobject]@{Input=$shape;Level=$level;Trial=$trial;Implementation=$name;ElapsedNs=[long]$v[0];PeakBytes=[long]$v[1];CodeBytes=(Get-Item $workers[$name]).Length;DecodedBytes=[long]$v[3];Iterations=[long]$v[4]})
    $process.Dispose()
   }
  }
 }
 $rows | Export-Csv (Join-Path $outputRoot 'matrix-trials.csv') -NoTypeInformation
 Write-Output "Completed native $shape matrix"
}
$provenance|ConvertTo-Json -Depth 5|Set-Content (Join-Path $outputRoot 'matrix-provenance.json')