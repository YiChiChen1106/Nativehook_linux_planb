# native_hook Plan B project page

This repository now contains both:

- a static project page for GitHub Pages / GitLab Pages style display
- the current Linux-side Plan B source tree, result CSVs, analysis scripts, and progress notes

## Repository layout

- `index.html`
  - static landing page for project overview
- `styles.css`
  - static page styling
- `assets/charts/`
  - figures used by the project page
- `linux_native_hook_v1/`
  - current Plan B source tree
- `analysis_scripts/`
  - plotting and small benchmark helpers
- `notes/`
  - progress records, baseline notes, server plan, and flush/batching explanation
- `.nojekyll`
  - prevents GitHub Pages from applying Jekyll processing

## What is intentionally included

- runnable Linux-side Plan B code
- benchmark / sweep shell scripts
- result CSV files
- summary figures for presentation and review
- short markdown notes that explain design decisions and current status

## What is intentionally excluded

- large raw runtime logs under `results/logs/`
- local build output
- temporary Office files
- personal meeting drafts not needed for the public project repository

## Pages deployment

For GitHub project pages, the repository can be published from:

- Branch: `main`
- Folder: `/ (root)`

If Pages is enabled, the landing page should be the root `index.html`.

## Notes

- The static page is intentionally framework-free.
- All asset paths are relative, so the project works under a repository subpath.
- The current content focuses on:
  - Why Plan B exists
  - Current runnable chain
  - Difference from real native_hook
  - Latest sample / filter / blocked results
  - Next-step ablation plan
