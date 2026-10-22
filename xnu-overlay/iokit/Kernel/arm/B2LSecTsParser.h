/** @file
  Host-testable Samsung sec_ts coordinate-event parser.

  The wire format is intentionally expressed with C built-in integer types so
  this file can be compiled both by EDK II and by a host C compiler.
**/

#ifndef B2L_SEC_TS_PARSER_H
#define B2L_SEC_TS_PARSER_H

#define B2L_SEC_TS_EVENT_SIZE  8
#define B2L_SEC_TS_MAX_COORD   4095

typedef unsigned char   B2L_U8;
typedef unsigned short  B2L_U16;

typedef enum {
  B2LSecTsIgnored = 0,
  B2LSecTsUpdated,
  B2LSecTsInvalid
} B2L_SEC_TS_PARSE_RESULT;

typedef struct {
  B2L_U8   Active;
  B2L_U8   TouchId;
  B2L_U16  X;
  B2L_U16  Y;
  B2L_U8   Pressure;
} B2L_SEC_TS_POINTER;

B2L_SEC_TS_PARSE_RESULT
B2LSecTsParseEvent (
  const B2L_U8         Event[B2L_SEC_TS_EVENT_SIZE],
  B2L_SEC_TS_POINTER  *Pointer
  );

B2L_U8
B2LSecTsRemainingEvents (
  const B2L_U8  Event[B2L_SEC_TS_EVENT_SIZE]
  );

#endif
