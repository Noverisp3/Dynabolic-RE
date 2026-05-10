"""LLM provider abstraction.

A `LLMProvider` is anything that can take a system prompt + user prompt and
return a string completion. Three real providers ship today (Ollama, OpenAI,
Anthropic) plus a `MockProvider` for tests. All use stdlib `urllib` so the
package has no third-party dependencies.

Env vars:
    OLLAMA_HOST       (default: http://localhost:11434)
    OLLAMA_MODEL      (default: llama3.1:8b)
    OPENAI_API_KEY    (required for OpenAI)
    OPENAI_MODEL      (default: gpt-4o-mini)
    ANTHROPIC_API_KEY (required for Anthropic)
    ANTHROPIC_MODEL   (default: claude-3-5-haiku-latest)

`from_env()` picks a provider based on `DYNABOLIC_LLM_PROVIDER`
(`ollama` | `openai` | `anthropic` | `mock`). Defaults to `ollama`.
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Callable, Protocol


class ProviderError(RuntimeError):
    """Raised when an LLM call fails (network, auth, malformed response)."""


class LLMProvider(Protocol):
    """Anything that turns (system, user) prompts into a completion string."""

    name: str

    def complete(self, system: str, user: str, *, temperature: float = 0.0) -> str:
        ...


# --- HTTP helper -------------------------------------------------------------

def _post_json(url: str, body: dict, headers: dict, timeout: float = 120.0) -> dict:
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        body_text = e.read().decode("utf-8", errors="replace")
        raise ProviderError(f"HTTP {e.code} from {url}: {body_text}") from e
    except urllib.error.URLError as e:
        raise ProviderError(f"network error contacting {url}: {e.reason}") from e
    try:
        return json.loads(raw)
    except json.JSONDecodeError as e:
        raise ProviderError(f"non-JSON response from {url}: {raw[:200]!r}") from e


# --- Providers ---------------------------------------------------------------

@dataclass
class OllamaProvider:
    """Local Ollama server (default).

    Talks to the /api/chat endpoint. Make sure `ollama serve` is running and
    the model is pulled (`ollama pull llama3.1:8b`).
    """

    model: str = field(default_factory=lambda: os.environ.get("OLLAMA_MODEL", "llama3.1:8b"))
    host: str = field(default_factory=lambda: os.environ.get("OLLAMA_HOST", "http://localhost:11434"))
    name: str = "ollama"

    def complete(self, system: str, user: str, *, temperature: float = 0.0) -> str:
        body = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "stream": False,
            "options": {"temperature": temperature},
        }
        data = _post_json(f"{self.host}/api/chat", body, headers={"Content-Type": "application/json"})
        try:
            return data["message"]["content"]
        except (KeyError, TypeError) as e:
            raise ProviderError(f"unexpected Ollama response shape: {data!r}") from e


@dataclass
class OpenAIProvider:
    """OpenAI Chat Completions. Needs OPENAI_API_KEY in env."""

    model: str = field(default_factory=lambda: os.environ.get("OPENAI_MODEL", "gpt-4o-mini"))
    api_key: str = field(default_factory=lambda: os.environ.get("OPENAI_API_KEY", ""))
    base_url: str = field(default_factory=lambda: os.environ.get("OPENAI_BASE_URL", "https://api.openai.com/v1"))
    name: str = "openai"

    def __post_init__(self) -> None:
        if not self.api_key:
            raise ProviderError("OPENAI_API_KEY not set")

    def complete(self, system: str, user: str, *, temperature: float = 0.0) -> str:
        body = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "temperature": temperature,
        }
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.api_key}",
        }
        data = _post_json(f"{self.base_url}/chat/completions", body, headers=headers)
        try:
            return data["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as e:
            raise ProviderError(f"unexpected OpenAI response shape: {data!r}") from e


@dataclass
class AnthropicProvider:
    """Anthropic Messages API. Needs ANTHROPIC_API_KEY in env."""

    model: str = field(default_factory=lambda: os.environ.get("ANTHROPIC_MODEL", "claude-3-5-haiku-latest"))
    api_key: str = field(default_factory=lambda: os.environ.get("ANTHROPIC_API_KEY", ""))
    base_url: str = field(default_factory=lambda: os.environ.get("ANTHROPIC_BASE_URL", "https://api.anthropic.com/v1"))
    max_tokens: int = 4096
    name: str = "anthropic"

    def __post_init__(self) -> None:
        if not self.api_key:
            raise ProviderError("ANTHROPIC_API_KEY not set")

    def complete(self, system: str, user: str, *, temperature: float = 0.0) -> str:
        body = {
            "model": self.model,
            "system": system,
            "messages": [{"role": "user", "content": user}],
            "max_tokens": self.max_tokens,
            "temperature": temperature,
        }
        headers = {
            "Content-Type": "application/json",
            "x-api-key": self.api_key,
            "anthropic-version": "2023-06-01",
        }
        data = _post_json(f"{self.base_url}/messages", body, headers=headers)
        try:
            # Anthropic returns content as a list of blocks; we only use text blocks.
            blocks = data["content"]
            return "".join(b.get("text", "") for b in blocks if b.get("type") == "text")
        except (KeyError, TypeError) as e:
            raise ProviderError(f"unexpected Anthropic response shape: {data!r}") from e


@dataclass
class MockProvider:
    """Deterministic provider for tests and offline demos.

    Construct with either:
        - a dict mapping (system, user) -> response, OR
        - a callable taking (system, user) -> response.

    Useful for unit tests without spinning up a real LLM.
    """

    responder: "Callable[[str, str], str] | dict[tuple[str, str], str]"
    name: str = "mock"

    def complete(self, system: str, user: str, *, temperature: float = 0.0) -> str:
        del temperature  # mocks are deterministic
        if callable(self.responder):
            return self.responder(system, user)
        try:
            return self.responder[(system, user)]
        except KeyError as e:
            raise ProviderError(
                f"MockProvider has no canned response for (system, user) keys; "
                f"got system={system!r} user={user!r}"
            ) from e


# --- Factory -----------------------------------------------------------------

def from_env() -> LLMProvider:
    """Pick a provider based on DYNABOLIC_LLM_PROVIDER. Defaults to ollama."""

    name = os.environ.get("DYNABOLIC_LLM_PROVIDER", "ollama").lower()
    if name == "ollama":
        return OllamaProvider()
    if name == "openai":
        return OpenAIProvider()
    if name == "anthropic":
        return AnthropicProvider()
    if name == "mock":
        return MockProvider(responder=lambda s, u: "")
    raise ProviderError(
        f"unknown DYNABOLIC_LLM_PROVIDER={name!r}; "
        f"expected one of: ollama, openai, anthropic, mock"
    )
