# Releasing

Releases are built by GitHub Actions ([`.github/workflows/release.yml`](../.github/workflows/release.yml)), never on a developer's machine, so every published file comes from the public source with a recorded toolchain.

## How to make a release

1. **Choose the version**, `<major>.<minor>.<patch>`, for example `0.1.0`.
2. **Create the release branch** `release/<version>` from `main`, and push it:

   ```
   git checkout main
   git checkout -b release/0.1.0
   git push -u origin release/0.1.0
   ```

3. **Open a pull request into `release/<version>`** with what the release should contain, for example from `main` or from a fix branch, and merge it.
4. **The workflow runs** on the merge and creates a **draft** release named after the version, with the tag `v<version>` (the tag itself is created when the draft is published).
5. **Review the draft** on the repository's *Releases* page: the files, the notes, and if you like, download the patch and try it.
6. **Publish it** from the draft's page.

Merging another pull request into the same release branch rebuilds the draft and replaces its files. Once a release is published the workflow refuses to change it: make a new release branch with a new version instead.

Closing a pull request without merging it does nothing.

## What a release contains

| File | Contents |
|---|---|
| `pcf2001-patch-<version>-win32.zip` | The patch: everything `cmake --install` installs. Today that's `dcogl.dll` (the new graphics component), `README.md`, `LEEME.md` and `LICENSE`. When the patcher program exists, its install rule puts it here too. Development tools are never installed. |
| `pcf2001-patcher-<version>-source.zip`, `pcf2001-patcher-<version>-source.tar.gz` | The source code of the merged commit, in a folder named after the version. |
| `SHA256SUMS.txt` | SHA-256 hashes of the files above, and of every file inside the patch `.zip`. |

The release notes list the toolchain the runner used (MSVC, Windows SDK and CMake versions) and the commands to rebuild the release, so anyone can check the published files ([BUILDING.md](BUILDING.md#comparing-your-build-with-a-release)).

## Where the version comes from

From the name of the release branch. The workflow passes it to the build (`build.bat Release <version>`), so the files report the same version as the release, for example in the first line of `dcogl.log`. Builds made without a version, such as everyday local builds, report the version in the top-level `CMakeLists.txt`.

## Requirements

- The release branch must be named exactly `release/<major>.<minor>.<patch>`; the workflow fails otherwise.
- Pull requests must come from branches of this repository. For pull requests from forks, GitHub gives the workflow read-only access, and it can't create the release.
- The workflow file must be in the merged code, which it is for any release branch created from `main`.
