import pytest
from pytest_embedded import Dut


@pytest.mark.generic
def test_argtable3(dut: Dut) -> None:
    dut.run_all_single_board_cases()
