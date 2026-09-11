"""The native cross-field rules an entry may name in its ``rules`` list."""

from __future__ import annotations

from typing import Callable, Dict

from .path import _bad, _is_obj


def _crop_aspect_fold(a: dict) -> dict:
    """§3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
    conflicting duplicate fails. The folded copy is what gets validated + normalized."""
    if a.get("aspect") is None or not _is_obj(a.get("spec")):
        return a
    spec = dict(a["spec"])
    if spec.get("aspect") is not None and spec["aspect"] != a["aspect"]:
        _bad('"aspect" appears both beside "spec" and inside it with different values')
    if spec.get("aspect") is None:
        spec["aspect"] = a["aspect"]
    out = {k: v for k, v in a.items() if k != "aspect"}
    out["spec"] = spec
    return out


_RULES: Dict[str, Callable[[dict], dict]] = {"cropAspectFold": _crop_aspect_fold}
