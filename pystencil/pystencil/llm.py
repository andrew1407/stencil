"""LLM assistant support: provider client, op-plan parsing, and plan execution.

The Python implementation of the shared Stencil LLM contract — ``llm-contract.md``
is authoritative for the op-plan schema/limits, system prompt, provider config
(``STENCIL_LLM_*`` env keys), wire mappings, and history rules. The model never touches
pixels: it answers with an *op-plan* whose actions map 1:1 onto existing
:class:`~pystencil.editor.Editor` methods, driven by :func:`execute_op_plan`. All
network goes through :class:`LlmClient`'s private ``_open`` seam — built on the same
shared urllib plumbing (and 30 s timeout) as :mod:`pystencil.server` — so tests stay
offline.

One documented deviation from contract §7: pystencil has no image resampling (the core
is deliberately resize-free), so attached images are NOT downscaled to 1568 px — callers
pass reasonably-sized images. Media types, the 32-message bound, and the image replay
rule match the contract.
"""

from __future__ import annotations

import base64
import importlib.resources
import json
import os
import re
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, FrozenSet, Iterable, List, Optional, Sequence, Tuple

from ._opschema import SchemaError, schema
from .layout import Line

# The urllib plumbing is shared with the server client: one request builder, one
# network seam, one {code, message} error-body parser — each client only supplies its
# own exception type and its own timeout.
from .server import _http_open, _json_request, _parse_http_error, _LLM_TIMEOUT


# ── contract limits (§1/§7/§11 — the same numbers in every client) ────────────
# Read from the checked-in copy of the canonical op registry
# (browser/js/config/llm/opRegistry.json; tests/test_canonical_drift.py byte-pins the
# copy) through the table-driven schema engine every op validator runs on.
SCHEMA = schema("pystencil")
_LIMITS = SCHEMA.limits
MAX_ACTIONS = _LIMITS["MAX_ACTIONS"]              # per plan: top-level and per variant
MAX_VARIANTS = _LIMITS["MAX_VARIANTS"]
MAX_LAYOUT_LINES = _LIMITS["MAX_LAYOUT_LINES"]
MAX_STRING_LENGTH = _LIMITS["MAX_STRING_CHARS"]   # per string field unless its spec caps it
MAX_FRAME_INDICES = _LIMITS["MAX_FRAME_INDICES"]
MAX_SAVE_NAME = _LIMITS["MAX_SAVE_NAME"]          # §2.1: the `save` op's optional project name
MAX_PATH_CHARS = _LIMITS["MAX_PATH_CHARS"]        # §10: the longest local path a `save` op may carry
# §11 interactive replies — the same numbers as every other client.
MIN_ASK_OPTIONS = _LIMITS["ask"]["minOptions"]
MAX_ASK_OPTIONS = _LIMITS["ask"]["maxOptions"]
MAX_ASK_QUESTION = _LIMITS["ask"]["question"]
MAX_ASK_LABEL = _LIMITS["ask"]["label"]
MAX_ASK_ANSWER = _LIMITS["ask"]["answer"]
DEFAULT_CUSTOM_LABEL = SCHEMA.registry["ask"]["defaultCustomLabel"]
MAX_HISTORY = 32        # chat messages replayed per call
# §12 chat persistence — the persisted-chat document version Chat.to_doc writes and
# Chat.from_doc accepts (any other version is treated as "no saved chat").
CHAT_DOC_VERSION = 1
# §7's auto-continuation note: the internal sentence the console appends to the RESTATED
# request after a plan made a new picture. It lives beside the §12 rules so the one place
# that writes it (cli.py) and the one that must never persist it agree by construction.
CONTINUATION_NOTE = (
    "[The working image is now the picture that action just made — "
    "continue with it, using its real pixel size.]"
)
_CONTINUATION_OPEN = "[The working image is now"
_PLAN_VERSION_KEY = re.compile(r'"version"\s*:')
_PLAN_FIELD_KEY = re.compile(r'"(?:actions|reply|variants|ask)"\s*:')


def chat_display_text(role: str, text: str) -> Optional[str]:
    """The §12.1 text to persist/restore for one turn, or None when it is dropped.

    The document is SHARED across surfaces and "a restored transcript must read as a
    conversation", so machinery never enters it. Applied on BOTH sides (``Chat.to_doc``
    and ``Chat.from_doc``, plus the editor's ``chat`` block), so a document from another
    surface or an older build can't be replayed as the user's own words:

    * §7's continuation note — appended to the restated request (stripped, the request
      stays) or standing alone in any bracketed variant (the turn is dropped);
    * an assistant turn that is a raw op-plan — §7 permits that on the WIRE, §12.1 does
      not. Assistant turns only: a user may paste JSON and see it again.
    """
    t = str(text or "").strip()
    if t.endswith("]"):
        at = t.rfind(_CONTINUATION_OPEN)
        if at != -1:
            t = t[:at].rstrip()
    if not t:
        return None
    if role == "assistant" and t[0] in "{[" and _PLAN_VERSION_KEY.search(t) and _PLAN_FIELD_KEY.search(t):
        return None
    return t

# How many images ONE message may carry (contract §7; the browser/extension
# MAX_ATTACHMENTS and the desktop's kMaxAttachments). The working image rides along on
# top of this and does not count against it. This cap binds the attachment-QUEUE
# surfaces (Chat.send's user images); the console's /upload set below has the cli's own.
MAX_ATTACHMENTS = 3

# The console's §2.1 upload set: how many /upload-ed images one turn may index with an
# `image` op (the cli console's max_attachments). Past it the oldest upload falls off.
MAX_UPLOAD_ATTACHMENTS = 8

# Media types the contract accepts for attached images (§7).
ACCEPTED_MEDIA_TYPES = ("image/png", "image/jpeg", "image/webp", "image/gif")

# Every LLM call is bounded by pystencil.server's _LLM_TIMEOUT (the asset's
# timeouts.chatSeconds) so a hostile/slow/hung provider can't block the caller.

# Providers + their pre-filled default base URLs (contract §5), from the checked-in
# copy of the canonical providers asset (browser/js/config/llm/providers.json;
# tests/test_canonical_drift.py byte-pins the copy). stencil-server's null default
# drops out of DEFAULT_BASE_URLS — it uses server_url + the existing bearer token.
_PROVIDERS_ASSET = json.loads(
    importlib.resources.files("pystencil")
    .joinpath("_data/providers.json")
    .read_text(encoding="utf-8")
)
PROVIDERS = tuple(_PROVIDERS_ASSET["providers"])
DEFAULT_BASE_URLS = {
    name: p["defaultBaseUrl"]
    for name, p in _PROVIDERS_ASSET["providers"].items()
    if p["defaultBaseUrl"]
}


# ── canonical system prompt (contract §4 + §13) ───────────────────────────────
# The prompt has two parts with different sync rules (§4): the PROSE CORE comes from
# the checked-in copy of the canonical asset (browser/js/config/llm/systemPrompt.json;
# tests/test_canonical_drift.py byte-pins the copy), while the "Available ops" list is
# GENERATED from OP_REGISTRY (defined after the validators/appliers it references) —
# the same table that validation and execution dispatch on, so the prompt can never
# promise an op this surface cannot run. LLM_SYSTEM_PROMPT / CONSOLE_SETTINGS_PROMPT /
# CONSOLE_SYSTEM_PROMPT are assembled right after the registry.
_PROMPT_ASSET = json.loads(
    importlib.resources.files("pystencil")
    .joinpath("_data/systemPrompt.json")
    .read_text(encoding="utf-8")
)

_PROMPT_CORE_HEAD = _PROMPT_ASSET["head"]

# This text console deliberately diverges from the canonical tail's "ask" paragraph
# (no previews, no image options — labels must stand alone); the shared prose from
# "Outlining" on is the asset's, verbatim.
_CONSOLE_ASK = """When a choice is genuinely the user's to make — which tint, which of several images —
add an "ask" object instead of guessing:
{"ask":{"question":"Which tint?","mode":"single"|"multi","allowCustom":true,
  "options":[{"label":"Sepia"},{"label":"B&W"}]}}
2 to 5 options; the pick comes back as the user's next message. This console shows the
options as a numbered list and cannot display pictures, so make each label stand alone.
Never write your own "Something else" / "Other" option: set "allowCustom": true and the client appends that free-text row itself."""

_TAIL_SHARED_ANCHOR = "\n\nOutlining ("
_shared_at = _PROMPT_ASSET["tail"].find(_TAIL_SHARED_ANCHOR)
if _shared_at < 0:  # pragma: no cover - guards asset rewording
    raise AssertionError("systemPrompt.json tail no longer contains the Outlining anchor")
_PROMPT_CORE_TAIL = _CONSOLE_ASK + _PROMPT_ASSET["tail"][_shared_at:]


# ── the console settings-op profile (the §10 cli-console analog) ──────────────
# The pystencil console carries the SAME profile as the cli console (contract §10)
# minus accent/reconnect (no theme, no reconnect command) and copy (no clipboard) —
# those capabilities are not wired here, so per §13 they simply have no OP_REGISTRY
# entries and their bullets are never generated. The console bullets live on their
# registry entries (scope "console"); only the block's closing sentence is prose.
_CONSOLE_BLOCK_TAIL = (
    'These console ops are not image edits and cannot appear inside "variants".'
)

# The op list's end = the `ask` paragraph's start (the same splice anchor the cli's
# console_system_prompt and the browser's EDITOR_SYSTEM_PROMPT verify).
CONSOLE_SPLICE_ANCHOR = "\n\nWhen a choice is genuinely"


