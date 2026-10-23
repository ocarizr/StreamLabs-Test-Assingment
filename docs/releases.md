# Publishing PipeForge releases

## Trigger and provenance

Only pushing a version tag triggers `.github/workflows/release.yml`. There is no manual publishing trigger, branch publishing trigger, or release-on-merge behavior.

Supported tags are `Vmajor.minor` and `Vmajor.minor.patch`, also accepting lowercase `v`. Examples: `V1.0`, `V1.0.0`, `V1.0.1`. Components have no leading zeros except zero itself. Prerelease/build suffixes and unprefixed versions are not supported. `V1.0` normalizes to the binary version `1.0.0`; asset and release names retain the original tag.

The validation job fetches full history and requires the tag's peeled commit, event commit, and checkout to agree. It then requires that commit to be an ancestor of fetched `origin/master`. A historical master commit is allowed; a commit existing only on another branch is rejected. Both lightweight and annotated tags are supported. Pushing a branch and tag together does not substitute for this ancestry check. Push master before the tag so the remote history is available.

After merging the desired code and updating local master, for example:

```sh
git switch master
git pull --ff-only
git tag -a V1.0.0 -m "PipeForge 1.0.0"
git push origin V1.0.0
```

Tag an older master commit explicitly if that is the intended release. Tag naming and ancestry checks apply equally. A tagged commit must contain the release workflow and build tooling; old commits predating this infrastructure cannot be retroactively packaged by the workflow.

## Build and assets

The build job checks out the validated commit, revalidates it, configures `windows-release` with the normalized version, compiles, and runs CTest. It installs CMake components into fresh staging directories and builds:

| Asset for V1.0.0 | Contents |
| --- | --- |
| `pipeforge-client-V1.0.0-windows-x64.zip` | `pipeforge-client.exe` |
| `pipeforge-server-V1.0.0-windows-x64.zip` | `pipeforge-server.exe` |
| `pipeforge-bundle-V1.0.0-windows-x64.zip` | Both executables |

The explicit M1 runtime manifest rejects debug symbols, tests, static libraries, source, nested paths, and extra files. Neither GoogleTest nor development tools are packaged. The static CRT avoids a separate Visual C++ redistributable dependency for these executables. The archive manifest must evolve alongside any future required runtime DLLs or notices.

Each ZIP is extracted into its own temporary directory and its executable(s) run with `--version` and `--help`, a system-only PATH, and no build-directory working directory. This checks package contents, version injection, startup, and missing application DLLs. It is not a replacement for later IPC acceptance tests or a test on every supported Windows installation.

Only validated ZIPs pass to the publishing job. That job has `contents: write`, executes no checked-out project code, rechecks the remote tag's commit, creates a draft release with the three assets, and publishes it after successful upload. Build/validation jobs have read-only permissions. No external personal access token is required. GitHub also displays its automatic source archives; those are separate from the three runtime assets.

## Local packaging, without publishing

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-release "-DPIPEFORGE_VERSION=1.0.0"
cmake --build --preset windows-release
ctest --preset windows-release
python scripts/release.py package --build build/windows-release --tag V1.0.0
python scripts/release.py smoke --tag V1.0.0
```

`dist/` is ignored. Local packaging never creates a tag or GitHub release. To check provenance locally, fetch full master history and run `python scripts/release.py validate --tag V1.0.0 --commit <full-commit-sha>` while that commit is checked out. Packaging and smoke testing are deliberately separate from Git checks so CI can exercise ZIP creation before any release tag exists.

## Failure handling and current feature boundary

Any validation, build, test, installation, or smoke-test failure prevents publishing. An upload failure may leave an unpublished draft; inspect it before retrying. The workflow does not overwrite existing releases or replace moved tags. Do not force-move published tags; use a new version for changed code.

The M1 binaries expose only help/version and do not implement named-pipe communication. Tagging M1 can exercise the infrastructure but does not make later milestones complete. This implementation work does not itself create or push a tag or publish a release.

See [GitHub tag-push events](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#push) and [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create) for platform behavior.
