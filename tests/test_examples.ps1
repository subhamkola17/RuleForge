$ErrorActionPreference = "Stop"

Write-Host "========================================"
Write-Host "       RuleForge Example Tests"
Write-Host "========================================"

$tests = @(
    @{
        Name = "Scholarship eligible"
        Rule = "examples/scholarship.rf"
        Input = "examples/scholarship.json"
        Expected = "result = Eligible"
    },
    @{
        Name = "Driving eligibility"
        Rule = "examples/driving.rf"
        Input = "examples/driving.json"
        Expected = "result = Eligible"
    },
    @{
        Name = "Discount calculation"
        Rule = "examples/discount.rf"
        Input = "examples/discount.json"
        Expected = "discount = 10"
    }
)

$passed = 0
$failed = 0

foreach ($test in $tests) {
    Write-Host "`nTEST: $($test.Name)"

    if (-not (Test-Path $test.Rule) -or
        -not (Test-Path $test.Input)) {
        Write-Host "[FAIL] Rule or input file not found."
        $failed++
        continue
    }

    $output = & ".\ruleforge.exe" $test.Rule $test.Input 2>&1 |
        Out-String
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0 -and $output.Contains($test.Expected)) {
        Write-Host "[PASS] Expected result found."
        $passed++
    }
    else {
        Write-Host "[FAIL] Expected: $($test.Expected)"
        Write-Host $output
        $failed++
    }
}

Write-Host "`n========================================"
Write-Host "Results: $passed passed, $failed failed"
Write-Host "========================================"

if ($failed -gt 0) {
    exit 1
}

exit 0