# §7 edge map: appended (verbatim) to the system-prompt suffix when — and only
# when — an edge-map image is actually attached after the working snapshot.
EDGE_MAP_SUFFIX = (
    "The second attached image is an edge-map render of the working image at the "
    "same pixel coordinates: use it to place outline points on real edges."
)

# ── errors ────────────────────────────────────────────────────────────────────
#: How much of a provider's own prose an error may quote (contract §6.3).
MAX_PROVIDER_DETAIL = 200

_CONTROLISH = re.compile(r"[\x00-\x1f\x7f]")
_URLISH = re.compile(r"[a-z][a-z0-9+.-]*://\S+", re.I)
_SECRETISH = re.compile(
    r"(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}"
    r"|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}"
    r"|[A-Za-z0-9_-]{24,}",
    re.I,
)


def _clean_detail(text: str) -> str:
    """Untrusted provider prose made safe to print: control characters out, URLs and
    token-shaped runs redacted (an endpoint may echo the key back), whitespace
    collapsed, hard-truncated. Port of the server's ``sanitizeUpstreamText``."""
    t = _CONTROLISH.sub(" ", text[: 4 * MAX_PROVIDER_DETAIL])
    t = _SECRETISH.sub("[redacted]", _URLISH.sub("[redacted]", t))
    t = " ".join(t.split())
    return t if len(t) <= MAX_PROVIDER_DETAIL else t[: MAX_PROVIDER_DETAIL - 1].strip() + "…"


class LlmError(Exception):
    """An LLM transport/provider failure (non-2xx, malformed payload, or a
    stencil-server ``stopReason`` of ``max_tokens``/``refusal`` — which per contract
    §6.3 must surface as an error, never be parsed as a plan)."""

    def __init__(
        self,
        message: str,
        *,
        code: str = "",
        status: Optional[int] = None,
        stop_reason: Optional[str] = None,
    ) -> None:
        super().__init__(message)
        self.message = message
        self.code = code
        self.status = status
        self.stop_reason = stop_reason


class LlmPlanError(LlmError):
    """A found op-plan JSON object failed strict validation (contract §1/§2): a
    missing/empty reply, a known op with invalid params, or an exceeded limit.
    Nothing executes when this is raised."""


class LlmExecutionError(LlmError):
    """A validated plan could not be executed on this surface (e.g. the ``frame``
    op, which needs a video input pystencil cannot decode)."""


# ── provider configuration (contract §5) ──────────────────────────────────────
@dataclass
class LlmConfig:
    """Which LLM endpoint to talk to — the same shape every client shares.

    ``base_url``/``model``/``api_key`` apply to ``ollama``/``openai-compat`` (the key
    is optional and sent as a Bearer header on ``openai-compat`` only); ``server_url``
    applies to ``stencil-server`` only (the collaboration server proxying Anthropic,
    authenticated with the existing session token). An empty ``base_url`` is pre-filled
    with the provider's contract default.
    """

    provider: str = "ollama"
    base_url: str = ""
    model: str = ""
    api_key: str = ""
    server_url: str = ""

    def __post_init__(self) -> None:
        self.provider = (self.provider or "ollama").strip().lower()
        if self.provider not in PROVIDERS:
            raise ValueError(
                "unknown LLM provider %r — use one of: %s"
                % (self.provider, ", ".join(PROVIDERS))
            )
        if not self.base_url:
            self.base_url = DEFAULT_BASE_URLS.get(self.provider, "")
        # True once a caller pins an explicit base URL via set_base_url();
        # set_provider() then keeps it instead of re-filling the provider default.
        self._url_pinned = False

    def set_base_url(self, url: str) -> "LlmConfig":
        """Pin an explicit base URL (a user override): :meth:`set_provider` keeps a
        pinned URL across provider switches instead of re-filling the default."""
        self.base_url = url
        self._url_pinned = True
        return self

    def set_provider(
        self, provider: str, *, keep_url: Optional[bool] = None
    ) -> "LlmConfig":
        """Switch providers in place, managing the default base URL (this module owns
        :data:`DEFAULT_BASE_URLS`).

        Unless the URL is kept, ``base_url`` is re-filled with the new provider's
        contract default. ``keep_url`` defaults to whether the current URL was pinned
        via :meth:`set_base_url`; pass an explicit bool to override. An unknown
        provider raises ``ValueError`` and changes nothing.
        """
        p = (provider or "").strip().lower()
        if p not in PROVIDERS:
            raise ValueError(
                "unknown LLM provider %r — use one of: %s" % (provider, ", ".join(PROVIDERS))
            )
        self.provider = p
        if not (self._url_pinned if keep_url is None else keep_url):
            self.base_url = DEFAULT_BASE_URLS.get(p, "")
        return self

    @classmethod
    def from_env(cls, env: Optional[dict] = None) -> "LlmConfig":
        """Build a config from the ``STENCIL_LLM_*`` environment keys (contract §5).

        ``env`` defaults to ``os.environ``; pass a mapping to test without touching
        the process environment. Missing/blank keys fall back to the defaults.
        """
        e = os.environ if env is None else env
        return cls(
            provider=(e.get("STENCIL_LLM_PROVIDER") or "").strip() or "ollama",
            base_url=(e.get("STENCIL_LLM_BASE_URL") or "").strip(),
            model=(e.get("STENCIL_LLM_MODEL") or "").strip(),
            api_key=(e.get("STENCIL_LLM_API_KEY") or "").strip(),
            server_url=(e.get("STENCIL_LLM_SERVER_URL") or "").strip(),
        )


# ── op-plan value types ───────────────────────────────────────────────────────
@dataclass
class Variant:
    """One alternative branch: a label (used for file/project naming) + its actions.

    Each variant starts from the image state AFTER the plan's top-level actions and
    yields one extra output image.
    """

    label: str
    actions: List[dict] = field(default_factory=list)


def variant_slug(label: str) -> str:
    """Sanitize a variant label into a filename slug (``[a-z0-9-]``, runs of other
    characters collapsed to a dash, "variant" fallback) — the Python counterpart of
    the Zig CLI's ``sanitizeLabel`` and mcp's ``sanitize_label``."""
    s = re.sub(r"[^a-z0-9-]+", "-", (label or "").lower()).strip("-")
    return s or "variant"


def variant_slugs(variants: Sequence["Variant"]) -> List[str]:
    """Unique file slugs for a plan's variants, in order.

    Each label goes through :func:`variant_slug`; a slug already taken gains the first
    free ``-2``/``-3``/… suffix so same-slug labels ("Rotated!" vs "rotated") don't
    silently overwrite each other's output files — the same dedupe the Zig CLI's
    ``variantStem`` and mcp's ``to_edit_requests`` apply.
    """
    taken: set = set()
    out: List[str] = []
    for v in variants:
        slug = variant_slug(v.label)
        if slug in taken:
            n = 2
            while "%s-%d" % (slug, n) in taken:
                n += 1
            slug = "%s-%d" % (slug, n)
        taken.add(slug)
        out.append(slug)
    return out


@dataclass
class AskOption:
    """One choice on an :class:`AskCard` (contract §11).

    A console cannot show a picture, so an option's preview — a render spec or an image
    reference — is dropped at parse time and only ``label`` survives (§11.4). The option
    itself is never dropped.
    """

    label: str


@dataclass
class AskCard:
    """A question put back to the user (contract §11).

    Rendered as a numbered list and answered by number on the next prompt; ``multi`` marks a
    card that takes several picks, ``allow_custom`` one that also accepts free text.
    """

    question: str
    multi: bool = False
    allow_custom: bool = False
    custom_label: str = DEFAULT_CUSTOM_LABEL
    options: List[AskOption] = field(default_factory=list)


@dataclass
class OpPlan:
    """A validated op-plan (contract §1): the chat reply plus whitelisted actions.

    ``actions``/``variants[i].actions`` hold normalized action dicts that passed the
    strict per-op validation; ``warnings`` lists any unknown ops that were dropped
    (they are also appended to ``reply``). A chat-only turn is a plan with the raw
    text as ``reply`` and no actions/variants. ``saved`` is filled in by
    :func:`execute_op_plan` with the ``.stencil`` paths the plan's §2.1 ``save``
    actions wrote.
    """

    reply: str
    actions: List[dict] = field(default_factory=list)
    variants: List[Variant] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    ask: Optional["AskCard"] = None
    saved: List[str] = field(default_factory=list)


# ── op-plan parsing (contract §1/§2/§3) ───────────────────────────────────────
# Field schemas, token grammars and the cross-field rules are the registry's
# (SCHEMA); only the per-op normalizers below are this surface's own.

# _ACTION_FIELDS / _ACTION_VALIDATORS / _ACTION_APPLIERS / _TOP_LEVEL_ONLY_OPS /
# _CONSOLE_SETTINGS_OPS are all DERIVED from OP_REGISTRY (contract §13) — defined
# after the appliers below, which the registry entries reference.


def _strip_fences(text: str) -> str:
    """Drop Markdown code-fence lines (``` / ```json) before JSON extraction."""
    return "\n".join(
        line for line in text.split("\n") if not line.strip().startswith("```")
    )


def _balanced_end(text: str, start: int) -> Optional[int]:
    """Index of the ``}`` closing the ``{`` at ``start`` (string/escape aware)."""
    depth = 0
    in_string = False
    escape = False
    for i in range(start, len(text)):
        ch = text[i]
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
    return None


def _first_json_object(text: str) -> Optional[dict]:
    """The first balanced ``{…}`` in ``text`` that parses as a JSON object, or None."""
    i = text.find("{")
    while i != -1:
        end = _balanced_end(text, i)
        if end is not None:
            try:
                obj = json.loads(text[i : end + 1])
            except ValueError:
                obj = None
            if isinstance(obj, dict):
                return obj
        i = text.find("{", i + 1)
    return None


