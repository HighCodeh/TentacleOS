// Host-test shim for ducky_parser.h. Only the layout enum is needed by
// hid_layouts.c; it must match the real definition in the firmware header.
#ifndef SHIM_DUCKY_PARSER_H
#define SHIM_DUCKY_PARSER_H

typedef enum {
  DUCKY_LAYOUT_US = 0,
  DUCKY_LAYOUT_ABNT2,
  DUCKY_LAYOUT_COUNT
} ducky_layout_t;

#endif // SHIM_DUCKY_PARSER_H
