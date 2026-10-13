
# RuleForge JSON regression tests
# Run from the project root:
# powershell -ExecutionPolicy Bypass -File .\tests\test_json_reports.ps1

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $projectRoot "ruleforge.exe"

if (-not (Test-Path $exe)) {
    throw "Cannot find ruleforge.exe at '$exe'. Build it first."
}

$tempDir = Join-Path $PSScriptRoot "_json_test_temp"

if (Test-Path $tempDir) {
    Remove-Item $tempDir -Recurse -Force
}

New-Item -ItemType Directory -Path $tempDir | Out-Null

# Use script-scoped counters consistently throughout this script.
$script:passed = 0
$script:failed = 0
$script:exe = $exe

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Assert-Test(
    [string]$Name,
    [bool]$Condition,
    [string]$Details = ""
) {
    if ($Condition) {
        Write-Host "[PASS] $Name" -ForegroundColor Green
        $script:passed++
    }
    else {
        Write-Host "[FAIL] $Name" -ForegroundColor Red

        if (-not [string]::IsNullOrWhiteSpace($Details)) {
            Write-Host "       $Details" -ForegroundColor Yellow
        }

        $script:failed++
    }
}

function Invoke-RuleForgeJson([string[]]$Arguments) {
    $rawOutput = (& $script:exe @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $parsed = $null
    $parseError = $null

    try {
        $parsed = $rawOutput | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        $parseError = $_.Exception.Message
    }

    return [PSCustomObject]@{
        Raw       = $rawOutput
        ExitCode  = $exitCode
        Json      = $parsed
        ParseError = $parseError
    }
}

try {
    Write-Host "========================================"
    Write-Host "    RuleForge JSON Regression Tests"
    Write-Host "========================================"

    # Test 1: Numeric JSON result.
    $numberRule = Join-Path $tempDir "number.rf"
    $numberInput = Join-Path $tempDir "number.json"

    Write-Utf8NoBom $numberRule @'
RULE numeric_result
WHEN score >= 50
THEN result = 42
ELSE result = 0
END
'@

    Write-Utf8NoBom $numberInput '{"score":75}'

    $numberRun = Invoke-RuleForgeJson @(
        "run", $numberRule, $numberInput, "--format", "json"
    )

    $numberIsNumeric = $false

    if ($null -ne $numberRun.Json) {
        $v = $numberRun.Json.result.value

        $numberIsNumeric = (
            $null -ne $v -and
            $v -is [ValueType] -and
            $v -isnot [bool]
        )
    }

    Assert-Test "Numeric result remains a JSON number" (
        $numberRun.ExitCode -eq 0 -and
        $null -eq $numberRun.ParseError -and
        $null -ne $numberRun.Json -and
        $numberRun.Json.status -eq "success" -and
        $numberIsNumeric -and
        $numberRun.Json.result.value -eq 42
    ) $numberRun.Raw

    # Test 2: Boolean JSON result.
    $booleanRule = Join-Path $tempDir "boolean.rf"
    $booleanInput = Join-Path $tempDir "boolean.json"

    Write-Utf8NoBom $booleanRule @'
RULE boolean_result
WHEN enabled == true
THEN result = true
ELSE result = false
END
'@

    Write-Utf8NoBom $booleanInput '{"enabled":true}'

    $booleanRun = Invoke-RuleForgeJson @(
        "run", $booleanRule, $booleanInput, "--format", "json"
    )

    $booleanIsBoolean = (
        $null -ne $booleanRun.Json -and
        $booleanRun.Json.result.value -is [bool]
    )

    Assert-Test "Boolean result remains a JSON boolean" (
        $booleanRun.ExitCode -eq 0 -and
        $null -eq $booleanRun.ParseError -and
        $null -ne $booleanRun.Json -and
        $booleanRun.Json.status -eq "success" -and
        $booleanIsBoolean -and
        $booleanRun.Json.result.value -eq $true
    ) $booleanRun.Raw

    # Test 3: String JSON result.
    $stringRule = Join-Path $tempDir "string.rf"
    $stringInput = Join-Path $tempDir "string.json"

    Write-Utf8NoBom $stringRule @'
RULE string_result
WHEN score >= 50
THEN result = "Eligible"
ELSE result = "Not eligible"
END
'@

    Write-Utf8NoBom $stringInput '{"score":75}'

    $stringRun = Invoke-RuleForgeJson @(
        "run", $stringRule, $stringInput, "--format", "json"
    )

    Assert-Test "String result remains a JSON string" (
        $stringRun.ExitCode -eq 0 -and
        $null -eq $stringRun.ParseError -and
        $null -ne $stringRun.Json -and
        $stringRun.Json.status -eq "success" -and
        $stringRun.Json.result.value -is [string] -and
        $stringRun.Json.result.value -eq "Eligible"
    ) $stringRun.Raw

    # Test 4: False condition selects ELSE.
    Write-Utf8NoBom $numberInput '{"score":10}'

    $elseRun = Invoke-RuleForgeJson @(
        "run", $numberRule, $numberInput, "--format", "json"
    )

    Assert-Test "False condition reports false and uses ELSE result" (
        $elseRun.ExitCode -eq 0 -and
        $null -eq $elseRun.ParseError -and
        $null -ne $elseRun.Json -and
        $elseRun.Json.conditionPassed -eq $false -and
        $elseRun.Json.result.value -eq 0
    ) $elseRun.Raw

    # Test 5: Validation-only JSON report.
    $validateRun = Invoke-RuleForgeJson @(
        "validate",
        (Join-Path $projectRoot "examples\scholarship.rf"),
        (Join-Path $projectRoot "examples\scholarship.json"),
        "--format", "json"
    )

    Assert-Test "Validation JSON succeeds without execution fields" (
        $validateRun.ExitCode -eq 0 -and
        $null -eq $validateRun.ParseError -and
        $null -ne $validateRun.Json -and
        $validateRun.Json.status -eq "success" -and
        $validateRun.Json.mode -eq "validate" -and
        $validateRun.Json.trace.Count -eq 0 -and
        $null -eq $validateRun.Json.result
    ) $validateRun.Raw

    # Test 6: Malformed JSON produces a parseable error report.
    $badInput = Join-Path $tempDir "malformed.json"

    Write-Utf8NoBom $badInput '{"score":}'

    $badJsonRun = Invoke-RuleForgeJson @(
        "run", $numberRule, $badInput, "--format", "json"
    )

    Assert-Test "Malformed JSON produces a parseable JSON error report" (
        $badJsonRun.ExitCode -ne 0 -and
        $null -eq $badJsonRun.ParseError -and
        $null -ne $badJsonRun.Json -and
        $badJsonRun.Json.status -eq "error" -and
        -not [string]::IsNullOrWhiteSpace($badJsonRun.Json.error)
    ) $badJsonRun.Raw

    # Test 7: Missing rule file produces a parseable error report.
    $missingRule = Join-Path $tempDir "missing.rf"

    $missingRun = Invoke-RuleForgeJson @(
        "run", $missingRule, $numberInput, "--format", "json"
    )

    Assert-Test "Missing file produces a parseable JSON error report" (
        $missingRun.ExitCode -ne 0 -and
        $null -eq $missingRun.ParseError -and
        $null -ne $missingRun.Json -and
        $missingRun.Json.status -eq "error" -and
        $missingRun.Json.error -like "*Cannot open file*"
    ) $missingRun.Raw

    Write-Host "========================================"
    Write-Host (
        "Results: {0} passed, {1} failed" -f
        $script:passed, $script:failed
    )
    Write-Host "========================================"

    if ($script:failed -gt 0) {
        exit 1
    }

    exit 0
}
finally {
    if (Test-Path $tempDir) {
        Remove-Item $tempDir -Recurse -Force
    }
}
