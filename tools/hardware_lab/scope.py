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

"""Manual oscilloscope controls using bounded LF commands and a Flipper reader."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import re
import signal
import threading

import lab
import pair

MAX_CYCLES = 12
ACTIONS = {
    'emular': ('rfid emutest 5000 0', 8, True),
    'invertido': ('rfid emutest 5000 1', 8, True),
    'pulso': ('rfid modtest 1000 1000', 4, True),
    'campo': ('rfid field 1000', 4, False),
    'ler': ('rfid watch 5', 8, False),
    'externo': ('rfid listen 1000', 4, True),
}
HELP = """
RFID — OSCILOSCÓPIO | nada liga ao abrir este painel
  emular       EM4100 0102030405 por 5 s; precisa de campo de leitor externo
  invertido    Mesma emulação com polaridade invertida, por 5 s
  pulso        GPIO11/T2 a 1 kHz por 1 s; não é um cartão válido
  externo      Medir campo externo, sem modular, por 1 s
  campo        Campo do High Boy a 125 kHz por 1 s; não precisa do Flipper
  ler          High Boy lê uma tag física por 5 s; não precisa do Flipper
  status       Consultar firmware e saídas, sem iniciar RF
  parar        Solicitar OFF; Flipper só é controlado se selecionado e conectado
  sair         Fechar o painel
