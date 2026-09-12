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

Split across limits/errors/config/types/frame/run/ops/console/registry/validate/plan/
ask/prompt/execute/client/chat; this module is the façade, and its import surface is
the contract every caller binds to.
"""

from __future__ import annotations

from .ask import ask_answer_text, format_ask
from .chat import Chat
from .client import LlmClient
from .config import (
  ACCEPTED_MEDIA_TYPES,
  DEFAULT_BASE_URLS,
  MAX_ATTACHMENTS,
  MAX_UPLOAD_ATTACHMENTS,
  PROVIDERS,
  LlmConfig,
)
from .console import (
  MAX_CONTEXT_PROJECTS,
  ConsoleServer,
  blocked_open_url,
  console_context,
  resolve_server,
  url_echoed_by_user,
)
from .errors import (
  MAX_PROVIDER_DETAIL,
  LlmError,
  LlmExecutionError,
  LlmPlanError,
  _clean_detail,
)
from .execute import execute_op_plan
from .limits import (
  CHAT_DOC_VERSION,
  CONTINUATION_NOTE,
  DEFAULT_CUSTOM_LABEL,
  MAX_ACTIONS,
  MAX_ASK_ANSWER,
  MAX_ASK_LABEL,
  MAX_ASK_OPTIONS,
  MAX_ASK_QUESTION,
  MAX_FRAME_INDICES,
  MAX_HISTORY,
  MAX_LAYOUT_LINES,
  MAX_PATH_CHARS,
  MAX_SAVE_NAME,
  MAX_STRING_LENGTH,
  MAX_VARIANTS,
  MIN_ASK_OPTIONS,
  SCHEMA,
  chat_display_text,
)
from .plan import parse_op_plan
from .prompt import (
  CONSOLE_SETTINGS_PROMPT,
  CONSOLE_SPLICE_ANCHOR,
  CONSOLE_SYSTEM_PROMPT,
  EDGE_MAP_SUFFIX,
  LLM_SYSTEM_PROMPT,
  _assemble_ops_bullets,
)
from .registry import (
  FORBIDDEN_OPS,
  OP_REGISTRY,
  OpSpec,
  _ACTION_APPLIERS,
  _ACTION_FIELDS,
  _ACTION_VALIDATORS,
  _CONSOLE_SETTINGS_OPS,
  _TOP_LEVEL_ONLY_OPS,
)
from .run import wire_images
from .types import AskCard, AskOption, OpPlan, Variant, variant_slug, variant_slugs
