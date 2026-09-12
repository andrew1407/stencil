"""Registry-driven op-plan schema engine (llm-contract.md §1-§2, §8, §11).

A rule-for-rule port of ``browser/js/llm/opSchema.js`` over the checked-in copy of
``browser/js/config/llm/opRegistry.json`` (``tests/test_canonical_drift.py`` byte-pins
the copy): profile membership, unknown-field rejection, required keys, types, enums,
ranges, string caps, token grammars and the cross-field presence rules (forms /
together / exclusive / minFields / onlyWith / requiredWith). :mod:`pystencil.llm` keeps
only its normalizers, executors and the native rules an entry names. ``re`` + ``json``
only; the registry is parsed once, on first use.
"""

from __future__ import annotations

from .path import SchemaError
from .schema import Schema, load_registry, schema

__all__ = ["Schema", "SchemaError", "load_registry", "schema"]
