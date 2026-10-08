"""LLM assistant support over ``contracts/llm/``: provider client, op-plan parsing, execution.

The model answers with an op-plan whose actions map onto :class:`~pystencil.editor.Editor`
methods. One deviation from §7: with no resampling, attachments are not downscaled to 1568 px.
This façade's import surface is the contract every caller binds to.
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
from .plan.execute import execute_op_plan
from .plan.limits import (
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
  chat_display_text,
)
from .plan.parse import parse_op_plan
from .prompt import (
  CONSOLE_SETTINGS_PROMPT,
  CONSOLE_SPLICE_ANCHOR,
  CONSOLE_SYSTEM_PROMPT,
  EDGE_MAP_SUFFIX,
  LLM_SYSTEM_PROMPT,
  _assemble_ops_bullets,
)
from .plan.registry import (
  FORBIDDEN_OPS,
  OP_REGISTRY,
  OpSpec,
  _ACTION_APPLIERS,
)
from .run import wire_images
from .types import AskCard, AskOption, OpPlan, Variant, variant_slug, variant_slugs
