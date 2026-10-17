# Releasing Fairfax

Fairfax releases are cut from a Git tag. The tag is the single source of the
version: a push of `vX.Y.Z` starts the release workflow, which refuses to run
unless `vX.Y.Z` without its `v` equals the `VERSION` line in `fairfax.pro`.

## Cut a release

1. Bump `VERSION` in `fairfax.pro` and commit it. That one line is the
   version compiled into the app and read by every packaging script.

   ```sh
   # edit the VERSION = ... line
   git commit -am "Release 0.5.2"
   ```

2. Tag the commit and push the tag to the public GitHub repository:

   ```sh
   git tag v0.5.2
   git push origin v0.5.2
   ```

   The tag must be `v` plus exactly `fairfax.pro`'s `VERSION`. A mismatch
   fails `check-version` before anything is built.

3. The workflow does the rest. It takes around an hour: one x86_64 runner
   builds and then checks all five packages, and a final job assembles and
   publishes the GitHub release.

4. Optionally, add the ARM packages (see [ARM packages](#arm-packages)).

To re-run a release, either re-run the failed workflow, or start the
**Release** workflow manually and give it the existing tag. The workflow is
idempotent: if the release already exists it uploads the assets again with
`--clobber` and refreshes the notes.

## What the workflow does

`.github/workflows/release.yml` has three jobs:

- **check-version** — checks out the tag and fails unless the tag without its
  leading `v` equals `fairfax.pro`'s `VERSION`.
- **build** — on `ubuntu-24.04` (x86_64): frees disk space, runs
  `bin/package-linux all` (builds every target, then installs each package
  in a fresh container of its distro and self-tests it), then uploads
  `dist/*` as the artifact `fairfax-linux-x86_64`. Docker is preinstalled on
  the runner.
- **release** — downloads the artifact, writes `fairfax-version.txt` (the
  bare version) with its own `fairfax-version.txt.sha256`, writes a
  `SHA256SUMS` file over every asset, and publishes the GitHub release with
  `gh release create`, or `gh release upload --clobber` when the release
  already exists. It fails first if any expected asset is missing.

The asset names are the contract the installer and the README rely on:

| Asset | System |
| --- | --- |
| `fairfax-x86_64.pkg.tar.zst` | Arch Linux |
| `fairfax-amd64.deb` | Debian/Ubuntu |
| `fairfax-x86_64.rpm` | Fedora |
| `fairfax-x86_64.AppImage` | any Linux from about 2022 on |
| `fairfax-linux-x86_64.tar.gz` | portable tarball |
| `fairfax-version.txt` (+ `.sha256`) | the current version, read first by `install.sh` |
| `install.sh` | the one-line installer |
| `SHA256SUMS` | checksums over every asset |

Every package has a sibling `.sha256`, and `SHA256SUMS` covers the lot.
`install.sh` verifies every download, `fairfax-version.txt` included.

Release notes come from the matching `## <version>` section of `CHANGELOG.md`
when that file exists. There is no changelog today, so the release falls back
to a short body with the one-line install command and the asset table. The
workflow never creates `CHANGELOG.md`.

## Build the packages locally

`bin/package-linux` builds the Linux packages in containers, so the host only
needs a container runtime. On spoond (the Arch build host) run it as root
with `podman` and `--network host`; the script uses `--network host` by
default because the kernel there has no nftables, so bridge networking fails.

```sh
sudo bin/package-linux all            # build and check every package
sudo bin/package-linux appimage tar   # just the two Qt-bundling targets
sudo bin/package-linux --check all    # re-check packages already in dist/
bin/package-linux --dry-run all       # print the container commands only
```

The output lands in `dist/`, each file with a `.sha256` beside it. Building
`all` also checks each package in a fresh container of its distro and runs
the app's `--demo --selftest`; a plain named target only builds unless you add
`--check`. The AppImage and tarball bundle Qt 6.8.3 (pulled through
`aqtinstall` on Ubuntu 22.04) and are checked on Ubuntu 24.04 and Debian
trixie, so their checks only need the host's X/GL runtime. It uses
`FAIRFAX_JOBS` for the compiles (default 2), and `FAIRFAX_LINUXDEPLOY_TAG`
to override the pinned linuxdeploy release.

The workflow calls exactly this script, so a local `sudo bin/package-linux
all` is the closest thing to a rehearsal of the release build. It builds for
the host's architecture only; `FAIRFAX_ARCH` can preview another one with
`--dry-run`.

## ARM packages

The release workflow builds x86_64 only. The ARM (aarch64) packages are built
on an ARM machine running Arch, such as an Omarchy ARM box, and added to the
release by hand. ARM users can build from source until then; `install.sh`
tells them so when a release has no ARM files.

On the ARM machine, as your normal user (it needs `docker` or `podman`):

```sh
git clone https://github.com/jrimmer/fairfax.git
cd fairfax
git checkout v0.5.2              # the release's tag
bin/package-linux all
```

That builds and checks the same five packages with ARM names
(`fairfax-aarch64.pkg.tar.zst`, `fairfax-arm64.deb`, `fairfax-aarch64.rpm`,
`fairfax-aarch64.AppImage`, `fairfax-linux-aarch64.tar.gz`), each with its
`.sha256`, in `dist/`. The `.deb`, `.rpm`, AppImage and tarball are built in
containers as on x86_64. Arch Linux publishes no ARM container image, so the
Arch package is built on the machine itself with `makepkg`, which may ask for
your password to install the build dependencies, and its check runs the
packaged app's selftest against the machine's own Qt.

Then attach them to the release:

```sh
gh release upload v0.5.2 dist/fairfax-aarch64.* dist/fairfax-arm64.* dist/fairfax-linux-aarch64.*
```

`SHA256SUMS` on the release then covers only the x86_64 files; each ARM file
has its own `.sha256`, which is what `install.sh` checks.
