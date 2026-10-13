\# RuleForge



A local-first C++20 rule engine for evaluating business rules with static type validation, expression evaluation, and JSON reporting.



\## Features



\- Rule-based evaluation using RULE, WHEN, THEN, ELSE, and END.

\- Static type validation before execution.

\- Arithmetic expressions with operator precedence and parentheses.

\- Boolean operators with short-circuit evaluation.

\- JSON input and structured JSON reports.

\- Execution traces and error reporting.

\- Local execution without a hosted service.



\## Project Structure



```text

RuleForge/

├── src/

│   ├── main.cpp

│   ├── parser.h

│   ├── validator.h

│   └── evaluator.h

├── examples/

├── tests/

├── .gitignore

└── README.md

```



\## Requirements



\- A C++20-compatible compiler

\- Git for version control (optional)



\## Build



```powershell

g++ -std=c++20 -Wall -Wextra -Wpedantic src/main.cpp -o ruleforge.exe

```



\## Usage



Show the version:



```powershell

.\\ruleforge.exe --version

```



Run a rule with JSON output:



```powershell

.\\ruleforge.exe run examples/scholarship.rf examples/scholarship.json --format json

```



Validate a rule:



```powershell

.\\ruleforge.exe validate examples/scholarship.rf examples/scholarship.json --format json

```



Run with a human-readable report:



```powershell

.\\ruleforge.exe examples/scholarship.rf examples/scholarship.json

```



\## Example Rule



```text

RULE scholarship

WHEN marks >= 80 AND attendance >= 75

THEN result = "Eligible"

ELSE result = "Not eligible"

END

```



Example input:



```json

{

&#x20; "marks": 85,

&#x20; "attendance": 90

}

```



\## Tests



Run the test suites:



```powershell

powershell -ExecutionPolicy Bypass -File .\\tests\\test\_evaluator\_edges.ps1

powershell -ExecutionPolicy Bypass -File .\\tests\\test\_examples.ps1

powershell -ExecutionPolicy Bypass -File .\\tests\\test\_json\_reports.ps1

```



Current baseline: 17 passing tests across the three suites.



\## License



No license has been selected yet.

