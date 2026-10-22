#include "B2LSecTsParser.h"

#define SEC_TS_COORDINATE_EVENT          0
#define SEC_TS_COORDINATE_ACTION_PRESS   1
#define SEC_TS_COORDINATE_ACTION_MOVE    2
#define SEC_TS_COORDINATE_ACTION_RELEASE 3

#define SEC_TS_TOUCHTYPE_NORMAL  0
#define SEC_TS_TOUCHTYPE_GLOVE   3
#define SEC_TS_TOUCHTYPE_STYLUS  4

static int
IsPointerTouchType (
  B2L_U8  TouchType
  )
{
  return (TouchType == SEC_TS_TOUCHTYPE_NORMAL) ||
         (TouchType == SEC_TS_TOUCHTYPE_GLOVE)  ||
         (TouchType == SEC_TS_TOUCHTYPE_STYLUS);
}

B2L_SEC_TS_PARSE_RESULT
B2LSecTsParseEvent (
  const B2L_U8         Event[B2L_SEC_TS_EVENT_SIZE],
  B2L_SEC_TS_POINTER  *Pointer
  )
{
  B2L_U8   Action;
  B2L_U8   EventId;
  B2L_U8   Pressure;
  B2L_U8   TouchId;
  B2L_U8   TouchType;
  B2L_U16  X;
  B2L_U16  Y;

  if ((Event == 0) || (Pointer == 0)) {
    return B2LSecTsInvalid;
  }

  EventId = Event[0] & 0x03;
  if (EventId != SEC_TS_COORDINATE_EVENT) {
    return B2LSecTsIgnored;
  }

  Action    = (Event[0] >> 6) & 0x03;
  TouchId   = (Event[0] >> 2) & 0x0F;
  TouchType = (B2L_U8)(((Event[6] >> 6) << 2) | (Event[7] >> 6));

  if ((TouchId == 0) || (Action == 0)) {
    return B2LSecTsInvalid;
  }

  X        = (B2L_U16)(((B2L_U16)Event[1] << 4) | (Event[3] >> 4));
  Y        = (B2L_U16)(((B2L_U16)Event[2] << 4) | (Event[3] & 0x0F));
  Pressure = Event[6] & 0x3F;

  if (Action == SEC_TS_COORDINATE_ACTION_PRESS) {
    if (Pointer->Active || !IsPointerTouchType (TouchType)) {
      return B2LSecTsIgnored;
    }

    Pointer->Active   = 1;
    Pointer->TouchId  = TouchId;
    Pointer->X        = X;
    Pointer->Y        = Y;
    Pointer->Pressure = (Pressure == 0) ? 1 : Pressure;
    return B2LSecTsUpdated;
  }

  if (!Pointer->Active || (TouchId != Pointer->TouchId)) {
    return B2LSecTsIgnored;
  }

  if (Action == SEC_TS_COORDINATE_ACTION_RELEASE) {
    Pointer->Active   = 0;
    Pointer->TouchId  = 0;
    Pointer->X        = X;
    Pointer->Y        = Y;
    Pointer->Pressure = 0;
    return B2LSecTsUpdated;
  }

  if ((Action == SEC_TS_COORDINATE_ACTION_MOVE) &&
      IsPointerTouchType (TouchType)) {
    Pointer->X        = X;
    Pointer->Y        = Y;
    Pointer->Pressure = (Pressure == 0) ? 1 : Pressure;
    return B2LSecTsUpdated;
  }

  return B2LSecTsIgnored;
}

B2L_U8
B2LSecTsRemainingEvents (
  const B2L_U8  Event[B2L_SEC_TS_EVENT_SIZE]
  )
{
  if (Event == 0) {
    return 0;
  }

  return Event[7] & 0x3F;
}
