#!/usr/bin/env bash
# Build dist/os-book.html + dist/os-book.pdf from ordered OS tutorial chapters.
# Requires: pandoc, xelatex (texlive). Optional: Chromium for PDF fallback if LaTeX fails.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${ROOT}/dist"
CSS_FILE="${ROOT}/book.css"
HTML_OUT="${OUT_DIR}/os-book.html"
PDF_OUT="${OUT_DIR}/os-book.pdf"

CHAPTERS=(
  "OS-0-Introduction.md"
  "OS-1-HelloInit-UEFI-boot-and-serial-study.md"
  "OS-2-ExitBootServices-and-kernel-jump-study.md"
  "OS-3-From-hello-init-to-real-OS-next-milestones-study.md"
  "OS-4-paging-study.md"
  "OS-5-idt-study.md"
  "OS-6-irq-pic-study.md"
  "OS-7-lapic-study.md"
  "OS-8-page-fault-study.md"
  "OS-99-plan.md"
)

usage() {
  echo "Usage: $0 [--html-only|--pdf-only]"
  echo "  default: dist/os-book.html (CSS) + dist/os-book.pdf (XeLaTeX via pandoc)"
  echo "  --html-only: HTML only"
  echo "  --pdf-only:  PDF only (pandoc → PDF from md; no HTML)"
  echo "Requires: pandoc, xelatex (texlive). Install: pacman -S pandoc texlive-binextra"
}

HTML_ONLY=0
PDF_ONLY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --html-only) HTML_ONLY=1 ;;
    --pdf-only)  PDF_ONLY=1 ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if [[ "${PDF_ONLY}" -eq 1 && "${HTML_ONLY}" -eq 1 ]]; then
  echo "Cannot combine --html-only and --pdf-only" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
cp -f "${CSS_FILE}" "${OUT_DIR}/book.css"

collect_inputs() {
  INPUTS=()
  local f
  for f in "${CHAPTERS[@]}"; do
    [[ -f "${ROOT}/${f}" ]] || { echo "Missing: ${ROOT}/${f}" >&2; exit 1; }
    INPUTS+=("${ROOT}/${f}")
  done
}

latex_font_args() {
  # Prefer Liberation* so Unicode (diagrams, ↔, ≈) renders in monospace/body.
  # (fc-match -q is not portable; use fc-list to detect an installed family.)
  if [[ -n "$(fc-list "Liberation Serif" 2>/dev/null | head -1)" ]]; then
    printf '%s\n' \
      -V "mainfont=Liberation Serif" \
      -V "sansfont=Liberation Sans" \
      -V "monofont=Liberation Mono"
  fi
}

build_html() {
  pandoc "${INPUTS[@]}" \
    --from markdown \
    --to html5 \
    --standalone \
    --toc \
    --toc-depth=3 \
    --metadata title="OS tutorial (src-os)" \
    --metadata lang=en \
    --css book.css \
    --highlight-style=tango \
    -o "${HTML_OUT}"
  echo "Wrote ${HTML_OUT}"
}

build_pdf_latex() {
  command -v pandoc >/dev/null 2>&1 || { echo "pandoc is required" >&2; return 1; }
  command -v xelatex >/dev/null 2>&1 || { echo "xelatex not found (install texlive: pacman -S texlive-binextra)" >&2; return 1; }

  # shellcheck disable=SC2207
  mapfile -t _fontargs < <(latex_font_args)

  if ! pandoc "${INPUTS[@]}" \
    --from markdown \
    --to pdf \
    --pdf-engine=xelatex \
    "${_fontargs[@]}" \
    --toc \
    --toc-depth=3 \
    --metadata title="OS tutorial (src-os)" \
    --metadata lang=en \
    --highlight-style=tango \
    -V geometry:margin=2.5cm \
    -V fontsize=11pt \
    -V documentclass=book \
    -V colorlinks=true \
    -o "${PDF_OUT}"; then
    return 1
  fi
  echo "Wrote ${PDF_OUT}"
}

# Fallback: HTML + CSS → PDF (Chrome print). No LaTeX needed.
build_pdf_chromium() {
  local html_path="$1"
  local pdf_path="$2"
  local file_url="file://${html_path}"

  if command -v chromium >/dev/null 2>&1; then
    chromium --headless=new --disable-gpu --no-pdf-header-footer \
      --print-to-pdf="${pdf_path}" "${file_url}" 2>/dev/null
    return 0
  fi
  if command -v google-chrome-stable >/dev/null 2>&1; then
    google-chrome-stable --headless=new --disable-gpu --no-pdf-header-footer \
      --print-to-pdf="${pdf_path}" "${file_url}" 2>/dev/null
    return 0
  fi
  if command -v google-chrome >/dev/null 2>&1; then
    google-chrome --headless=new --disable-gpu --no-pdf-header-footer \
      --print-to-pdf="${pdf_path}" "${file_url}" 2>/dev/null
    return 0
  fi
  if command -v wkhtmltopdf >/dev/null 2>&1; then
    wkhtmltopdf --enable-local-file-access --margin-top 12 --margin-bottom 12 \
      --margin-left 12 --margin-right 12 "${html_path}" "${pdf_path}" 2>/dev/null
    return 0
  fi
  return 1
}

collect_inputs

if [[ "${PDF_ONLY}" -eq 0 ]]; then
  build_html
fi

if [[ "${HTML_ONLY}" -eq 1 ]]; then
  exit 0
fi

# PDF: primary = pandoc + XeLaTeX
if build_pdf_latex; then
  exit 0
fi

# Fallback: need HTML for Chromium path
if [[ "${PDF_ONLY}" -eq 1 && ! -f "${HTML_OUT}" ]]; then
  echo "PDF-only mode failed (no xelatex). Building HTML for Chromium fallback…" >&2
  build_html
fi

if [[ -f "${HTML_OUT}" ]] && build_pdf_chromium "${HTML_OUT}" "${PDF_OUT}"; then
  echo "Wrote ${PDF_OUT} (via Chromium / wkhtmltopdf)"
  exit 0
fi

echo "Failed to build PDF. Install: pacman -S pandoc texlive-binextra" >&2
echo "  (provides xelatex). Or install chromium for HTML→PDF fallback." >&2
exit 1