# ── per-op normalizers: the typed struct filling the generic deep-pick can't express ──
# Each runs on the registry-normalized action — {op, declared keys present, defaults,
# `trim` keys trimmed} — AFTER the table-driven check passed; nothing here validates.
def _normalize_crop(out: dict) -> dict:
    """The console-only ``"album": false`` means "no derivation" — dropped from the spec."""
    if out["spec"].get("album") is False:
        del out["spec"]["album"]
    return out


def _float_dims(out: dict) -> dict:
    """§2 centimetre dims are floats (page / blank)."""
    for key in ("width", "height"):
        if key in out:
            out[key] = float(out[key])
    return out


def _strip_field(key: str) -> Callable[[dict], dict]:
    """Store a padded string field trimmed (connect/disconnect server, delete path);
    resolution against the console's own state happens at execution."""

    def normalize(out: dict) -> dict:
        out[key] = out[key].strip()
        return out

    return normalize


def _normalize_save(out: dict) -> dict:
    """An empty (or all-space) path is no destination at all; this executor
    notes+skips a path anyway."""
    if out.get("path") == "":
        del out["path"]
    return out


def _normalize_open_url(out: dict) -> dict:
    """``incognito`` always rides the action (False when omitted). Whether the USER
    wrote the URL is the plan-level guard's job (:func:`url_echoed_by_user`)."""
    out.setdefault("incognito", False)
    return out


class _MisplacedOp(Exception):
    """§1's one exception: a top-level-only or console-settings op inside a variant.
    Costs that variant its place (the caller turns this into a warning), never the plan."""

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


def _plan_check(check: Callable[[], None]) -> None:
    """Run a plan-level registry check; a failure is the uniform plan error."""
    try:
        check()
    except SchemaError as e:
        raise LlmPlanError(str(e)) from None


def _validate_actions(raw: Any, warnings: List[str], where: str) -> List[dict]:
    """Validate an actions array: unknown ops are dropped with a warning (forward
    compatibility); a known op with invalid params fails the whole plan. A misplaced
    top-level-only/console op raises :class:`_MisplacedOp` — the variant goes, not the plan."""
    if raw is None:
        return []
    _plan_check(lambda: SCHEMA.check_envelope(raw, "actions"))
    out: List[dict] = []
    for a in raw:
        op = a.get("op")
        if not isinstance(op, str) or not op:
            raise LlmPlanError('an action in "%s" is missing its "op"' % where)
        if op not in OP_REGISTRY:
            warnings.append('unknown op "%s" dropped' % op)
            continue
        if op in _TOP_LEVEL_ONLY_OPS and where != "actions":
            raise _MisplacedOp(
                'the "%s" op is top-level only and cannot appear in a variant' % op
            )
        if op in _CONSOLE_SETTINGS_OPS and where != "actions":
            raise _MisplacedOp(
                'the "%s" op adjusts the console, not the image, and cannot appear '
                "in a variant" % op
            )
        out.append(_ACTION_VALIDATORS[op](a))
    return out


def parse_op_plan(text: str) -> OpPlan:
    """Parse raw LLM reply text into a validated :class:`OpPlan` (contract §1).

    Extraction is tolerant: Markdown code fences are stripped and the first balanced
    ``{…}`` JSON object is taken. Text with no JSON object at all is a *chat-only*
    turn — the raw text becomes ``reply`` with zero actions (not an error). Once an
    object is found, validation is strict: ``reply`` must be a non-empty string, every
    action must validate per §2 (unknown ops are dropped with a warning appended to
    the reply; a known op with invalid params raises :class:`LlmPlanError`), and the
    shared limits apply. ``version`` other than 1 (or absent) is accepted but ignored.

    §1's one exception to that strictness: a variant holding a top-level-only or
    console-settings op is dropped with a warning naming it, and the rest of the plan
    still runs — one misplaced op must not cost the user the whole turn.
    """
    raw = text if isinstance(text, str) else str(text)
    obj = _first_json_object(_strip_fences(raw))
    if obj is None:
        return OpPlan(reply=raw.strip())
    reply = obj.get("reply")
    warnings: List[str] = []
    # §1 reply tolerance: models routinely omit the reply while planning valid
    # actions — substitute rather than lose the plan to a missing pleasantry.
    # The substitute itself is decided below, once the plan's contents are known.
    reply_omitted = not isinstance(reply, str) or not reply.strip()
    actions = _validate_actions(obj.get("actions"), warnings, "actions")
    variants: List[Variant] = []
    raw_variants = obj.get("variants")
    if raw_variants is not None:
        # The registry envelope: ≤ MAX_VARIANTS objects of {label: string, actions}.
        _plan_check(lambda: SCHEMA.check_envelope(raw_variants, "variants"))
        for i, rv in enumerate(raw_variants):
            label = (rv.get("label") or "").strip()
            try:
                v_actions = _validate_actions(
                    rv.get("actions"), warnings, "variants[%d]" % i
                )
            except _MisplacedOp as e:
                # §1: drop THIS variant with a warning naming it; the rest still runs.
                named = 'variant %d ("%s")' % (i + 1, label) if label else "variant %d" % (i + 1)
                warnings.append("dropped %s — %s" % (named, e.reason))
                continue
            variants.append(
                Variant(label=label or "variant %d" % (i + 1), actions=v_actions)
            )
    ask = _validate_ask(obj.get("ask"), warnings)
    # "Done." only when the plan actually carries work — a bare "Done." on an
    # empty plan reads as a success that never occurred (contract §1).
    if reply_omitted:
        if actions or variants or ask is not None:
            reply = "Done."
            warnings.append("The model omitted its reply — the plan still ran")
        else:
            reply = "The model returned an empty plan — nothing was changed."
    if warnings:
        reply = reply + "\n" + "\n".join("[warning] " + w for w in warnings)
    return OpPlan(
        reply=reply, actions=actions, variants=variants, warnings=warnings, ask=ask
    )


# ── §11 interactive replies (`ask`) ───────────────────────────────────────────
def _validate_ask(raw: Any, warnings: List[str]) -> Optional[AskCard]:
    """Validate the optional ``ask`` object (contract §11) → the card, or ``None``.

    The card's structure — keys, caps, 2..5 options, an image reference's exactly-one-of
    url / projectId / scanIndex, http(s)-only urls — is the registry's ask schema, checked
    on every surface even where nothing renders. Option previews are ordinary §2 actions,
    validated as "inside variants or previews": a misplaced op costs the option its
    preview (a warning), an invalid one the plan. A console has nowhere to show a preview,
    so it is dropped after validation — with ONE note for the card, never one per option —
    and only ``label`` survives.
    """
    if raw is None:
        return None
    _plan_check(lambda: SCHEMA.validate_ask(raw))
    card = SCHEMA.normalize_ask(raw)
    options: List[AskOption] = []
    dropped_preview = False
    for i, (ro, opt) in enumerate(zip(raw["options"], card["options"])):
        if ro.get("actions") is not None:
            try:
                _validate_actions(ro["actions"], warnings, "ask option %d" % (i + 1))
            except _MisplacedOp:
                pass  # the preview is dropped below anyway — one card-level note
        if ro.get("actions") is not None or ro.get("image") is not None:
            dropped_preview = True
        options.append(AskOption(label=opt["label"]))
    if dropped_preview:
        warnings.append(
            "the console can't show option previews — the choices are listed by name"
        )
    return AskCard(
        question=card["question"],
        multi=card["mode"] == "multi",
        allow_custom=card.get("allowCustom", False),
        custom_label=card.get("customLabel", DEFAULT_CUSTOM_LABEL),
        options=options,
    )


def ask_answer_text(
    card: Optional[AskCard], typed: str
) -> Optional[str]:
    """Resolve what the user typed at a card into the answer for their next turn.

    A console answers by NUMBER (contract §11.4): ``"2"`` picks one option, ``"1,3"`` (or
    ``"1 3"``) picks several when the card is multi-select. Returns the joined labels, or
    ``None`` when the text is not a selection — which is not an error: it simply goes to the
    model as typed, so an unanswered card never blocks the conversation.
    """
    if card is None or not card.options:
        return None
    text = (typed or "").strip()
    if not text:
        return None
    picked: List[int] = []
    for token in re.split(r"[,\s]+", text):
        if not token:
            continue
        if not token.isdigit():
            return None
        n = int(token)
        if not 1 <= n <= len(card.options):
            return None
        if n - 1 not in picked:  # a repeat is the user re-stating a pick
            picked.append(n - 1)
    if not picked:
        return None
    if len(picked) > 1 and not card.multi:
        return None
    return ", ".join(card.options[i].label for i in picked)[:MAX_ASK_ANSWER]


def format_ask(card: AskCard) -> str:
    """The card as console text: the question, its numbered options, and how to answer."""
    lines = ["", card.question]
    for i, option in enumerate(card.options):
        lines.append("  %d. %s" % (i + 1, option.label))
    if card.allow_custom:
        lines.append("  or type your own: %s" % card.custom_label)
    lines.append(
        "answer with the number%s, or just say what you want"
        % ("s (e.g. 1,3)" if card.multi else " (e.g. 2)")
    )
    return "\n".join(lines)