Ex.: emular 6 repete seis vezes, com desligamento entre os ciclos.
Ctrl+C durante um teste cancela as próximas repetições e aguarda o
comando atual terminar (até cerca de 5 s de sinal), depois confere OFF.
Emulação/pulso: retirar a tag física; GPIO11 + gate T2; bobina diferencial.
Campo/leitura: manter outros leitores desligados. Não mexer nos menus RF.
Sem --flipper, somente o High Boy é controlado; ligar o leitor externo manualmente
para emulação/pulso. O campo externo pode vir de um Flipper funcionando na bateria.
"""


def finish_reader(flipper):
    """Accept natural decoder completion as well as an explicit Ctrl+C stop."""
    flipper.receive(.2)
    transcript = ''.join(row['value'] for row in flipper.events if row['kind'] == 'rx')
    if flipper.active and 'Reading stopped' in transcript and re.search(r'>: *$', transcript):
        flipper.active = False
    flipper.stop()
    return ''.join(row['value'] for row in flipper.events if row['kind'] == 'rx')


def run_action(args, action, cancelled):
    if action not in ACTIONS and action not in ('status', 'parar'):
        raise ValueError('Comando desconhecido. Digite ajuda.')
    stamp = lab.dt.datetime.now(lab.dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    folder = args.output / ('scope-' + stamp + '-' + lab.uuid.uuid4().hex[:6])
    folder.mkdir(parents=True)
    lab.save(folder / 'events.jsonl', '')
    report = dict(action=action, status='running', output=str(folder),
                  instrument_verification='pending', highboy=args.highboy,
                  flipper=args.flipper, cleanup_errors=[])
    link = flipper = None
    is_verified = False
    with (folder / 'events.jsonl').open('a') as log:
        def event(kind, value):
            log.write(json.dumps(dict(utc=lab.dt.datetime.now(lab.dt.timezone.utc).isoformat(),
                                      kind=kind, value=value), ensure_ascii=False) + '\n')
            log.flush()
            if kind == 'command':
                print('\nHIGH BOY > ' + value, flush=True)
            elif kind == 'rx':
                print(value['text'], end='', flush=True)

        def check_cancelled():
            if cancelled.is_set():
                raise lab.Cancelled('Parada solicitada.')

        try:
            link = lab.Link(args.highboy, event)
            link.console()
            report['firmware'] = link.command('rfbench info')
            if not re.search(r'INFO schema=1 version=.*elf_sha256=[0-9a-f]{64}', report['firmware']):
                raise RuntimeError('Firmware de laboratório/P4 não confirmado; teste não iniciado.')
            is_verified = True
            report['initial_status'] = link.command('rfid status')
            if action != 'parar' and not lab.idle(report['initial_status']):
                raise RuntimeError('Antena ocupada. Feche o aplicativo NFC/RFID antes de testar.')
            check_cancelled()
            if action != 'status':
                has_flipper = args.flipper and any(
                    device['device'] == args.flipper and (device['vid'], device['pid']) == (0x0483, 0x5740)
                    for device in lab.ports())
                if has_flipper:
                    with contextlib.redirect_stdout(io.StringIO()):
                        flipper = pair.Flipper(args.flipper, folder / 'flipper.json')
                    print('\nFLIPPER > conectado; identificação completa no registro.', flush=True)
                    flipper.send(b'loader info\r\n')
                    if 'No application is running' not in flipper.receive(.5):
                        flipper.send(b'loader close\r\n')
                        flipper.receive(.5)
                        flipper.send(b'loader info\r\n')
                        if 'No application is running' not in flipper.receive(.5):
                            raise RuntimeError('Aplicativo do Flipper não fechou; teste não iniciado.')
                check_cancelled()
                if action in ACTIONS:
                    command, timeout, needs_reader = ACTIONS[action]
                    if needs_reader:
                        if flipper:
                            flipper.start(reader=True)
                        else:
                            print('Somente High Boy: o campo externo deve vir de um leitor ligado manualmente.', flush=True)
                        if action != 'externo':
                            report['external_field'] = lab.field(link.command('rfid listen 1000'))
                            external = report['external_field']
                            if not external['carrier_detected'] or not 110000 <= external['measured_hz'] <= 140000:
                                raise RuntimeError('Campo externo de 125 kHz não confirmado. '
                                                   'Ligue um leitor e alinhe as antenas; modulação não acionada. '
                                                   'Para gerar campo com o próprio High Boy, use campo.')
                    else:
                        external = lab.field(link.command('rfid listen 100'))
                        if external['carrier_detected']:
                            raise RuntimeError('Campo externo detectado; afaste/desligue o outro leitor.')
                    check_cancelled()
                    print('\n>>> SINAL: ' + action.upper() + ' — preparar captura no osciloscópio', flush=True)
                    report['response'] = link.command(command, timeout)
                    text = report['response']
                    if action in ('emular', 'invertido'):
                        if 'EM4100 waveform complete:' not in text or 'outputs OFF' not in text:
                            raise RuntimeError('Emulação não completou: consulte a resposta acima.')
                        report['control_result'] = 'waveform_completed; physical_modulation_unverified'
                    else:
                        case_id = {'pulso': 'lf-modulation', 'campo': 'lf-field',
                                   'ler': 'lf-read', 'externo': 'lf-passive'}[action]
                        report['observation'] = lab.inspect(case_id, text)
                    if 'reset_required=1' in text:
                        raise RuntimeError('Firmware solicita reset; não iniciar outro teste.')
            report['status'] = 'cancelled' if cancelled.is_set() else 'complete'
        except lab.Cancelled as exc:
            report.update(status='cancelled', error=str(exc))
        except Exception as exc:
            report.update(status='failed', error=str(exc))
        finally:
            if link:
                try:
                    if is_verified:
                        # Do not disturb an unrelated frontend that we did not acquire.
                        if action == 'parar' or lab.idle(report.get('initial_status', '')):
                            link.command('rfid off')
                        report['final_status'] = link.command('rfid status')
                        report['highboy_off'] = lab.idle(report['final_status'])
                        if not report['highboy_off']:
                            raise RuntimeError('OFF do High Boy não confirmado.')
                except Exception as exc:
                    report['cleanup_errors'].append('High Boy: ' + str(exc))
                finally:
                    try:
                        link.close()
                    except Exception as exc:
                        report['cleanup_errors'].append('Porta High Boy: ' + str(exc))
            if flipper:
                try:
                    report['flipper_transcript'] = finish_reader(flipper)
                    report['flipper_off'] = True
                    report['decoded'] = re.findall(r'EM4100\s+([0-9A-Fa-f]{10})', report['flipper_transcript'])
                    report['test_id_decoded'] = pair.TEST_ID in report['decoded']
                except Exception as exc:
                    report['cleanup_errors'].append('Flipper: ' + str(exc))
                finally:
                    try:
                        flipper.u.close()
                    except Exception as exc:
                        report['cleanup_errors'].append('Porta Flipper: ' + str(exc))
            if report['cleanup_errors']:
                report['status'] = 'failed'
            lab.save(folder / 'report.json', report)
    print('\nRESULTADO:', report['status'], flush=True)
    if report.get('error'):
        print('ERRO:', report['error'])
    if report['cleanup_errors']:
        print('OFF NÃO CONFIRMADO:', '; '.join(report['cleanup_errors']))
    elif report.get('highboy_off'):
        print('High Boy: OFF confirmado.' + (' Flipper: leitura parada.' if flipper else ''))
    if report.get('control_result') and 'test_id_decoded' in report:
        print('Flipper leu 0102030405.' if report['test_id_decoded'] else 'Flipper não decodificou 0102030405.')
    print('Registro:', folder, flush=True)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--highboy', required=True, help='Porta P4 explicitamente identificada')
    parser.add_argument('--flipper', help='Porta USB CDC do Flipper')
    parser.add_argument('--output', type=Path, default=lab.OUTPUT)
    parser.add_argument('--command', choices=tuple(ACTIONS) + ('status', 'parar'))
    args = parser.parse_args(argv)
    cancelled = threading.Event()
    exiting = threading.Event()
    is_running = False

    def stop(signum, _frame):
        cancelled.set()
        if signum != signal.SIGINT or not is_running:
            exiting.set()
            if not is_running:
                raise KeyboardInterrupt
        print('\nPARADA solicitada; aguardando o comando limitado terminar e verificando OFF.', flush=True)

    signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
    previous = {sig: signal.signal(sig, stop) for sig in signals}
    try:
        if args.command:
            is_running = True
            result = run_action(args, args.command, cancelled)
            return 0 if result['status'] == 'complete' else 1
        print(HELP)
        print('P4:', args.highboy, '\nFlipper:', args.flipper or 'não selecionado')
        while not exiting.is_set():
            try:
                words = input('\nRFID > ').strip().lower().split()
                if not words:
                    continue
                action = words[0]
                if action in ('sair', 'exit', 'q'):
                    break
                if action in ('ajuda', 'help', '?'):
                    print(HELP)
                    continue
                cycles = int(words[1]) if len(words) == 2 else 1
                if len(words) > 2 or not 1 <= cycles <= MAX_CYCLES:
                    raise ValueError('Use comando e, opcionalmente, 1 a 12 repetições.')
                if action not in ACTIONS and action not in ('status', 'parar'):
                    raise ValueError('Comando desconhecido. Digite ajuda.')
                cancelled.clear()
                is_running = True
                result = None
                for index in range(cycles):
                    if cancelled.is_set():
                        break
                    print(f'\nCICLO {index + 1}/{cycles}', flush=True)
                    result = run_action(args, action, cancelled)
                    if result['status'] != 'complete':
                        break
                if result and result['cleanup_errors']:
                    print('Painel encerrado: conferir os aparelhos antes de continuar.')
                    return 1
            except (ValueError, OSError, RuntimeError) as exc:
                print('ERRO:', exc)
            finally:
                is_running = False
        return 0
    except (KeyboardInterrupt, EOFError):
        print('\nPainel encerrado.')
        return 0
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)


if __name__ == '__main__':
    raise SystemExit(main())
