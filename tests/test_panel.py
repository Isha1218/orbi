from orbi.panel import render_markdown, render_panel


def test_bold_and_list():
    out = render_markdown("Move the **demo**.\n\n- one\n- two\n")
    assert "<strong>demo</strong>" in out
    assert "<li>one</li>" in out


def test_fenced_code_is_escaped():
    out = render_markdown("```python\nif a < b:\n    pass\n```\n")
    assert "<pre><code" in out
    assert "a &lt; b" in out


def test_table():
    out = render_markdown("| a | b |\n| --- | --- |\n| 1 | 2 |\n")
    assert "<table>" in out
    assert "<td>1</td>" in out


def test_links_and_images():
    out = render_markdown("[site](https://example.com) ![cat](https://example.com/cat.png)")
    assert '<a href="https://example.com">site</a>' in out
    assert '<img alt="cat" src="https://example.com/cat.png"' in out


def test_raw_html_is_not_run():
    out = render_markdown("<script>alert(1)</script>\n\nhi <b onclick='x'>there</b>")
    assert "<script>" not in out
    assert "<b " not in out


def test_title_and_actions_are_escaped():
    actions = [{"label": "Open <Gmail>", "url": 'https://x.com/?a=1&b="2"'}]
    out = render_panel("Re: <demo>", "text", actions)
    assert "<h1>Re: &lt;demo&gt;</h1>" in out
    assert 'data-url="https://x.com/?a=1&amp;b=&quot;2&quot;"' in out
    assert ">Open &lt;Gmail&gt;</button>" in out


def test_no_actions():
    assert "<button" not in render_panel("Title", "text")
