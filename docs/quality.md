# Quality and CI

rss-ddc reports repository health through a concise README status strip and a
static [Quality Dashboard](quality/index.html). On GitHub Pages, the dashboard
URL is `https://rogue-signal-studios.github.io/rss-ddc/quality/` once Pages is
enabled with **GitHub Actions** as its publishing source.

## What the badges mean

- **Repository quality** is the GitHub Actions workflow that runs the build,
  synthetic tests, installed-library consumer contract, static analysis,
  LLVM coverage, CodeQL, and dashboard generation.
- **Coverage** links to the dashboard's LLVM-derived report. It deliberately
  does not show a hand-maintained or guessed percentage.
- **Security** links to GitHub CodeQL results. **Dependency review** runs on
  pull requests and only evaluates dependency changes; this C/Objective-C
  repository has no package-manager runtime dependency graph.
- **License** is the repository's MIT license. **rss-ddc API** is the public
  pre-1.0 version declared by `include/rss_ddc.h`.

## Dashboard data

`make coverage` executes the existing offline test executables with LLVM
instrumentation and writes `build/coverage/coverage.json` plus a readable
`coverage.txt`. The dashboard generator combines that artifact with a tiny
compiled metadata program that calls `rss_ddc_provider_capabilities`; provider
rows therefore come from the current library policy, not a duplicate table.

The dashboard shows the commit, timestamp, public API version, CI check state,
test-executable count, compiler/warning policy, LLVM line/function/region/
branch coverage, and provider transport capabilities. A transport capability
does not claim that every monitor supports every semantic action.

GitHub Actions uploads coverage and dashboard artifacts for every pull request.
Only a successful push to `main` deploys the static payload using
`configure-pages`, `upload-pages-artifact`, and `deploy-pages`; pull requests
validate generation but never deploy. If Pages has not been enabled in the
repository settings, the deployment job will require that one-time setting.

## Local equivalents

```sh
make clean
make
make test
make consumer-test
make analyze
make coverage
make dashboard
make dashboard-test
git diff --check
```

All of these commands are hardware-independent. They compile synthetic fixtures
and never open an IOAV display client, issue DDC/CI, or validate physical
monitor behavior. Hardware evidence remains documented separately in the
[hardware validation matrix](hardware-validation.md).
