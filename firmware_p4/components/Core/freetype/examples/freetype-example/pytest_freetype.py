# SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
from __future__ import unicode_literals

import textwrap

import pytest
from pytest_embedded import Dut


@pytest.mark.generic
def test_freetype_example(dut: Dut) -> None:
    dut.expect_exact('FreeType library initialized')
    dut.expect_exact('Font loaded')
    for c in 'FreeType':
        dut.expect_exact(f'Rendering char: \'{c}\'')
