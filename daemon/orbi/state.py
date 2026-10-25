"""World state, and the priority layers that pick exactly one thing for the face to show."""
from dataclasses import dataclass, field


@dataclass
class Agent:
    id: str
    task: str
    status: str = "working"  # working | needs_input


@dataclass
class News:
    agent_id: str
    ok: bool
    command: dict = None  # a plugin's scene, card or expression to show instead


@dataclass
class WorldState:
    conversation: str = None   # listening | thinking | confused
    face_content: dict = None  # a scene or card command, shown for a few seconds
    agents: list = field(default_factory=list)
    unread: list = field(default_factory=list)
    ambient: list = field(default_factory=list)  # plugin commands on the ambient layer


def current_layer(s):
    """Returns an expression name, or the scene/card command on display."""
    if s.conversation:
        return s.conversation
    if s.face_content:
        return s.face_content
    if any(a.status == "needs_input" for a in s.agents):
        return "expectant"
    if s.unread:
        if s.unread[0].command:
            return s.unread[0].command
        if s.unread[0].ok:
            return "pleased"
        return "worried"
    if any(a.status == "working" for a in s.agents):
        return "busy"
    if s.ambient:
        return s.ambient[0]
    return "idle"
