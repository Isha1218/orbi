"""Keeps the world state's agents and news in step with the agent.* events from any harness."""
import asyncio

from .state import Agent, News

NEWS_SECONDS = 10.0


class Agents:
    def __init__(self, daemon):
        self.daemon = daemon
        daemon.on("agent.started", self.on_started)
        daemon.on("agent.working", self.on_working)
        daemon.on("agent.needs_input", self.on_needs_input)
        daemon.on("agent.done", self.on_finished)
        daemon.on("agent.failed", self.on_finished)

    def find(self, agent_id):
        for agent in self.daemon.state.agents:
            if agent.id == agent_id:
                return agent
        return None

    def on_started(self, event):
        self.daemon.state.agents.append(Agent(event["id"], event["task"]))

    def on_working(self, event):
        agent = self.find(event["id"])
        if agent:
            agent.status = "working"

    def on_needs_input(self, event):
        agent = self.find(event["id"])
        if agent:
            agent.status = "needs_input"

    def on_finished(self, event):
        agent = self.find(event["id"])
        if agent:
            self.daemon.state.agents.remove(agent)
        news = News(event["id"], event["type"] == "agent.done")
        self.daemon.state.unread.append(news)
        asyncio.get_running_loop().call_later(NEWS_SECONDS, self.clear_news, news)

    def clear_news(self, news):
        if news in self.daemon.state.unread:
            self.daemon.state.unread.remove(news)
            self.daemon.refresh()
