# native_hook Plan B project page

This directory is a static site package prepared for a separate GitHub Pages project repository.

## What is included

- `index.html`
- `styles.css`
- `assets/charts/planb_sweep_summary_ppt_clean.png`
- `assets/charts/planb_blocked_summary_ppt_clean.png`
- `.nojekyll`

## Suggested repository name

You can create a repository like:

- `native-hook-planb`

Then this page can usually be published at:

- `https://<your-github-username>.github.io/native-hook-planb/`

## Deploy steps

1. Create a new GitHub repository for this project page.
2. Copy all files in this directory into that repository root.
3. Commit and push to `main`.
4. In GitHub repository settings, enable Pages from:
   - Branch: `main`
   - Folder: `/ (root)`
5. Wait for GitHub Pages to finish building.

## Notes

- This page is intentionally a static site with no framework dependency.
- All asset paths are relative, so it is suitable for a project page under a repository subpath.
- The content currently focuses on:
  - Why Plan B exists
  - Current runnable chain
  - Difference from real native_hook
  - Latest sample / filter / blocked results
  - Next-step ablation plan
