# 1. Repo Rules

## 1.1 Commit Conventions

Follow the Conventional Commits format:

```bash
type(scope): message
```

```bash
git commit -m "chore(storage): added B+Tree index support"
```

- Keep each commit focused on one logical change, don't mix refactoring and feature additions in the same commit. Commit Types:
    - `feat` (new feature)
    - `chore` (maintenance)
    - `refactor`
    - `fix`
    - `docs`
    - `test`
    - `misc`

<br/>

## 1.2 Tags, Branches & Releases
Create a **Feature Branch** when a starting a new development phase. 

```bash
git checkout -b feature/btree-index
```

<br/>
Use **Tags** to mark important commits with semantic versioning `MAJOR.MINOR.PATCH`

```bash
v1.0.0  # Major release
v1.1.0  # Minor feature added
v1.1.1  # Patch fix
```

<br/>
Create a **Release Branch** when a milestone is reached: 

```bash
git checkout -b release/v1.0.0
```

Then go to GitHub → Releases → New Release and attach the tag.

<br/>

Tags Utils: 

```bash
### New Tag
git tag -a v1.0.0 -m "First stable release"
git push origin v1.0.0

### Delete Tag 
git tag -d v1.0.0
git push origin --delete v1.0.0

### Listing & Details
git tag
git show v1.0.0

### Checkout & Branching
git checkout v1.0.0
git checkout -b fix-branch v1.0.0
```

<br/>

## 1.3 Change Log
Consider writing a `CHANGELOG.md` file when pushing a new release. You can just generate one: 

```bash
git log --pretty=format:"- %s" v1.0.0 -- v1.1.0 >> CHANGELOG.md
```

<br/>
Otherwise follow this structure if you need to manually write it:

```bash
# Changelog

## [1.1.0] - 2025-02-09
### Added
- Support for transactions in the KV store.
- Improved B+Tree indexing performance.

### Fixed
- Concurrency issue in the write-ahead log.
- Memory leak in database replication.

## [1.0.0] - 2025-01-15
### Added
- Initial release with B+Tree and KV store.

```