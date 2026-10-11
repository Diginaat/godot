---
type: Technique
title: Reading PDFs for research
description: The agent's PDF reader needs pdftoppm; without it, extract text with pypdf and read the text files.
tags: [research, tooling]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# Technique

```
python -m pip install --user pypdf
python -c "import pypdf; r = pypdf.PdfReader('paper.pdf'); print('\n'.join(p.extract_text() or '' for p in r.pages))" > paper.txt
```

Write the text to a scratch folder, not the repository. Slide decks often
have little text per page (speaker notes help when present); figures are
lost, so note when a conclusion depends on an image.
