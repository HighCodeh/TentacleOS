# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Interactive terminal launcher; opening the menu never starts RF."""
import json
import signal
import sys

import lab
import pair


def choose_port(devices, name):
    if not devices:
        raise RuntimeError(name + ' não conectado por USB.')
    print('\n' + name + ' — escolha a porta:')
    for i, device in enumerate(devices, 1):
        print(i, device['device'], 'serial=', device['serial_number'], device.get('interface'))
    selection = int(input('Número da porta (P4 para RFID): '))
    if not 1 <= selection <= len(devices):
        raise ValueError('Porta inválida.')
    return devices[selection - 1]['device']


def main():
    choices = [('Comparação completa com Flipper', 'pair'),
               ('Autoteste RFID', 'lf-selftest'), ('Campo 125 kHz por 1 s', 'lf-field'),
               ('Leitura de tag por 5 s', 'lf-read'), ('Captura DA por 250 ms', 'lf-raw'),
               ('Varredura 118–132 kHz', 'lf-sweep'), ('Medir campo externo por 1 s', 'lf-passive'),
               ('Pulso de carga T2 por 1 s (não emula tag)', 'lf-modulation'),
               ('Reset USB automático', 'usb-reset'), ('Boot USB + SYNC + retorno', 'usb-boot'),
               ('Bateria / alimentação', 'power'), ('P4 ↔ C5: 60 requisições', 'spi-link')]
    while True:
        print('\nHIGH BOY LAB — campo desligado entre testes; Ctrl+C solicita parada.')
        for i, (title, _) in enumerate(choices, 1):
            print(f'{i:2}. {title}')
        print(' 0. Sair')
        try:
            selected = int(input('Teste: '))
            if selected == 0:
                return 0
            if not 1 <= selected <= len(choices):
                raise ValueError('Teste inválido.')
            kind = choices[selected - 1][1]
            available = lab.ports()
            port = choose_port([d for d in available if (d['vid'], d['pid']) == lab.CP2105], 'High Boy')
            label = input('Identificação da placa/alteração (ex.: C112 4.7nF): ')
            if kind == 'pair':
                flipper = choose_port([d for d in available if (d['vid'], d['pid']) == (0x0483, 0x5740)], 'Flipper')
                pair.main(['--highboy', port, '--flipper', flipper, '--mode', 'deep', '--label', label])
            else:
                chip = input('Chip p4 ou c5: ').strip().lower() if kind.startswith('usb-') else 'p4'
                print(lab.CASES[kind]['probes'])
                runner = lab.Runner(progress=lab.terminal_event)
                signal.signal(signal.SIGINT, lambda *_: runner.cancelled.set())
                result = runner.run(kind, port, label, chip=chip)
                print(json.dumps({k: result.get(k) for k in ('status', 'rows', 'error', 'output')}, indent=2))
        except (ValueError, RuntimeError, OSError) as exc:
            print('ERRO:', exc)
        finally:
            signal.signal(signal.SIGINT, signal.default_int_handler)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (EOFError, KeyboardInterrupt):
        print('\nMenu encerrado.')
