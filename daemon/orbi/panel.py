"""The screen panel: a small overlay window in the top-right corner for transcript
toasts and rich output (Markdown, code, tables, links)."""
import html
import json
import webbrowser
from pathlib import Path

import AppKit
import markdown
import webview
from webview.window import FixPoint
from PyObjCTools import AppHelper

WIDTH = 440
MARGIN = 12
TOP = 36  # leaves room for the menu bar
PAGE = Path(__file__).with_name("panel.html")


def render_markdown(text):
    md = markdown.Markdown(extensions=["fenced_code", "tables"])
    # Raw HTML in the content is shown as text, not run
    md.preprocessors.deregister("html_block")
    md.inlinePatterns.deregister("html")
    return md.convert(text)


def render_panel(title, content, actions=None):
    parts = [f"<h1>{html.escape(title)}</h1>"]
    parts.append(f'<div class="body">{render_markdown(content)}</div>')
    if actions:
        parts.append('<div class="actions">')
        for action in actions:
            label = html.escape(action["label"])
            url = html.escape(action["url"], quote=True)
            parts.append(f'<button data-url="{url}">{label}</button>')
        parts.append("</div>")
    return "\n".join(parts)


class Api:
    """Called from the page's JavaScript."""

    def __init__(self, panel):
        self._panel = panel

    def fit(self, height):
        self._panel.fit(height)

    def open(self, url):
        webbrowser.open(url)


class Panel:
    def __init__(self):
        """Create on the main thread; the window opens when run() starts."""
        screen = webview.screens[0]
        max_height = int(screen.height * 0.75)
        page = PAGE.read_text().replace("MAX_HEIGHT", str(max_height))
        self.window = webview.create_window(
            "Orbi",
            html=page,
            js_api=Api(self),
            x=screen.width - WIDTH - MARGIN,
            y=TOP,
            width=WIDTH,
            height=100,
            frameless=True,
            easy_drag=False,
            on_top=True,
            focus=False,
            transparent=True,
            hidden=True,
        )

    def toast(self, text):
        self.call_js("showToast", text)

    def show(self, title, content, actions=None):
        self.call_js("showPanel", render_panel(title, content, actions))

    def close(self):
        self.window.destroy()

    def call_js(self, name, arg):
        self.window.evaluate_js(f"{name}({json.dumps(arg)})")

    def fit(self, height):
        """Resize to the page's content, or hide when there is nothing to show."""
        if height <= 0:
            self.window.hide()
            return
        self.window.resize(WIDTH, height, FixPoint.NORTH | FixPoint.EAST)
        # orderFrontRegardless shows the window without activating Orbi or taking focus
        AppHelper.callAfter(self.window.native.orderFrontRegardless)

    def run(self):
        """Blocks until the window closes; must be called on the main thread."""
        webview.start(hide_dock_icon)


def hide_dock_icon():
    app = AppKit.NSApplication.sharedApplication()
    AppHelper.callAfter(app.setActivationPolicy_, AppKit.NSApplicationActivationPolicyAccessory)
