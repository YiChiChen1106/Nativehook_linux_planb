# Full Project GitHub Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the existing GitHub project while adding a sanitized portfolio package for the user's resume and project showcase.

**Architecture:** Keep the existing GitHub tree unchanged as the base. Add only portfolio-safe materials under `portfolio/`: project narrative, benchmark methodology, summarized results, presentations, and report excerpts. Do not copy the internal GitLab repository or OH source tree. Record the exclusion policy and source dates in a manifest.

**Tech Stack:** Git, PowerShell file synchronization, C++ benchmark sources, shell/Python runners, Markdown/HTML artifacts.

---

### Task 1: Create the portfolio manifest and directory policy

**Files:**
- Create: `SYNC_MANIFEST_2026-08-18.md`

- [ ] Record the source repositories, source branch tips, copied artifacts, and excluded patterns before staging files.
- [ ] Keep the existing GitHub tree and branch history untouched.

### Task 2: Add the portfolio narrative and benchmark materials

**Files:**
- Create: `portfolio/README.md`
- Create: `portfolio/benchmark/`
- Create: `portfolio/experiments/`
- Create: `portfolio/presentations/`
- Create: `portfolio/report/`

- [ ] Describe the engineering problem and contributions without copying internal implementation files.
- [ ] Copy benchmark source only after checking for internal hostnames, credentials, package paths, and company-specific identifiers.
- [ ] Copy summarized CSV/Markdown results, selected HTML presentations, and report excerpts.

### Task 3: Keep internal and unrelated materials out of the portfolio

**Files:**
- Exclude: GitLab `device/` source and complete repository snapshots
- Exclude: SOW, internship certificate, unrelated PPT/PDF, raw logs, build outputs, caches, internal datasets, and temporary screenshots

- [ ] Remove the temporary GitLab archive extraction from the staging branch.
- [ ] Check all staged paths against the exclusion list.

### Task 4: Verify and commit the portfolio package

- [ ] Run `git diff --check`.
- [ ] Scan staged text for credential-like strings and forbidden local path references.
- [ ] Confirm no internal source tree, build directory, cache directory, raw log, or personal document is staged.
- [ ] Commit the portfolio package on `portfolio/2026-08-18`.

### Task 5: Push and report

- [ ] Push `portfolio/2026-08-18` to the user's GitHub repository.
- [ ] Report the branch URL, commit ID, source commit IDs, and any content excluded from the snapshot.
