"""Shows an email the agent has read: the sender on the face, the summary on the screen.
It doesn't read mail itself; the agent does that with its own tools."""
from orbi.plugins import define_plugin

DESCRIPTION = """Show an email you have read to the user: the sender's initials (or avatar) and a short label on the face, and your summary in a panel on the screen.
- sender: e.g. "Sarah Johnson <sarah@example.com>".
- subject: the email's subject, used as the panel title.
- summary_markdown: your summary, in markdown (a few bullets).
- label: at most 12 characters for the face, the gist, e.g. "Demo → Fri".
- url: optional link to open the email, e.g. its Gmail URL.
- avatar: optional image URL or local file path for the sender."""


def initials(sender):
    """"Sarah Johnson <sarah@example.com>" -> "SJ"."""
    name = sender.split("<")[0].strip()
    if "@" in name:
        name = name.split("@")[0]
    words = name.replace(".", " ").replace("_", " ").split()
    letters = ""
    for word in words[:2]:
        if word[0].isalpha():
            letters += word[0].upper()
    return letters


def setup(ctx):
    async def show_email(
        sender: str,
        subject: str,
        summary_markdown: str,
        label: str = "",
        url: str | None = None,
        avatar: str | None = None,
    ) -> str:
        if avatar:
            card = {"avatar": avatar}
        elif initials(sender):
            card = {"initials": initials(sender)}
        else:
            card = {"icon": "mail"}
        face_result = await ctx.face.show(card=card, label=label)

        actions = []
        if url:
            button = "Open email"
            if "mail.google.com" in url:
                button = "Open in Gmail"
            actions.append({"label": button, "url": url})
        content = f"**From:** {sender}\n\n{summary_markdown}"
        ctx.screen.show(content, title=subject, actions=actions)
        return f"{face_result} The summary is on the screen."

    ctx.tools.register("show_email", show_email, description=DESCRIPTION)


plugin = define_plugin(name="email", setup=setup)