# ── plan execution over the Editor facade ─────────────────────────────────────
class _FrameMap:
    """Contract §1 executor-side coordinate re-mapping: the running transform from
    the frame the model SAW (the pre-plan snapshot) into the current working frame.

    Every plan coordinate arrives in the pre-plan frame; each executed ``crop``
    composes a translation by minus its resolved rect origin, and each ``rotate``
    composes core ``rotateImageRGBA``'s quarter-turn mapping in continuous
    coordinates — one clockwise turn of a w×h view sends (x, y) to (h − y, x).
    Layout points are pushed through the accumulated steps, clamped into the
    current bounds, then drawn. Bare stub editors without geometry (no
    ``resolve_crop_rect``/``image_size``) record nothing and draw plans as-is.
    """

    def __init__(self, steps: Optional[list] = None) -> None:
        # ("crop", ox, oy) subtracts a resolved crop origin; ("cw", h) is one
        # clockwise quarter-turn of the view whose height was h at that step.
        self._steps: list = list(steps or [])

    def branch(self) -> "_FrameMap":
        """An independent copy for a variant (which composes its own crops/rotates
        on top of the top-level actions' transform)."""
        return _FrameMap(self._steps)

    def push_crop(self, origin_x: float, origin_y: float) -> None:
        if origin_x or origin_y:
            self._steps.append(("crop", float(origin_x), float(origin_y)))

    def push_rotate(self, quarters: int, view_w: float, view_h: float) -> None:
        w, h = float(view_w), float(view_h)
        for _ in range(quarters % 4):  # left turns normalize to 1..3 clockwise
            self._steps.append(("cw", h))
            w, h = h, w

    def map_point(self, x: float, y: float) -> Tuple[float, float]:
        for step in self._steps:
            if step[0] == "crop":
                x, y = x - step[1], y - step[2]
            else:
                x, y = step[1] - y, x
        return x, y

    def reset(self) -> None:
        """Drop the accumulated steps: a fresh picture (a §2.1 ``image`` switch) starts
        a fresh coordinate frame, so nothing planned before it applies any more."""
        self._steps.clear()


def wire_images(images: Optional[Iterable]) -> list:
    """Attachments in their WIRE form: strictly ``(media_type, bytes)`` pairs.

    An attachment may carry a third element — the source file name §2.1's ``save``
    derives its default project name from — which never rides the wire."""
    out: list = []
    for item in images or []:
        try:
            parts = tuple(item)
            pair = (parts[0], parts[1])
        except (TypeError, IndexError, ValueError):
            raise ValueError("each image must be a (media_type, bytes) tuple") from None
        if len(parts) > 3:
            raise ValueError("each image must be a (media_type, bytes) tuple")
        out.append(pair)
    return out


def _attachment_parts(item: Any) -> Tuple[Any, str]:
    """``(bytes, name)`` from one attachment.

    Attachments are the contract's ``(media_type, bytes)`` tuples — the very list a
    caller passed to :meth:`Chat.send` / :meth:`Editor.prompt`. An optional third
    element carries the original file name, which §2.1's ``save`` derives its default
    project name from (a plain 2-tuple simply has none)."""
    parts = tuple(item)
    return parts[1], (str(parts[2]) if len(parts) > 2 and parts[2] else "")


class _PlanRun:
    """Per-execution state the §2.1 multi-image ops need (contract §2.1).

    ``attachments`` is the turn's attached images in attachment order — the auto-attached
    working snapshot is not one of them; ``save_dir`` is where ``save`` writes its
    ``.stencil`` files (the console/API's own output directory, cwd by default);
    ``notes`` collects per-action skip warnings (an unsatisfiable index or a save with
    nothing loaded costs that ACTION, never the plan); ``saved`` records the written
    paths and ``active_name`` the name a later unnamed ``save`` derives from.
    """

    def __init__(self, attachments: Optional[Sequence] = None, save_dir: str = "",
                 console: Any = None) -> None:
        self.attachments: list = list(attachments or [])
        self.save_dir = save_dir or ""
        # The §10 console-profile hook object (the REPL), or None at the library level.
        self.console = console
        self.notes: List[str] = []
        self.saved: List[str] = []
        self.active_name = ""
        # One attachment and nothing else: it names an unnamed save even before an
        # explicit `image` op adopts it (the browser's activeAttachment rule).
        if len(self.attachments) == 1:
            self.active_name = _save_stem(_attachment_parts(self.attachments[0])[1])


def _save_stem(name: str) -> str:
    """A file name reduced to the project name a ``save`` uses: no directories, no
    extension. Empty when nothing usable is left — the caller then falls back."""
    base = str(name or "").replace("\\", "/").rsplit("/", 1)[-1]
    base = re.sub(r"\.[^.]+$", "", base).strip()
    return "" if set(base) <= {"."} else base


def _unique_save_path(directory: str, name: str) -> str:
    """``<name>.stencil`` in ``directory``, suffixed " 2", " 3"… past a name already
    on disk — so a plan saving several images never overwrites its own output."""
    stem = _save_stem(name) or "project"
    path = os.path.join(directory, stem + ".stencil") if directory else stem + ".stencil"
    n = 2
    while os.path.exists(path):
        candidate = "%s %d.stencil" % (stem, n)
        path = os.path.join(directory, candidate) if directory else candidate
        n += 1
    return path


def _clamp_point(x: float, y: float, w: float, h: float) -> Tuple[float, float]:
    """Clamp a layout point into the current image's bounds (contract §1)."""
    return (min(max(x, 0.0), float(w)), min(max(y, 0.0), float(h)))


