"""The console profile spliced into the canonical system prompt (§10)."""

from __future__ import annotations

import unittest

import pystencil.llm as llm_module
from pystencil.llm import CONSOLE_SETTINGS_PROMPT, CONSOLE_SYSTEM_PROMPT, LLM_SYSTEM_PROMPT


class ConsoleProfilePromptTest(unittest.TestCase):
  """The §4 embed's just-added bullets, and the console-profile splice (§10)."""

  def test_the_system_prompt_carries_the_new_section_2_bullets(self):
    # layout: an empty lines array removes every drawn line.
    self.assertIn('array REMOVES every drawn line', LLM_SYSTEM_PROMPT)
    # formula: empty expr clears; enabled:false switches formulas off.
    self.assertIn('An empty "expr" clears', LLM_SYSTEM_PROMPT)
    self.assertIn('{"op":"formula","enabled":false} switches formulas OFF', LLM_SYSTEM_PROMPT)
    # page: the custom-size form in centimetres.
    self.assertIn('{"op":"page","width":20,"height":30} in centimetres', LLM_SYSTEM_PROMPT)
    # blank: explicit centimetre dims ride as width/height.
    self.assertIn('centimetre dims ride as "width"/"height"', LLM_SYSTEM_PROMPT)
    # undo/redo joined the op list.
    self.assertIn('{"op":"undo","steps":1} / {"op":"redo","steps":1}', LLM_SYSTEM_PROMPT)

  def test_console_prompt_splices_the_settings_block_at_the_ask_anchor(self):
    # The block sits in the CONSOLE prompt only, spliced at the end of the op
    # list — directly before the ask paragraph — leaving §4 itself untouched.
    self.assertNotIn(CONSOLE_SETTINGS_PROMPT, LLM_SYSTEM_PROMPT)
    self.assertIn(CONSOLE_SETTINGS_PROMPT, CONSOLE_SYSTEM_PROMPT)
    block = CONSOLE_SYSTEM_PROMPT.index(CONSOLE_SETTINGS_PROMPT)
    ask = CONSOLE_SYSTEM_PROMPT.index(llm_module.CONSOLE_SPLICE_ANCHOR.lstrip("\n"))
    save_bullet = CONSOLE_SYSTEM_PROMPT.index('{"op":"save"')
    self.assertTrue(save_bullet < block < ask)

  def test_console_block_carries_the_profile_minus_the_exceptions(self):
    # pystencil = the cli console profile EXCEPT accent/reconnect/copy.
    for op in ('"connect"', '"disconnect"', '"delete"', '"openUrl"', '"clear"',
         '"clearChat"', '"reset"'):
      self.assertIn('{"op":%s' % op, CONSOLE_SETTINGS_PROMPT)
    for absent in ("accent", "reconnect", '"copy"'):
      self.assertNotIn(absent, CONSOLE_SETTINGS_PROMPT)
    self.assertIn('cannot appear inside "variants"', CONSOLE_SETTINGS_PROMPT)
