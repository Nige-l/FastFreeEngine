# FastFreeEngine Documentation Website

Static documentation site built with [MkDocs](https://www.mkdocs.org/) and [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/).

## Local Development

```bash
cd website
pip install -r requirements.txt
mkdocs serve  # local dev server at http://127.0.0.1:8000
mkdocs build  # generate static site in website/site/
```

## Deployment

The site deploys automatically to GitHub Pages via GitHub Actions on every push to `main`. See `.github/workflows/website.yml` for the CI workflow.

## Directory Structure

```
website/
    mkdocs.yml          MkDocs configuration
    requirements.txt    Python dependencies
    docs/               Markdown source files
        index.md        Home page
        getting-started.md
        tutorials/      Tutorial pages
        api/            API reference (generated from .context.md)
        internals/      "How It Works" pages
        community.md    Community links
    site/               Build output (git-ignored)
```