# One applier per op, mapping the validated action onto the Editor method it
# stands for (contract §2). Each applier rides the op's OP_REGISTRY entry beside
# its validator, so dispatch is table-driven on both the parse and execute sides.
# ``frame`` is the plan's running §1 re-mapping transform (None for direct
# hand-built calls).
def _apply_crop(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    spec = action["spec"]
    # §10 console profile: the "album": true spec key rides beside the tokens and maps
    # onto the /crop … album axis derivation, never onto the token spec string.
    album = bool(spec.get("album"))
    spec_str = " ".join(
        "%s=%s" % (k, spec[k]) for k in ("x1", "y1", "x2", "y2", "aspect") if k in spec
    )
    if frame is not None:
        # Resolve exactly as Editor.crop is about to (the same core resolveCrop
        # path) to learn the kept rect's origin; a bad spec (None) is the same
        # no-op crop() performs, so the transform stays unchanged.
        resolve = getattr(editor, "resolve_crop_rect", None)
        rect = None
        if callable(resolve):
            rect = resolve(spec_str, album=album) if album else resolve(spec_str)
        if rect is not None:
            frame.push_crop(rect[0], rect[1])
    editor.crop(spec_str, album=album) if album else editor.crop(spec_str)


def _apply_rotate(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    times = action.get("times", 1)
    quarters = -times if action["dir"] == "left" else times
    if frame is not None:
        size = getattr(editor, "image_size", None)
        if size is not None:
            frame.push_rotate(quarters, size[0], size[1])
    editor.rotate(quarters)


def _apply_filter(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    if action["mode"] == "custom":
        editor.set_filter_color(action["tint"])
    else:
        editor.set_filter(action["mode"])


def _apply_layout(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    lines = [Line.from_dict(d) for d in action["lines"]]
    size = getattr(editor, "image_size", None)
    for line in lines:
        for p in line.points:
            x, y = frame.map_point(p.x, p.y) if frame is not None else (p.x, p.y)
            if size is not None:
                x, y = _clamp_point(x, y, size[0], size[1])
            p.x, p.y = x, y
    editor.draw(lines)


def _apply_formula(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    # §2: `enabled` alone toggles formulas (false restores identity, keeping the
    # expressions); otherwise set_formula validates via the shared parser and turns
    # allow_formulas on for a non-empty expression (an empty expr clears that axis).
    if "enabled" in action:
        editor.set_allow_formulas(action["enabled"])
    else:
        editor.set_formula(action["axis"], action["expr"])


def _apply_page(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    # §2: custom cm dims map onto the editors' custom page size (one form or the other).
    if "width" in action:
        editor.set_page_format("custom", action["width"], action["height"])
    else:
        editor.set_page_format(action["format"])


def _apply_blank(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    # §2: explicit cm dims override "format" — rendered at the core's default DPI,
    # exactly like the console's `/blank <w> <h>` custom flow.
    if "width" in action:
        from .core import get_core  # lazy, like Editor's own core access

        w_px, h_px = get_core().default_blank_size_px(action["width"], action["height"])
        editor.blank(w_px, h_px, color=action["color"])
    else:
        editor.blank(color=action["color"], page=action.get("format") or "A4")


def _apply_frame(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    raise LlmExecutionError(
        'the "frame" op needs a video input; pystencil has no video decoding '
        "(no ffmpeg) and operates on supplied frames — extract the frame with "
        "the CLI/desktop first and load() it as an image"
    )


def _apply_image(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    """§2.1: adopt the turn's Nth attached image as the working image.

    An index the turn cannot satisfy is a skipped ACTION with a note, never a failed
    plan — and the adopted picture starts a fresh coordinate frame (§1's accumulated
    crop/rotate re-mapping no longer describes it)."""
    index = action["index"]
    attachments = run.attachments if run is not None else []
    if index > len(attachments):
        note = "Skipped switching to attached image %d — this message attached %d image(s)" % (
            index, len(attachments)
        )
        if run is not None:
            run.notes.append(note)
        return
    data, name = _attachment_parts(attachments[index - 1])
    stem = _save_stem(name)
    editor.load(data, name=stem) if stem else editor.load(data)
    run.active_name = stem
    if frame is not None:
        frame.reset()


def _apply_save(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                run: Optional["_PlanRun"] = None) -> None:
    """§2.1: write the current image + layout as ``<name>.stencil`` beside the output.

    The name comes from the action, else from the active attachment's file name, else
    from the editor's own project name; a name already on disk gains a " 2"/" 3"…
    suffix. Nothing loaded is a skipped action with a note, never a failed plan."""
    has_image = getattr(editor, "has_image", None)
    if has_image is not None and not has_image():
        if run is not None:
            run.notes.append("Skipped save — no working image to save")
        return
    # A "path" destination is valid but not honoured here — note + usual place.
    if action.get("path") and run is not None:
        run.notes.append("Saved to the usual place — this surface cannot save to a path")
    active = run.active_name if run is not None else ""
    name = action.get("name") or active or getattr(editor, "name", "") or "project"
    path = _unique_save_path(run.save_dir if run is not None else "", name)
    editor.save_project(path)
    if run is not None:
        run.saved.append(path)


def _apply_history_step(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                        run: Optional["_PlanRun"] = None) -> None:
    """§2 ``undo``/``redo``: step the editor's OWN edit history. One step is one
    history entry; running out of entries is a note, never a failed plan."""
    op = action["op"]
    step = editor.undo if op == "undo" else editor.redo
    steps = action.get("steps", 1)
    done = 0
    while done < steps and step():
        done += 1
    if done < steps and run is not None:
        run.notes.append(
            "%s stopped after %d step(s) — no more history entries" % (op, done)
        )


def _apply_reset(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    """§2 ``reset``: drop every pending edit back to the original (the console's
    /reset). Nothing loaded is a skipped action with a note, never a failed plan."""
    has_image = getattr(editor, "has_image", None)
    if has_image is not None and not has_image():
        if run is not None:
            run.notes.append("Skipped reset — no working image")
        return
    editor.reset()


# The console-settings ops' executor hook methods (§10 console profile): the REPL
# passes itself as `console` and these methods run the SAME paths its /connect,
# /disconnect, /delete, /upload <url>, /drop and /chat clear commands use.
# Execution misses are notes per §1, never failed plans. clearChat's hook only
# RECORDS the request — the confirm + clear are deferred to the end of the turn.
_CONSOLE_HOOKS = {
    "connect": "plan_connect",
    "disconnect": "plan_disconnect",
    "delete": "plan_delete",
    "openUrl": "plan_open_url",
    "clear": "plan_clear",
    "clearChat": "plan_clear_chat",
}


def _apply_console_op(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                      run: Optional["_PlanRun"] = None) -> None:
    """Dispatch a §10 console-profile op to the surface's console hooks.

    Without a console session (the library API — ``Editor.prompt`` / a bare
    ``execute_op_plan``) the op is skipped with a note: connections, local project
    files, the working-image slot and the conversation (``clearChat`` — a
    single-turn ``Editor.prompt`` has none to clear) belong to the interactive
    console, and ``openUrl`` additionally needs the console's user-echo guard, so
    nothing here may fetch. A hook returns an optional note string (a §1
    execution miss)."""
    op = action["op"]
    console = run.console if run is not None else None
    if console is None:
        if run is not None:
            run.notes.append(
                'Skipped "%s" — this op drives the interactive console, and no '
                "console session is attached" % op
            )
        return
    note = getattr(console, _CONSOLE_HOOKS[op])(action)
    if note and run is not None:
        run.notes.append(note)
    # A load/clear that actually replaced the working picture starts a fresh frame,
    # and no earlier attachment names an unnamed save any more (a miss — note
    # returned — changed nothing, so the running transform still applies).
    if op in ("openUrl", "clear") and not note:
        if frame is not None:
            frame.reset()
        if run is not None:
            run.active_name = ""


# ── the op registry (contract §13): ONE entry per op ──────────────────────────
@dataclass(frozen=True)
class OpSpec:
    """One §13 registry entry — the single source of an op's existence on this surface.

    Membership, the key schema and the flags come from the shared opRegistry.json entry
    (SCHEMA); this surface adds the validator (the table-driven check + normalize),
    the applier, the prompt bullet and the block that carries it, so the "Available
    ops" prompt sections are GENERATED from the same table that validation and
    execution dispatch on: the prompt can never promise an op this surface cannot run.
    """

    validator: Callable[[dict], dict]
    applier: Callable[..., None]
    fields: FrozenSet[str]          # allowed action keys (including "op" itself)
    bullet: str                     # the op's prompt bullet, verbatim (§4/§10)
    scope: str = "core"             # which block carries the bullet: "core"|"console"
    top_level_only: bool = False    # §2/§2.1: inside "variants" it drops that variant
    console_settings: bool = False  # §10 console profile: variant ban + console hooks
    capability: str = ""            # runtime capability the op needs ("" = always wired)


# A bullet documenting two ops rides both entries and is emitted once (§4 keeps
# undo/redo — and the console's connect/disconnect — as single shared bullets).
_UNDO_REDO_BULLET = """- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
  than one request."""

_CONNECT_DISCONNECT_BULLET = """- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
  user's collaboration-server connections. Only a server listed in the console
  state below may be named — never invent, complete, or suggest a new address; for
  a server not listed there, tell the user to run '/connect <url>' themselves."""


def _make_validator(
    entry: dict, normalizer: Optional[Callable[[dict], dict]]
) -> Callable[[dict], dict]:
    """The registry's check (native rules, unknown fields, types, grammars, presence
    rules) + generic normalize, then this surface's own normalizer, if any."""

    def validate(a: dict) -> dict:
        try:
            out = SCHEMA.normalize(SCHEMA.validate_action(a, entry), entry)
        except SchemaError as e:
            raise LlmPlanError(str(e)) from None
        return normalizer(out) if normalizer is not None else out

    return validate


# What this surface adds to each registry entry: (applier, normalizer, bullet scope,
# bullet). Table order is prompt order: the §2 core ops as §4 lists them, then the
# §10 console-profile ops as the console block splices them (reset last — a §2 core
# op whose bullet rides the console block). The pystencil console carries the cli
# console's profile minus accent/reconnect (no theme, no reconnect command) and copy
# (no clipboard) — the registry restricts those entries to the cli, so they are never
# registered or promised here.
_SURFACE_OPS: Dict[str, tuple] = {
    "crop": (
        _apply_crop, _normalize_crop, "core",
        """- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
  opposite side. Include only the edges you want to move. For a target aspect ratio add
  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
  region should be kept.""",
    ),
    "rotate": (
        _apply_rotate, None, "core",
        """- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.""",
    ),
    "filter": (
        _apply_filter, None, "core",
        """- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.""",
    ),
    "layout": (
        _apply_layout, None, "core",
        """- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
  shapes, or structure from an attached image, answer with this op. An empty "lines"
  array REMOVES every drawn line — that is what "clear/remove the lines" means.""",
    ),
    "formula": (
        _apply_formula, None, "core",
        """- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.""",
    ),
    "page": (
        _apply_page, _float_dims, "core",
        """- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).""",
    ),
    "blank": (
        _apply_blank, _float_dims, "core",
        """- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
  centimetre dims ride as "width"/"height" instead of "format".""",
    ),
    "undo": (_apply_history_step, None, "core", _UNDO_REDO_BULLET),
    "redo": (_apply_history_step, None, "core", _UNDO_REDO_BULLET),
    "frame": (
        _apply_frame, None, "core",
        """- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
  only valid when the current input is a video.""",
    ),
    "image": (
        _apply_image, None, "core",
        """- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
  message (1-based, in attachment order); coordinates in later actions are in THAT
  image's pixel frame. Only valid when the user attached images. Use it to edit several
  attached images in one plan, giving each image its OWN actions.""",
    ),
    "save": (
        _apply_save, _normalize_save, "core",
        """- {"op":"save","name":"portrait 1"} — save the current image with its drawn lines as a
  project. When the user asks to process several images and keep the results, finish
  each image's actions with a "save" before switching to the next: image 1, its edits,
  save, image 2, its edits, save, …""",
    ),
    # The §10 console profile executes through the console's hooks.
    "connect": (_apply_console_op, _strip_field("server"), "console", _CONNECT_DISCONNECT_BULLET),
    "disconnect": (_apply_console_op, _strip_field("server"), "console", _CONNECT_DISCONNECT_BULLET),
    "delete": (
        _apply_console_op, _strip_field("path"), "console",
        """- {"op":"delete","path":"old.stencil"} — delete a LOCAL .stencil project file in
  the working directory (the console's /delete). Only .stencil files, never a URL
  or a path outside the working directory.""",
    ),
    "openUrl": (
        _apply_console_op, _normalize_open_url, "console",
        """- {"op":"openUrl","url":"https://…"} — load an image (or video frame) from a URL
  as the working image (the console's /upload). ONLY a URL the user themselves
  wrote in this conversation — never introduce, complete, or rewrite one.""",
    ),
    "clear": (
        _apply_console_op, None, "console",
        """- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
  This is what "remove/delete/clear the image" means. Never answer that with
  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
  removal. Takes no fields.""",
    ),
    # clearChat runs via the /chat clear path with an in-app confirm, DEFERRED to the
    # end of the turn (the REPL's plan_clear_chat hook records it).
    "clearChat": (
        _apply_console_op, None, "console",
        """- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
  confirm first, and the clear happens after this plan's other actions finish. This IS
  what "clear the chat / conversation / history" means; never answer that it cannot be
  done. Takes no fields.""",
    ),
    "reset": (
        _apply_reset, None, "console",
        """- {"op":"reset"} — drop every edit, back to the image exactly as it was loaded (the
  console's /reset). Takes no fields.""",
    ),
}

OP_REGISTRY: Dict[str, OpSpec] = {}
for _name, (_applier, _normalizer, _scope, _bullet) in _SURFACE_OPS.items():
    _entry = SCHEMA.ops.get(_name)
    if _entry is None:  # pragma: no cover - guards registry edits
        raise AssertionError('"%s" has no pystencil entry in opRegistry.json' % _name)
    _flags = _entry["flags"]
    OP_REGISTRY[_name] = OpSpec(
        _make_validator(_entry, _normalizer), _applier,
        frozenset({"op", *_entry["keys"]}), _bullet, scope=_scope,
        # §2/§2.1 top-level-only ops drop the variant they appear in; the §10
        # settings ops do the same (with their own message) and run through the
        # console hooks.
        top_level_only=bool(_flags.get("topLevelOnly")),
        console_settings=bool(_flags.get("editorSetting") or _flags.get("consoleSetting")),
    )
_unbound = sorted(set(SCHEMA.ops) - set(OP_REGISTRY))
if _unbound:  # pragma: no cover - guards registry edits
    raise AssertionError(
        "opRegistry.json registers %s for pystencil, but nothing here executes them"
        % ", ".join(_unbound)
    )


# §13 forbidden ops — the §10 "never model-drivable" boundary, as NAMES (the
# registry's forbidden.perSurface.pystencil): the assistant's own configuration
# (self-configuration is the exfiltration primitive), clipboard READS, hotkey
# rebinding, ending the session, chat persistence/consent toggles, and server-side
# destruction beyond §10's grants. Two teeth: the import-time registry check
# below (parse skips an unregistered name as unknown), and _apply_action's reject.
FORBIDDEN_OPS = tuple(SCHEMA.registry["forbidden"]["perSurface"]["pystencil"])

_forbidden_registered = sorted(set(OP_REGISTRY) & set(FORBIDDEN_OPS))
if _forbidden_registered:  # pragma: no cover - guards future registry edits
    raise AssertionError(
        "FORBIDDEN_OPS names may never be registered: %s" % ", ".join(_forbidden_registered)
    )


# Derived dispatch tables (single source: OP_REGISTRY).
_ACTION_FIELDS = {name: spec.fields for name, spec in OP_REGISTRY.items()}
_ACTION_VALIDATORS = {name: spec.validator for name, spec in OP_REGISTRY.items()}
_ACTION_APPLIERS = {name: spec.applier for name, spec in OP_REGISTRY.items()}
_TOP_LEVEL_ONLY_OPS = tuple(n for n, s in OP_REGISTRY.items() if s.top_level_only)
_CONSOLE_SETTINGS_OPS = tuple(n for n, s in OP_REGISTRY.items() if s.console_settings)


# ── prompt generation (contract §13) ──────────────────────────────────────────
# The capabilities actually wired on this surface. pystencil has no clipboard and
# no theme store, so nothing optional is wired — accent/copy hold no registry
# entries at all; the tag exists so a capability-carrying entry is EXCLUDED from
# generation (falling to §1's unknown-op skip) when its capability is not wired.
_SURFACE_CAPABILITIES: FrozenSet[str] = frozenset()

# §13 prompt censor: the generator refuses to emit any bullet matching sensitive
# patterns (api keys, bearer tokens, endpoint-setting instructions) — a registry
# mistake fails loudly at import instead of leaking into the prompt.
_PROMPT_CENSOR_PATTERNS = tuple(
    re.compile(p, re.IGNORECASE)
    for p in (
        r"api[\s_-]?key",
        r"\bbearer\b",
        r"\bauthorization\b",
        r"(?:access|auth|session|secret)[\s_-]?token",
        r"\bendpoint\b",
        r"base[\s_-]?url",
    )
)


def _assemble_ops_bullets(
    registry: Dict[str, OpSpec],
    scope: str,
    capabilities: FrozenSet[str] = _SURFACE_CAPABILITIES,
) -> str:
    """Concatenate a prompt block's op bullets from the registry (contract §13).

    Registry order is emission order; a bullet shared by two ops (undo/redo,
    connect/disconnect) is emitted once. An entry whose ``capability`` is not in
    ``capabilities`` is excluded — the op is then never promised to the model and
    falls to §1's unknown-op skip. A bullet matching a censor pattern raises."""
    bullets: List[str] = []
    for name, spec in registry.items():
        if spec.scope != scope:
            continue
        if spec.capability and spec.capability not in capabilities:
            continue
        for pattern in _PROMPT_CENSOR_PATTERNS:
            if pattern.search(spec.bullet):
                raise AssertionError(
                    'the "%s" op\'s prompt bullet matches the sensitive pattern %r '
                    "and may not be emitted (contract §13)" % (name, pattern.pattern)
                )
        if spec.bullet not in bullets:
            bullets.append(spec.bullet)
    return "\n".join(bullets)


# The §4 embed: the verbatim prose core around the GENERATED core ops section.
LLM_SYSTEM_PROMPT = (
    _PROMPT_CORE_HEAD
    + _assemble_ops_bullets(OP_REGISTRY, "core")
    + "\n\n"
    + _PROMPT_CORE_TAIL
)

# The §10 console-profile block: the GENERATED console bullets + the variant-ban
# closing sentence.
CONSOLE_SETTINGS_PROMPT = (
    _assemble_ops_bullets(OP_REGISTRY, "console") + "\n" + _CONSOLE_BLOCK_TAIL
)

if CONSOLE_SPLICE_ANCHOR not in LLM_SYSTEM_PROMPT:  # pragma: no cover - guards rewording
    raise AssertionError("LLM_SYSTEM_PROMPT no longer contains the console-settings splice anchor")

# §4 + the console block at the end of its op list — what the pystencil console's
# /prompt sends as its system prompt; LLM_SYSTEM_PROMPT itself stays the §4 embed.
CONSOLE_SYSTEM_PROMPT = LLM_SYSTEM_PROMPT.replace(
    CONSOLE_SPLICE_ANCHOR, "\n" + CONSOLE_SETTINGS_PROMPT + CONSOLE_SPLICE_ANCHOR, 1
)


def _apply_action(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    """Apply one validated action via the Editor method the op maps to (contract §2)."""
    op = action["op"]
    if op in FORBIDDEN_OPS:  # §13's second tooth — guards hand-built plans too
        raise LlmExecutionError(
            'the "%s" op is never model-drivable on any surface (contract §13)' % op
        )
    applier = _ACTION_APPLIERS.get(op)
    if applier is None:  # unreachable after parse_op_plan; guards hand-built plans
        raise LlmExecutionError("unsupported op %r" % op)
    applier(action, editor, frame, run)


def execute_op_plan(
    plan: OpPlan,
    editor: Any,
    attachments: Optional[Sequence] = None,
    save_dir: str = "",
    console: Any = None,
) -> list:
    """Execute a validated plan against an :class:`~pystencil.editor.Editor`.

    Top-level ``actions`` mutate ``editor`` in place (contract semantics: at most one
    updated result). Each variant then branches from the *flattened pixels* of the
    post-actions state — a fresh editor of the same class is loaded with that snapshot,
    the variant's actions run on it, and it yields one extra output. Returns the list
    of result :class:`~pystencil.image.Image` objects: the updated working image first
    (only when there were top-level actions), then one image per variant, in order
    (labels live in ``plan.variants[i].label``). A chat-only plan returns ``[]``.

    Plan coordinates arrive in the frame of the image the model was shown, so a
    running :class:`_FrameMap` re-maps later layout points through the plan's own
    crops/rotates (contract §1); variants continue from the top-level transform.

    ``attachments`` are the images the turn attached, in attachment order — the same
    list handed to :meth:`Chat.send` / :meth:`Editor.prompt`, as ``(media_type, bytes)``
    tuples (an optional third element names the source file, which an unnamed ``save``
    derives its project name from). The §2.1 ``image`` op indexes them 1-based and
    ``save`` writes ``<name>.stencil`` into ``save_dir`` (the cwd by default), with the
    written paths collected on ``plan.saved``. Per §2.1 an index the turn cannot
    satisfy, or a save with nothing loaded, costs that ACTION only: the note lands in
    ``plan.warnings`` (and, like a parse warning, on ``plan.reply``).

    ``console`` attaches the interactive console's §10 hook object (the REPL passes
    itself) so the console-profile ops — connect/disconnect/delete/openUrl/clear/
    clearChat — execute through the same paths its commands use; without one they
    are skipped with a note (the library API has no connections, no /delete scope,
    no user-echo guard for openUrl, and no conversation for clearChat).
    """
    outputs: list = []
    base = None  # the post-actions snapshot, rendered at most once
    frame = _FrameMap()  # §1: plan coordinates are in the pre-plan frame
    run = _PlanRun(attachments, save_dir, console)
    for action in plan.actions:
        _apply_action(action, editor, frame, run)
    plan.saved.extend(run.saved)
    if run.notes:
        plan.warnings.extend(run.notes)
        plan.reply = plan.reply + "\n" + "\n".join("[warning] " + w for w in run.notes)
    # A plan may legitimately end with nothing loaded (a §10 `clear`, or console ops
    # alone, which per §10 run without a working image) — then there is no result.
    has_image = getattr(editor, "has_image", None)
    empty = has_image is not None and not has_image()
    if plan.actions and not empty:
        base = editor.result()
        outputs.append(base)
    if plan.variants:
        if empty:
            note = "Skipped the variants — no working image is left to branch from"
            plan.warnings.append(note)
            plan.reply = plan.reply + "\n[warning] " + note
            return outputs
        if base is None:
            base = editor.result()  # snapshot AFTER the top-level actions
        for variant in plan.variants:
            branch = type(editor)()
            branch.load(base, name=variant.label)
            vframe = frame.branch()
            for action in variant.actions:
                _apply_action(action, branch, vframe)
            outputs.append(branch.result())
    return outputs


# ── §10 console-profile executor helpers (the cli console's, ported) ─────────
def url_echoed_by_user(history: Sequence[dict], current_text: str, url: str) -> bool:
    """§10 openUrl guard: the model may only ECHO the user — true when ``url`` appears
    verbatim in the current turn's text or a replayed USER turn (assistant text and
    fetched/attached content never count). ``history`` is Chat-shaped message dicts."""
    if url in (current_text or ""):
        return True
    for m in history or []:
        if m.get("role") == "user" and url in (m.get("text") or ""):
            return True
    return False


def blocked_open_url(plan: OpPlan, history: Sequence[dict], current_text: str) -> Optional[str]:
    """The first top-level ``openUrl`` whose URL the user never wrote (→ the whole
    plan is blocked, nothing executes), or None when every openUrl is an echo."""
    for a in plan.actions:
        if a.get("op") == "openUrl" and not url_echoed_by_user(
            history, current_text, a["url"]
        ):
            return a["url"]
    return None


def resolve_server(urls: Sequence[str], want: str):
    """§10's connect/disconnect stance over the console's own URL list: exact URL
    match, else a UNIQUE host (or host:port) match, case-insensitive. Returns the
    matched index, ``"none"``, or ``"ambiguous"`` — the model can never introduce a
    new address (an unmatched name is the user's to /connect)."""
    want = (want or "").strip()
    if not want:
        return "none"
    for i, u in enumerate(urls):
        if u == want:
            return i
    found: Optional[int] = None
    low = want.lower()
    for i, u in enumerate(urls):
        auth = u.split("://", 1)[-1].split("/", 1)[0]
        if auth.startswith("["):  # a bracketed IPv6 literal keeps its brackets
            host = auth[: auth.index("]") + 1] if "]" in auth else auth
        else:
            host = auth.rsplit(":", 1)[0]
        if auth.lower() == low or host.lower() == low:
            if found is not None:
                return "ambiguous"
            found = i
    return found if found is not None else "none"


@dataclass
class ConsoleServer:
    """One live connection as the console-context suffix sees it: the URL and
    (optionally) its project names — NEVER a token; this class has nowhere to put one."""

    url: str
    active: bool = False  # hosts the active fetched project
    projects: Optional[List[str]] = None  # None = not fetched/unreachable (line omitted)


# How many project names one server contributes to the context suffix.
MAX_CONTEXT_PROJECTS = 20


def console_context(servers: Sequence[ConsoleServer], active_project: str = "") -> str:
    """The console's dynamic system-prompt suffix (§4 allows one; the cli console's
    ``consoleContextAlloc`` ported verbatim): the connection list (URLs only), the
    active project, and each server's project names — so "what am I connected to?" /
    "which projects are on my server?" are answered from context, and the
    connect/disconnect ops resolve against addresses the user already owns."""
    out = [
        "Console state (the console's own connections and project, for answering "
        "questions about it):\n"
    ]
    if not servers:
        out.append("Connections: none — the user can add one with '/connect <url>'.")
    else:
        out.append("Connections (%d): " % len(servers))
        for i, s in enumerate(servers):
            if i:
                out.append(", ")
            out.append(s.url)
            if s.active:
                out.append(" (active project's server)")
        out.append(".")
    if active_project:
        out.append('\nActive server project: "%s".' % active_project)
    else:
        out.append("\nActive server project: none.")
    for s in servers:
        if s.projects is None:  # unknown (unreachable) ≠ empty
            continue
        out.append("\nProjects on %s: " % s.url)
        if not s.projects:
            out.append("(none)")
            continue
        shown = s.projects[:MAX_CONTEXT_PROJECTS]
        out.append(", ".join(shown))
        if len(s.projects) > len(shown):
            out.append(" (+%d more)" % (len(s.projects) - len(shown)))
        out.append(".")
    return "".join(out)


# ── provider client (contract §6) ─────────────────────────────────────────────
def _b64(data: Any) -> str:
    """Base64-encode image bytes (an already-encoded str passes through)."""
    if isinstance(data, str):
        return data
    return base64.b64encode(bytes(data)).decode("ascii")


def _msg_parts(m: dict) -> Tuple[str, str, list]:
    """Unpack an internal message dict into (role, text, images)."""
    return (
        m.get("role") or "user",
        m.get("text") or "",
        list(m.get("images") or []),
    )


class LlmClient:
    """A per-provider LLM chat client (urllib, no deps, offline-testable).

    Messages are dicts ``{"role": "user"|"assistant", "text": str, "images": [...]}``
    where each image is a ``(media_type, bytes)`` tuple, base64-encoded into the
    provider's wire shape (contract §6). Request builders are pure (no network);
    every call executes through the private :meth:`_open` seam, mirroring
    :class:`pystencil.server.ServerConnection`. For ``stencil-server``, pass either
    a ``server`` object (a :class:`~pystencil.server.ServerConnection`, whose
    ``.base`` URL and ``.token`` are read) or a config ``server_url`` + ``token``.
    """

    def __init__(
        self,
        config: Optional[LlmConfig] = None,
        *,
        server: Any = None,
        token: str = "",
    ) -> None:
        self.config = config if config is not None else LlmConfig.from_env()
        if server is not None:
            url = getattr(server, "base", "") or getattr(server, "base_url", "") or ""
            self._server_url = url.rstrip("/")
            self._token = token or getattr(server, "token", "") or ""
        else:
            self._server_url = (self.config.server_url or "").rstrip("/")
            self._token = token

    # ── request plumbing ──
    def _build_request(
        self, messages: Sequence[dict], system: str = LLM_SYSTEM_PROMPT
    ) -> urllib.request.Request:
        """Pure builder: assemble the provider-specific POST (no network)."""
        provider = self.config.provider
        if provider == "ollama":
            return self._ollama_request(messages, system)
        if provider == "openai-compat":
            return self._openai_request(messages, system)
        return self._server_request(messages, system)

    def _base_post(
        self, path: str, wire: list, bearer: Optional[str] = None
    ) -> urllib.request.Request:
        """The §6.1/§6.2 shared envelope: ``{model, stream:false, messages}`` POSTed
        to ``{baseUrl}{path}``."""
        body = {"model": self.config.model, "stream": False, "messages": wire}
        return self._post(self.config.base_url.rstrip("/") + path, body, bearer=bearer)

    def _ollama_request(
        self, messages: Sequence[dict], system: str
    ) -> urllib.request.Request:
        """``POST {baseUrl}/api/chat`` — native chat, images as bare base64 (§6.1)."""
        wire: list = [{"role": "system", "content": system}]
        for m in messages:
            role, text, images = _msg_parts(m)
            entry: dict = {"role": role, "content": text}
            if images:
                entry["images"] = [_b64(data) for _mt, data in images]
            wire.append(entry)
        return self._base_post("/api/chat", wire)

    def _openai_request(
        self, messages: Sequence[dict], system: str
    ) -> urllib.request.Request:
        """``POST {baseUrl}/chat/completions`` — images as data URLs; optional
        ``Authorization: Bearer <apiKey>`` (§6.2)."""
        wire: list = [{"role": "system", "content": system}]
        for m in messages:
            role, text, images = _msg_parts(m)
            if images:
                content: Any = [{"type": "text", "text": text}]
                for mt, data in images:
                    content.append(
                        {
                            "type": "image_url",
                            "image_url": {"url": "data:%s;base64,%s" % (mt, _b64(data))},
                        }
                    )
            else:
                content = text
            wire.append({"role": role, "content": content})
        return self._base_post(
            "/chat/completions", wire, bearer=self.config.api_key or None
        )

    def _server_request(
        self, messages: Sequence[dict], system: str
    ) -> urllib.request.Request:
        """``POST {serverUrl}/llm/chat`` — the collaboration server's Anthropic proxy,
        authenticated with the existing Stencil session token (§6.3)."""
        if not self._server_url:
            raise LlmError(
                "no stencil-server URL configured — set STENCIL_LLM_SERVER_URL or "
                "pass a ServerConnection"
            )
        wire: list = []
        for m in messages:
            role, text, images = _msg_parts(m)
            entry: dict = {"role": role, "text": text}
            if images:
                entry["images"] = [
                    {"mediaType": mt, "data": _b64(data)} for mt, data in images
                ]
            wire.append(entry)
        body: dict = {"system": system, "messages": wire}
        if self.config.model:
            body["model"] = self.config.model
        return self._post(self._server_url + "/llm/chat", body, bearer=self._token or "")

    @staticmethod
    def _post(url: str, body: dict, bearer: Optional[str] = None) -> urllib.request.Request:
        """A JSON POST Request; ``bearer`` adds ``Authorization`` (None omits it).
        Delegates to the request builder shared with :mod:`pystencil.server`."""
        return _json_request("POST", url, body, bearer=bearer)

    def _open(self, req: urllib.request.Request) -> Any:
        """Execute a Request, translating non-2xx / bad payloads into :class:`LlmError`
        (a network-level ``URLError`` propagates as ``OSError``, like the server client).

        The single network seam (tests monkey-patch this, like ServerConnection._open);
        the urlopen/HTTPError plumbing is the one shared with the server client, under the
        LLM timeout rather than the REST one.
        Redirects are refused: urllib would replay the key against the host the 30x named.
        """
        _status, payload = _http_open(
            req, self._error_from, follow_redirects=False, timeout=_LLM_TIMEOUT)
        if not payload:
            raise LlmError("empty response from the LLM provider")
        try:
            return json.loads(payload.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            raise LlmError("non-JSON response from the LLM provider") from None

    @staticmethod
    def _error_from(e: urllib.error.HTTPError) -> LlmError:
        """Build an LlmError from an HTTPError, parsing a ``{code, message}`` body
        when present (e.g. the server's 503 ``llmDisabled``).

        The message alone is the text: it already says the reason once (§6.3), and the
        machine ``code`` in front of it only restated it — it stays on the exception,
        where a caller can branch on it.
        """
        code, message = _parse_http_error(e)
        return LlmError(_clean_detail(message), code=code, status=e.code)

    def _extract_reply(self, payload: Any) -> str:
        """Pull the reply text out of a provider response (contract §6).

        For ``stencil-server``, a ``stopReason`` of ``max_tokens``/``refusal`` raises
        a typed :class:`LlmError` — those replies are never parsed as plans.
        """
        provider = self.config.provider
        text: Any = None
        if provider == "ollama":
            msg = payload.get("message") if isinstance(payload, dict) else None
            text = msg.get("content") if isinstance(msg, dict) else None
        elif provider == "openai-compat":
            choices = payload.get("choices") if isinstance(payload, dict) else None
            first = choices[0] if isinstance(choices, list) and choices else None
            msg = first.get("message") if isinstance(first, dict) else None
            text = msg.get("content") if isinstance(msg, dict) else None
        else:  # stencil-server
            if not isinstance(payload, dict):
                payload = {}
            stop = payload.get("stopReason") or ""
            text = payload.get("text")
            if stop == "max_tokens":
                raise LlmError(
                    "response truncated (max_tokens) — not parsed as a plan",
                    stop_reason="max_tokens",
                )
            if stop == "refusal":
                raise LlmError(
                    "the model refused to answer", stop_reason="refusal"
                )
        if not isinstance(text, str):
            raise LlmError("malformed %s response: no reply text" % provider)
        return text

    # ── the one public call ──
    def chat(self, messages: Sequence[dict], system: str = LLM_SYSTEM_PROMPT) -> str:
        """Send one non-streaming chat call and return the raw reply text."""
        return self._extract_reply(self._open(self._build_request(messages, system)))


# ── stateful chat (contract §7) ───────────────────────────────────────────────
class Chat:
    """A client-side conversation: bounded history replayed in full on every call.

    All providers are stateless, so :meth:`send` replays the most recent
    ``MAX_HISTORY`` (32) messages. The image replay rule keeps payloads bounded:
    only the current turn's images plus the single most recent prior image are sent;
    older turns are replayed text-only. Attached images are ``(media_type, bytes)``
    tuples (see the module docstring for the no-downscale deviation).
    """

    def __init__(self, client: Optional[LlmClient] = None) -> None:
        self.client = client if client is not None else LlmClient()
        # Retained history: dicts {"role", "text", "images"} (newest last). Images
        # that the replay rule can never send again are blanked by send() — see its
        # memory note — so only replayable attachments are held.
        self.history: List[dict] = []

    @staticmethod
    def _check_images(images: Optional[Iterable], cap: Optional[int] = MAX_ATTACHMENTS) -> list:
        """Validate/normalize attachments into [(media_type, bytes), ...].

        Bounded by ``MAX_ATTACHMENTS`` (contract §7): a turn's images are re-encoded,
        replayed and paid for on every call. Over the cap is an error rather than a
        silent trim — a caller that passed ten images should hear about it. ``cap=None``
        keeps the media-type validation but lifts the count bound — the §7 cap binds the
        USER attachment queue, not surface-internal transients (the edge map, the
        console's §2.1 /upload set, which has its own 8-image bound).
        """
        out: list = []
        for media_type, data in wire_images(images):
            if media_type not in ACCEPTED_MEDIA_TYPES:
                raise ValueError(
                    "unsupported media type %r — accepted: %s"
                    % (media_type, ", ".join(ACCEPTED_MEDIA_TYPES))
                )
            out.append((media_type, data))
        if cap is not None and len(out) > cap:
            raise ValueError(
                "up to %d images per message (got %d)" % (cap, len(out))
            )
        return out

    def _trim(self) -> None:
        """Bound the retained history to the most recent MAX_HISTORY messages."""
        if len(self.history) > MAX_HISTORY:
            del self.history[: len(self.history) - MAX_HISTORY]

    def _wire_messages(self) -> List[dict]:
        """History with the image replay rule applied (current turn's images plus
        the single most recent prior image; everything older text-only)."""
        last = len(self.history) - 1
        prior: Optional[int] = None  # index of the newest earlier image-bearing message
        for i in range(last - 1, -1, -1):
            if self.history[i]["images"]:
                prior = i
                break
        out: List[dict] = []
        for i, m in enumerate(self.history):
            if i == last:
                images = list(m["images"])
            elif i == prior:
                images = [m["images"][-1]]  # only its most recent image replays
            else:
                images = []
            out.append({"role": m["role"], "text": m["text"], "images": images})
        return out

    def send(
        self,
        text: str,
        images: Optional[Iterable] = None,
        *,
        system: Optional[str] = None,
        transient_images: Optional[Iterable] = None,
    ) -> Tuple[str, OpPlan]:
        """Send one user turn; returns ``(reply, plan)``.

        The user message (with its validated attachments) joins the history, the
        bounded history is replayed through the client, and the raw reply is parsed
        into an :class:`OpPlan` (the raw text — not the extracted reply — is what the
        assistant turn stores, so the model sees its own JSON on later turns).
        Execution is the caller's choice via :func:`execute_op_plan`.

        ``system`` overrides the canonical system prompt for THIS call only (a
        suffixed prompt is never remembered). ``transient_images`` ride the wire
        directly after ``images`` on this turn only — never stored, so never
        replayed (§7's edge map).

        Memory: once an image-bearing turn is on the wire, the replay rule can never
        send any EARLIER turn's images again (the new turn supersedes them as the
        most recent prior), so those attachments are blanked from the retained
        history rather than held for the life of the chat. The wire payloads are
        identical to keeping them.
        """
        self.history.append(
            {"role": "user", "text": str(text), "images": self._check_images(images)}
        )
        self._trim()
        wire = self._wire_messages()
        transient = self._check_images(transient_images, cap=None)
        if transient:
            wire[-1]["images"] = wire[-1]["images"] + transient
        if self.history[-1]["images"]:
            for m in self.history[:-1]:
                m["images"] = []
        raw = self.client.chat(wire, system if system is not None else LLM_SYSTEM_PROMPT)
        plan = parse_op_plan(raw)
        self.history.append({"role": "assistant", "text": raw, "images": []})
        self._trim()
        return plan.reply, plan

    # ── §12 chat persistence (per-project, opt-in) ──
    @staticmethod
    def _display_text(raw: str) -> str:
        """The DISPLAYED form of an assistant turn: the plan's extracted ``reply``.

        History stores the raw model text (see :meth:`send`), but the persisted
        document is shared across surfaces and must read as a conversation, so the
        raw JSON is re-parsed here; text that fails to parse as a plan (it was
        displayed as-is when it arrived) falls back to itself.
        """
        try:
            return parse_op_plan(raw).reply
        except LlmError:
            return raw

    def to_doc(self, now_ms: Optional[int] = None) -> dict:
        """Serialize the conversation as the §12.1 persisted-chat document.

        Text-only by contract: images are NEVER persisted, assistant turns store
        their displayed reply (never the raw JSON plan), empty-text turns are
        dropped, and the result is trimmed to the most recent ``MAX_HISTORY`` (32)
        messages. §7's internal machinery is dropped too, via
        :func:`chat_display_text`. ``now_ms`` pins the informational ``savedAt``
        stamp (tests); the default is the current epoch ms.
        """
        messages: List[dict] = []
        for m in self.history:
            role = m.get("role")
            if role not in ("user", "assistant"):
                continue
            text = m.get("text")
            if not isinstance(text, str):
                continue
            if role == "assistant":
                text = self._display_text(text)
            shown = chat_display_text(role, text)
            if shown is None:
                continue
            messages.append({"role": role, "text": shown})
        return {
            "version": CHAT_DOC_VERSION,
            "savedAt": int(time.time() * 1000) if now_ms is None else int(now_ms),
            "messages": messages[-MAX_HISTORY:],
        }

    # Contract §12.3 documents these under the to_dict/from_dict spelling.
    to_dict = to_doc

    @classmethod
    def from_doc(cls, doc: Any, client: Optional[LlmClient] = None) -> "Chat":
        """Rebuild a Chat from a §12.1 document (a dict or its JSON string).

        Restore is forgiving by contract — a malformed document, a ``version``
        other than 1, non-``user``/``assistant`` roles, and missing/non-string
        ``text`` all degrade to dropped messages or an empty Chat, never an
        exception. Stray ``images`` fields are ignored (persisted chats are
        text-only); anything past ``MAX_HISTORY`` messages is truncated to the
        most recent. Restoring never triggers a model call. The §12.1 gate
        (:func:`chat_display_text`) runs here too: another surface's — or an older
        build's — internal text is never restored into the conversation.
        """
        chat = cls(client)
        if isinstance(doc, str):
            try:
                doc = json.loads(doc)
            except ValueError:
                return chat
        if not isinstance(doc, dict) or doc.get("version") != CHAT_DOC_VERSION:
            return chat
        messages = doc.get("messages")
        if not isinstance(messages, list):
            return chat
        for m in messages:
            if not isinstance(m, dict):
                continue
            role = m.get("role")
            text = m.get("text")
            if role not in ("user", "assistant") or not isinstance(text, str):
                continue
            shown = chat_display_text(role, text)
            if shown is None:
                continue
            chat.history.append({"role": role, "text": shown, "images": []})
        chat._trim()
        return chat

    from_dict = from_doc

    def clear(self) -> None:
        """Forget the whole conversation (the client/config is kept)."""
        del self.history[:]
