param([string]$Python = "python")
$ErrorActionPreference = 'Stop'
$testCertificates = @()
try {
    foreach ($index in 0, 1) {
        $certificate = New-SelfSignedCertificate -Type Custom -Subject "CN=neo-tls-live-test-$([guid]::NewGuid())" -Provider 'Microsoft Software Key Storage Provider' -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy NonExportable -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddHours(1) -KeyUsage DigitalSignature,KeyEncipherment -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.1,1.3.6.1.5.5.7.3.2')
        $testCertificates += $certificate
    }
    $pins = @{
        server = $testCertificates[0].GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256).ToLowerInvariant()
        client = $testCertificates[1].GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256).ToLowerInvariant()
    }
    $pins | ConvertTo-Json | Set-Content -LiteralPath 'build/x86_64/Windows/tls-live-public-pins.json' -Encoding utf8
    & $Python neo-vm/platform/win32/tests/tls_live.py build/x86_64/Windows/tls-live-public-pins.json
    if ($LASTEXITCODE -ne 0) { throw 'Live mutual TLS check failed.' }
    & $Python neo-vm/platform/win32/tests/network_restore.py build/x86_64/Windows/tls-live-public-pins.json
    if ($LASTEXITCODE -ne 0) { throw 'Full OS network restore check failed.' }
} finally {
    foreach ($certificate in $testCertificates) {
        Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -DeleteKey -Force
    }
}
