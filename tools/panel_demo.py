"""Opens the panel and walks through a toast and two example panels."""
import threading
import time

from orbi.panel import Panel

EMAIL = """Sarah wants to move the **demo to Friday** at 2pm.

- The staging build needs to be ready by Thursday
- She'll send the deck tonight
- Marco can't make it, so record the call
"""

CODE = """Here's a regex for emails:

```python
import re

EMAIL = re.compile(r"^[\\w.+-]+@[\\w-]+(\\.[\\w-]+)+$")

def is_email(text):
    return EMAIL.match(text) is not None  # a deliberately long line, so the block has to scroll sideways
```

| Input | Match |
| --- | --- |
| `sarah@example.com` | yes |
| `sarah@example` | no |
| `a.b+tag@mail.co.uk` | yes |
"""


def demo(panel):
    time.sleep(1)
    panel.toast("What was Sarah's email about?")
    time.sleep(2)
    panel.show("Re: Demo timing", EMAIL, [{"label": "Open in Gmail", "url": "https://mail.google.com"}])
    time.sleep(5)
    panel.toast("Write me a regex for emails")
    time.sleep(2)
    panel.show("Email regex", CODE)


panel = Panel()
threading.Thread(target=demo, args=(panel,), daemon=True).start()
panel.run()
