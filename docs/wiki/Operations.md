# Master Control Program Operations

## Local Commands

```powershell
cmake --build build\debug --config Debug
ctest --test-dir build\debug -C Debug --output-on-failure
cmake --install build\debug --config Debug --prefix dist\debug
dist\debug\MasterControlBootstrapper.exe detect
```

## Push Guard

- Enable the repository hook with `scripts/Enable-GitHooks.ps1`.
- The pre-push hook rejects commits that declare AI contributors.
- The GitHub `AI Contributor Guard` workflow mirrors the same rule for pushes and pull requests.
