# Release Process

## Version Numbering
MeshCore follows Semantic Versioning (semver.org):
- MAJOR.MINOR.PATCH format
- MAJOR: Breaking changes
- MINOR: New features, backward compatible
- PATCH: Bug fixes, backward compatible

## Pre-Release Checklist

### Code Quality
- All CI workflows passing
- No compiler warnings
- Code follows style guide
- Memory allocation rules followed
- Security scan clean

### Testing
- Builds on all supported platforms
- Hardware tested on representative devices
- Radio functionality verified
- Client apps tested (Web, Android, iOS)
- Example applications work correctly

### Documentation
- CHANGELOG.md updated
- README.md reflects new features
- API documentation current
- Migration guide if breaking changes
- Release notes drafted

## Release Steps

### 1. Version Branch
- Create release branch from dev: `git checkout -b release/vX.Y.Z dev`
- Update version numbers in library.json
- Update CHANGELOG.md with release date
- Commit version changes

### 2. Testing Phase
- Run full test suite
- Hardware validation on all supported devices
- Community beta testing if major release
- Fix critical bugs, cherry-pick to release branch

### 3. Merge to Master
- Merge release branch to master
- Tag release: `git tag -a vX.Y.Z -m "Release version X.Y.Z"`
- Push tags: `git push origin vX.Y.Z`

### 4. Build Artifacts
- Build firmware binaries for all variants
- Generate UF2 files where applicable
- Create checksums for all binaries
- Upload to flasher.meshcore.co.uk

### 5. GitHub Release
- Create GitHub release from tag
- Attach binary artifacts
- Include release notes from CHANGELOG
- Link to documentation
- Highlight breaking changes

### 6. Post-Release
- Merge master back to dev
- Announce on Discord
- Update documentation sites
- Close related issues

## Hotfix Process
For critical bugs in production:
- Branch from master: `git checkout -b hotfix/vX.Y.Z master`
- Fix bug with minimal changes
- Update CHANGELOG
- Test thoroughly
- Merge to both master and dev
- Tag and release

## Release Types

### Major Releases
- Breaking API changes
- Protocol changes requiring network coordination
- Significant architecture changes
- Extended beta testing required

### Minor Releases
- New features
- New hardware support
- Performance improvements
- Should be backward compatible

### Patch Releases
- Bug fixes only
- Security fixes
- Documentation corrections
- Quick turnaround acceptable

## Automated Release Process

GitHub Actions is set up to automatically build and release firmware.

It will automatically build firmware when one of the following tag formats are pushed:

- `companion-v1.0.0`
- `repeater-v1.0.0`
- `room-server-v1.0.0`

> NOTE: replace `v1.0.0` with the version you want to release as.

- You can push one, or more tags on the same commit, and they will all build separately.
- Once the firmware has been built, a new (draft) GitHub Release will be created.
- You will need to update the release notes, and publish it.
