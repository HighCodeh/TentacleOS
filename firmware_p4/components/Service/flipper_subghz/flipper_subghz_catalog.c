// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "lib/subghz/types.h"

extern const SubGhzProtocol subghz_protocol_gate_tx;
extern const SubGhzProtocol subghz_protocol_keeloq;
extern const SubGhzProtocol subghz_protocol_nice_flo;
extern const SubGhzProtocol subghz_protocol_came;
extern const SubGhzProtocol subghz_protocol_prastel;
extern const SubGhzProtocol subghz_protocol_faac_slh;
extern const SubGhzProtocol subghz_protocol_nice_flor_s;
extern const SubGhzProtocol subghz_protocol_came_twee;
extern const SubGhzProtocol subghz_protocol_came_atomo;
extern const SubGhzProtocol subghz_protocol_nero_sketch;
extern const SubGhzProtocol subghz_protocol_ido;
extern const SubGhzProtocol subghz_protocol_hormann;
extern const SubGhzProtocol subghz_protocol_nero_radio;
extern const SubGhzProtocol subghz_protocol_somfy_telis;
extern const SubGhzProtocol subghz_protocol_somfy_keytis;
extern const SubGhzProtocol subghz_protocol_princeton;
extern const SubGhzProtocol subghz_protocol_linear;
extern const SubGhzProtocol subghz_protocol_secplus_v2;
extern const SubGhzProtocol subghz_protocol_secplus_v1;
extern const SubGhzProtocol subghz_protocol_megacode;
extern const SubGhzProtocol subghz_protocol_holtek;
extern const SubGhzProtocol subghz_protocol_chamb_code;
extern const SubGhzProtocol subghz_protocol_power_smart;
extern const SubGhzProtocol subghz_protocol_marantec;
extern const SubGhzProtocol subghz_protocol_bett;
extern const SubGhzProtocol subghz_protocol_doitrand;
extern const SubGhzProtocol subghz_protocol_phoenix_v2;
extern const SubGhzProtocol subghz_protocol_honeywell;
extern const SubGhzProtocol subghz_protocol_honeywell_wdb;
extern const SubGhzProtocol subghz_protocol_magellan;
extern const SubGhzProtocol subghz_protocol_intertechno_v3;
extern const SubGhzProtocol subghz_protocol_clemsa;
extern const SubGhzProtocol subghz_protocol_ansonic;
extern const SubGhzProtocol subghz_protocol_smc5326;
extern const SubGhzProtocol subghz_protocol_holtek_th12x;
extern const SubGhzProtocol subghz_protocol_linear_delta3;
extern const SubGhzProtocol subghz_protocol_dooya;
extern const SubGhzProtocol subghz_protocol_alutech_at_4n;
extern const SubGhzProtocol subghz_protocol_kinggates_stylo_4k;
extern const SubGhzProtocol subghz_protocol_bin_raw;
extern const SubGhzProtocol subghz_protocol_mastercode;
extern const SubGhzProtocol subghz_protocol_x10;
extern const SubGhzProtocol subghz_protocol_hormann_bisecur;
extern const SubGhzProtocol subghz_protocol_legrand;
extern const SubGhzProtocol subghz_protocol_dickert_mahs;
extern const SubGhzProtocol subghz_protocol_gangqi;
extern const SubGhzProtocol subghz_protocol_marantec24;
extern const SubGhzProtocol subghz_protocol_hollarm;
extern const SubGhzProtocol subghz_protocol_hay21;
extern const SubGhzProtocol subghz_protocol_revers_rb2;
extern const SubGhzProtocol subghz_protocol_feron;
extern const SubGhzProtocol subghz_protocol_roger;
extern const SubGhzProtocol subghz_protocol_elplast;
extern const SubGhzProtocol subghz_protocol_treadmill37;
extern const SubGhzProtocol subghz_protocol_beninca_arc;
extern const SubGhzProtocol subghz_protocol_jarolift;
extern const SubGhzProtocol subghz_protocol_ditec_gol4;
extern const SubGhzProtocol subghz_protocol_doorbell32;
extern const SubGhzProtocol subghz_protocol_keyfinder;
extern const SubGhzProtocol subghz_protocol_keyfinder2;
extern const SubGhzProtocol subghz_protocol_nord_ice;
extern const SubGhzProtocol subghz_protocol_allstar_firefly;
extern const SubGhzProtocol subghz_protocol_telcoma_edge;
extern const SubGhzProtocol subghz_protocol_cardin_s508;
extern const SubGhzProtocol subghz_protocol_freezer;
extern const SubGhzProtocol subghz_protocol_faac_tm;
extern const SubGhzProtocol subghz_protocol_proteco;
extern const SubGhzProtocol subghz_protocol_belkorn;

