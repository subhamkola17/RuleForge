$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Exe = Join-Path $ProjectRoot "ruleforge.exe"
$TempDir = Join-Path $PSScriptRoot "_evaluator_edge_temp"

New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

$script:Passed = 0
$script:Failed = 0

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Content
    )

    $Encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $Encoding)
}

function Assert-Test {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Details = ""
    )

    if ($Passed) {
        $script:Passed++
        Write-Host "[PASS] $Name" -ForegroundColor Green
    }
    else {
        $script:Failed++
        Write-Host "[FAIL] $Name" -ForegroundColor Red

        if (-not [string]::IsNullOrWhiteSpace($Details)) {
            Write-Host $Details
        }
    }
}

function Invoke-RuleJson {
    param(
        [string]$RulePath,
        [string]$InputPath
    )

    $Raw = & $Exe run $RulePath $InputPath --format json 2>&1
    $ExitCode = $LASTEXITCODE
    $RawText = ($Raw | ForEach-Object { "$_" }) -join "`n"

    $ParsedJson = $null
    $ParseError = $null

    try {
        $ParsedJson = $RawText | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        $ParseError = $_.Exception.Message
    }

    return [PSCustomObject]@{
        ExitCode  = $ExitCode
        Raw       = $RawText
        Json      = $ParsedJson
        ParseError = $ParseError
    }
}

function Write-TestFiles {
    param(
        [string]$RuleText,
        [string]$InputText
    )

    $RulePath = Join-Path $TempDir "edge_test.rf"
    $InputPath = Join-Path $TempDir "edge_test.json"

    Write-Utf8NoBom $RulePath $RuleText
    Write-Utf8NoBom $InputPath $InputText

    return [PSCustomObject]@{
        RulePath  = $RulePath
        InputPath = $InputPath
    }
}

Write-Host "========================================"
Write-Host " RuleForge Evaluator Edge-Case Tests"
Write-Host "========================================"

if (-not (Test-Path $Exe)) {
    Write-Host "[ERROR] ruleforge.exe not found. Compile the project first." -ForegroundColor Red
    exit 1
}

# Test 1: Quoted numeric-looking strings must remain strings.
$Files = Write-TestFiles @'
RULE quoted_number
WHEN code == "80"
THEN result = "Matched"
ELSE result = "Not matched"
END
'@ '{"code":"80"}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath
$traceText = ""
if ($null -ne $r.Json -and $null -ne $r.Json.trace) {
    $traceText = $r.Json.trace -join "`n"
}

Assert-Test "Quoted numeric-looking strings remain strings" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $true -and
    $r.Json.result.value -eq "Matched" -and
    $traceText.Contains('(code == "80")') -and
    $traceText.Contains('"80"')
) $r.Raw

# Test 2: Numeric literals should appear without quotation marks in traces.
$Files = Write-TestFiles @'
RULE numeric_literal
WHEN score >= 80
THEN result = "Pass"
ELSE result = "Fail"
END
'@ '{"score":80}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath
$traceText = ""
if ($null -ne $r.Json -and $null -ne $r.Json.trace) {
    $traceText = $r.Json.trace -join "`n"
}

Assert-Test "Numeric literal is unquoted in condition trace" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $true -and
    $r.Json.result.value -eq "Pass" -and
    $traceText.Contains("(score >= 80)") -and
    -not $traceText.Contains('(score >= "80")')
) $r.Raw

# Test 3: Multiplication must take precedence over addition.
$Files = Write-TestFiles @'
RULE arithmetic_precedence
WHEN 2 + 3 * 4 == 14
THEN result = 1
ELSE result = 0
END
'@ '{}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath

Assert-Test "Arithmetic precedence evaluates multiplication first" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $true -and
    $r.Json.result.value -eq 1
) $r.Raw

# Test 4: Parentheses must override the default precedence.
$Files = Write-TestFiles @'
RULE parentheses_precedence
WHEN (2 + 3) * 4 == 20
THEN result = 1
ELSE result = 0
END
'@ '{}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath

Assert-Test "Parentheses override arithmetic precedence" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $true -and
    $r.Json.result.value -eq 1
) $r.Raw

# Test 5: AND must skip the right condition when the left is false.
$Files = Write-TestFiles @'
RULE and_short_circuit
WHEN score > 100 AND missingFlag == true
THEN result = "Unexpected"
ELSE result = "Short-circuited"
END
'@ '{"score":50,"missingFlag":true}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath
$traceText = ""
if ($null -ne $r.Json -and $null -ne $r.Json.trace) {
    $traceText = $r.Json.trace -join "`n"
}

Assert-Test "AND short-circuit skips the right condition" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $false -and
    $r.Json.result.value -eq "Short-circuited" -and
    $traceText.Contains("AND short-circuit") -and
    -not $traceText.Contains("Condition: (missingFlag == true)")
) $r.Raw

# Test 6: OR must skip the right condition when the left is true.
$Files = Write-TestFiles @'
RULE or_short_circuit
WHEN score > 10 OR missingFlag == true
THEN result = "Short-circuited"
ELSE result = "Unexpected"
END
'@ '{"score":50,"missingFlag":false}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath
$traceText = ""
if ($null -ne $r.Json -and $null -ne $r.Json.trace) {
    $traceText = $r.Json.trace -join "`n"
}

Assert-Test "OR short-circuit skips the right condition" (
    $r.ExitCode -eq 0 -and
    $null -eq $r.ParseError -and
    $r.Json.conditionPassed -eq $true -and
    $r.Json.result.value -eq "Short-circuited" -and
    $traceText.Contains("OR short-circuit") -and
    -not $traceText.Contains("Condition: (missingFlag == true)")
) $r.Raw

# Test 7: Division by zero must return a parseable JSON error.
$Files = Write-TestFiles @'
RULE division_by_zero
WHEN 10 / 0 > 1
THEN result = "Unexpected"
ELSE result = "Unexpected"
END
'@ '{}'

$r = Invoke-RuleJson $Files.RulePath $Files.InputPath

Assert-Test "Division by zero returns a parseable JSON error" (
    $null -eq $r.ParseError -and
    $null -ne $r.Json -and
    $r.Json.status -eq "error"
) $r.Raw

Write-Host "========================================"
Write-Host ("Results: {0} passed, {1} failed" -f $script:Passed, $script:Failed)
Write-Host "========================================"

if ($script:Failed -gt 0) {
    exit 1
}

exit 0