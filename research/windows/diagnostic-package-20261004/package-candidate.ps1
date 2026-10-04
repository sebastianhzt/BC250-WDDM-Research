param(
    [Parameter(Mandatory=$true)][string]$CompileOutput,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ReviewedSysSha256,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{40}$')][string]$ExpectedThumbprint
)
# Test-sign an exact POSTBUILD-reviewed candidate. Never install/load, change
# certificate stores, create keys, alter BCD/registry or contact a device.
$ErrorActionPreference='Stop'
$diagBase=(Resolve-Path -LiteralPath $PSScriptRoot).Path
$diagRoot=(Resolve-Path -LiteralPath (Join-Path $diagBase '../../..')).Path
$diagOut=(Resolve-Path -LiteralPath (Join-Path $diagRoot 'output')).Path
$compiled=(Resolve-Path -LiteralPath $CompileOutput).Path
if ((Split-Path -Parent $compiled) -ine $diagOut -or
    (Split-Path -Leaf $compiled) -notmatch '^diagnostic-build21-[A-Za-z0-9_-]+$') {
    throw 'CompileOutput must be a direct diagnostic-build21-* folder in this repository output.'
}
$report=Get-Content -LiteralPath (Join-Path $compiled 'RESULT.json') -Raw | ConvertFrom-Json
$unsigned=Join-Path $compiled 'atikmdag-UNSIGNED.sys'
if ($report.build_id -ne 21 -or $report.driver_loaded -ne $false -or
    $report.driver_installed -ne $false -or $report.gpu_dma_enabled -ne $false -or
    $report.driver_signed -ne $false -or $report.installable_package -ne $false -or
    @($report.stages).Count -ne 19 -or
    @($report.artifact_sha256.PSObject.Properties).Count -ne 12 -or
    @($report.stages | Where-Object returncode -ne 0).Count -ne 0 -or
    (Get-FileHash -LiteralPath $unsigned -Algorithm SHA256).Hash -ine $ReviewedSysSha256 -or
    $report.artifact_sha256.'atikmdag-UNSIGNED.sys' -ine $ReviewedSysSha256) {
    throw 'Compile result or reviewed unsigned SYS mismatch.'
}
$provenance=Get-Content -LiteralPath (Join-Path $diagBase 'provenance.json') -Raw | ConvertFrom-Json
$snapshotProperties=@($report.snapshot_sha256.PSObject.Properties)
$snapshotFiles=@(Get-ChildItem -LiteralPath $diagBase -Recurse -File)
if ($snapshotProperties.Count -ne 19 -or $snapshotFiles.Count -ne 19) { throw 'Unexpected reviewed snapshot count.' }
foreach($property in $snapshotProperties) {
    $reviewedFile=Join-Path $diagBase $property.Name
    if ((Get-FileHash -LiteralPath $reviewedFile -Algorithm SHA256).Hash -ine $property.Value) {
        throw "Reviewed snapshot changed: $($property.Name)"
    }
}
foreach($property in $provenance.candidate_source_sha256.PSObject.Properties) {
    $source=Join-Path $diagBase ('source/'+$property.Name)
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ine $property.Value -or
        $report.source_sha256.($property.Name) -ine $property.Value) {
        throw "Candidate source changed: $($property.Name)"
    }
}
$cert=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$ExpectedThumbprint)
if (-not $cert.HasPrivateKey -or $cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -lt (Get-Date)) {
    throw 'Expected certificate must have a private key and be currently valid.'
}
$tools=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0'
$sign=Join-Path $tools 'x64/signtool.exe'
$inf2cat=Join-Path $tools 'x86/Inf2Cat.exe'
if (-not(Test-Path -LiteralPath $sign -PathType Leaf) -or
    -not(Test-Path -LiteralPath $inf2cat -PathType Leaf)) { throw 'WDK signing/catalog tools absent.' }