const SubGhzProtocol *const flipper_subghz_catalog[] = {
    &subghz_protocol_gate_tx,
    &subghz_protocol_keeloq,
    &subghz_protocol_nice_flo,
    &subghz_protocol_came,
    &subghz_protocol_prastel,
    &subghz_protocol_faac_slh,
    &subghz_protocol_nice_flor_s,
    &subghz_protocol_came_twee,
    &subghz_protocol_came_atomo,
    &subghz_protocol_nero_sketch,
    &subghz_protocol_ido,
    &subghz_protocol_hormann,
    &subghz_protocol_nero_radio,
    &subghz_protocol_somfy_telis,
    &subghz_protocol_somfy_keytis,
    &subghz_protocol_princeton,
    &subghz_protocol_linear,
    &subghz_protocol_secplus_v2,
    &subghz_protocol_secplus_v1,
    &subghz_protocol_megacode,
    &subghz_protocol_holtek,
    &subghz_protocol_chamb_code,
    &subghz_protocol_power_smart,
    &subghz_protocol_marantec,
    &subghz_protocol_bett,
    &subghz_protocol_doitrand,
    &subghz_protocol_phoenix_v2,
    &subghz_protocol_honeywell,
    &subghz_protocol_honeywell_wdb,
    &subghz_protocol_magellan,
    &subghz_protocol_intertechno_v3,
    &subghz_protocol_clemsa,
    &subghz_protocol_ansonic,
    &subghz_protocol_smc5326,
    &subghz_protocol_holtek_th12x,
    &subghz_protocol_linear_delta3,
    &subghz_protocol_dooya,
    &subghz_protocol_alutech_at_4n,
    &subghz_protocol_kinggates_stylo_4k,
    &subghz_protocol_bin_raw,
    &subghz_protocol_mastercode,
    &subghz_protocol_x10,
    &subghz_protocol_hormann_bisecur,
    &subghz_protocol_legrand,
    &subghz_protocol_dickert_mahs,
    &subghz_protocol_gangqi,
    &subghz_protocol_marantec24,
    &subghz_protocol_hollarm,
    &subghz_protocol_hay21,
    &subghz_protocol_revers_rb2,
    &subghz_protocol_feron,
    &subghz_protocol_roger,
    &subghz_protocol_elplast,
    &subghz_protocol_treadmill37,
    &subghz_protocol_beninca_arc,
    &subghz_protocol_jarolift,
    &subghz_protocol_ditec_gol4,
    &subghz_protocol_doorbell32,
    &subghz_protocol_keyfinder,
    &subghz_protocol_keyfinder2,
    &subghz_protocol_nord_ice,
    &subghz_protocol_allstar_firefly,
    &subghz_protocol_telcoma_edge,
    &subghz_protocol_cardin_s508,
    &subghz_protocol_freezer,
    &subghz_protocol_faac_tm,
    &subghz_protocol_proteco,
    &subghz_protocol_belkorn,
};
const size_t flipper_subghz_catalog_count =
    sizeof(flipper_subghz_catalog) / sizeof(flipper_subghz_catalog[0]);
