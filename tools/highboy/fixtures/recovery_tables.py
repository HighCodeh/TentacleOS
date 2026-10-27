"""Public pinned partition-table fixtures from the integrated SDK builds."""
import base64
import zlib

TABLES = {
    'P4': zlib.decompress(base64.b64decode('eNpbFcDIxDCBgYEhgYEhr6yYAR2sCmBkYPgAZCgwMOSXJKYkliSiyzMyCADVCDAwFGRUxmfmZZagyoNkmIAsXZD+eAMM8xkEGRj04fKGmPYzMzDEAFmMDMn5RakppbkFaPLNDAyxQJYyQ2JxcWoJmhdev/6PAq4p+3wIyTxneSXv9cMDph/M/o+CUTAKRsEoGAWjYBSMglEwggAAlLEq0A==')),
    'C5': zlib.decompress(base64.b64decode('eNpbFcDIxDCBgYEhgYEhr6yYAR2sCmBkYPgAZCgwMOSXJKYkliSiyzMyCADVCDAwFGRUxmfmZZagyoNkmMAGAPXHG2CYzyDIwKAElzfEtL+ZgcEJyJJjSCwuTi0pxpBnBjuegZEhOb8oNaU0twBZ/vXr/yjgdnfwKQGRBpNfGxb+iZmZu/P/KBgFo2AUjIJRMApGwSgYBSMIAAA0xCoo')),
}