$name='candidate-build21-'+(Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$session=Join-Path $diagOut $name
New-Item -ItemType Directory -Path $session -ErrorAction Stop | Out-Null
$package=Join-Path $session 'driver'
New-Item -ItemType Directory -Path $package -ErrorAction Stop | Out-Null
$toolOutput=Join-Path $session 'tools'
New-Item -ItemType Directory -Path $toolOutput -ErrorAction Stop | Out-Null
Copy-Item -LiteralPath $unsigned -Destination (Join-Path $package 'atikmdag.sys')
Copy-Item -LiteralPath (Join-Path $diagBase 'source/inf/amdbc250_dream.inf') -Destination $package
if ((Get-FileHash -LiteralPath (Join-Path $package 'atikmdag.sys') -Algorithm SHA256).Hash -ine $ReviewedSysSha256 -or
    (Get-FileHash -LiteralPath (Join-Path $package 'amdbc250_dream.inf') -Algorithm SHA256).Hash -ine
        $provenance.candidate_source_sha256.'inf/amdbc250_dream.inf') { throw 'Staging copy mismatch.' }
# Archive logs for each native command; no passwords/PFX are passed or exported.
function Invoke-DiagTool([string]$stage,[string]$exe,[string[]]$Arguments) {
    @($exe)+$Arguments | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $session ($stage+'.command.json'))
    & $exe @Arguments 1> (Join-Path $session ($stage+'.stdout')) 2> (Join-Path $session ($stage+'.stderr'))
    $exitCode=$LASTEXITCODE
    $exitCode | Set-Content -LiteralPath (Join-Path $session ($stage+'.exit-code'))
    if ($exitCode -ne 0) { throw "Package stage $stage failed: $exitCode. Candidate is NOT accepted." }
}
Invoke-DiagTool 'sign-sys' $sign @('sign','/s','My','/sha1',$ExpectedThumbprint,'/fd','SHA256',(Join-Path $package 'atikmdag.sys'))
Invoke-DiagTool 'inf2cat' $inf2cat @("/driver:$package",'/os:10_X64','/uselocaltime')
$cat=Join-Path $package 'amdbc250_dream.cat'
if (-not(Test-Path -LiteralPath $cat -PathType Leaf)) { throw 'Catalog not produced.' }
Invoke-DiagTool 'sign-cat' $sign @('sign','/s','My','/sha1',$ExpectedThumbprint,'/fd','SHA256',$cat)
foreach($file in @('atikmdag.sys','amdbc250_dream.cat')) {
    $target=Join-Path $package $file
    Invoke-DiagTool ('verify-'+$file) $sign @('verify','/pa','/v',$target)
    $signature=Get-AuthenticodeSignature -LiteralPath $target
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ine $ExpectedThumbprint) {
        throw 'Unexpected signer or invalid signature.'
    }
}
foreach($file in @('atikmdag.sys','amdbc250_dream.inf')) {
    Invoke-DiagTool ('catalog-member-'+$file) $sign @('verify','/pa','/v','/c',$cat,(Join-Path $package $file))
}
foreach($tool in @('pnp-binding-preflight','w2p-preflight','resource-preflight',
                  'pci-config-preflight','diagnostic-access-preflight')) {
    $toolName=$tool+'.exe'
    $original=Join-Path $compiled $toolName
    if ((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ine $report.artifact_sha256.($toolName)) {
        throw 'Compiled diagnostic tool hash mismatch.'
    }
    Copy-Item -LiteralPath $original -Destination $toolOutput
}
$hashes=@{}
foreach($file in (Get-ChildItem -LiteralPath $package,$toolOutput -File)) {
    $hashes[$file.FullName.Substring($session.Length+1).Replace('\','/')]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$acceptance=@{
    build_id=21; reviewed_unsigned_sys_sha256=$ReviewedSysSha256.ToLowerInvariant();
    signer_thumbprint=$ExpectedThumbprint.ToUpperInvariant(); files_sha256=$hashes;
    test_signature_and_catalog_verified=$true; install_approved=$false;
    packaging_tools_sha256=@{
        signtool=(Get-FileHash -LiteralPath $sign -Algorithm SHA256).Hash;
        inf2cat=(Get-FileHash -LiteralPath $inf2cat -Algorithm SHA256).Hash
    };
    installed=$false; hardware_tested=$false; screen_output_validated=$false;
    pnp_runtime_validated=$false; accelerated_driver=$false;
    warning='Test-signed diagnostic candidate only. Not a production Microsoft signature or recommendation to replace Build11.'
}
$acceptance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $session 'CANDIDATE.json') -Encoding utf8
Write-Output "TEST-SIGNED DIAGNOSTIC CANDIDATE: $session"
Write-Output 'Nothing installed. Runtime/screen/recovery approval still required.'
