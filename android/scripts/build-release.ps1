param(
    [switch]$SkipTests
)

$required = @(
    "AI_INTERVIEW_API_BASE_URL",
    "AI_INTERVIEW_VERSION_CODE",
    "AI_INTERVIEW_VERSION_NAME",
    "AI_INTERVIEW_RELEASE_STORE_FILE",
    "AI_INTERVIEW_RELEASE_STORE_PASSWORD",
    "AI_INTERVIEW_RELEASE_KEY_ALIAS",
    "AI_INTERVIEW_RELEASE_KEY_PASSWORD"
)

$missing = $required | Where-Object {
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
}
if ($missing.Count -gt 0) {
    throw "Missing release environment variables: $($missing -join ', ')"
}
if (-not $env:AI_INTERVIEW_API_BASE_URL.StartsWith("https://")) {
    throw "AI_INTERVIEW_API_BASE_URL must use HTTPS"
}
$androidRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
Push-Location $androidRoot
try {
    if (-not (Test-Path -LiteralPath $env:AI_INTERVIEW_RELEASE_STORE_FILE)) {
        throw "Release keystore not found: $env:AI_INTERVIEW_RELEASE_STORE_FILE"
    }

    $tasks = @("lintRelease", "assembleRelease", "bundleRelease")
    if (-not $SkipTests) {
        $tasks = @("testDebugUnitTest") + $tasks
    }

    & ".\gradlew.bat" @tasks --no-daemon
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE"
    }

    Write-Host "Release APK: app\build\outputs\apk\release\app-release.apk"
    Write-Host "Release AAB: app\build\outputs\bundle\release\app-release.aab"
} finally {
    Pop-Location
}
