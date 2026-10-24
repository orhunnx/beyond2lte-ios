/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_HID_WIRE_H
#define B2L_HID_WIRE_H
/* iOS26 IOKit digitizer wire ABI measured from the shipped native creator and
 * getters. Internal IOFixed fields remain integer-only in the kernel; doubles
 * are serialized as IEEE754 bits without executing FP instructions. */
struct b2l_digitizer_wire {
    uint32_t size, type, options;
    uint8_t depth, reserved[3];
    uint64_t x, y, z;
    uint32_t index, transducer_type, identity, event_mask, child_mask, button_mask;
    uint64_t pressure, aux_pressure, twist;
    uint32_t orientation_type, alignment;
    uint64_t orientation[6];
    uint32_t generation, will_update, did_update, tail;
};
static_assert(sizeof(b2l_digitizer_wire) == 160, "iOS26 digitizer length");
static_assert(__builtin_offsetof(b2l_digitizer_wire, generation) == 0x90, "native getter +4104");
static_assert(__builtin_offsetof(b2l_digitizer_wire, orientation) == 0x60, "native orientation ABI");
static uint64_t b2l_fixed_double(int32_t value)
{
    if (value == INT32_MIN) return UINT64_C(0x7ff8000000000000); /* IOFixedNaN */
    if (!value) return 0;
    uint64_t sign = value < 0 ? UINT64_C(1) << 63 : 0;
    uint32_t magnitude = value < 0 ? (uint32_t)-(int64_t)value : (uint32_t)value;
    unsigned bit = 31u - (unsigned)__builtin_clz(magnitude);
    return sign | ((uint64_t)(bit + 1023u - 16u) << 52) |
        (((uint64_t)magnitude << (52u - bit)) & UINT64_C(0xfffffffffffff));
}
static int32_t b2l_double_fixed(uint64_t bits)
{
    unsigned exp = (unsigned)((bits >> 52) & 2047);
    if (exp == 2047) return INT32_MIN;
    if (exp < 1007) return 0;
    if (exp > 1037) return bits >> 63 ? INT32_MIN + 1 : INT32_MAX;
    uint64_t mantissa = (bits & UINT64_C(0xfffffffffffff)) | (UINT64_C(1) << 52);
    uint64_t magnitude = mantissa >> (1059u - exp);
    return bits >> 63 ? -(int32_t)magnitude : (int32_t)magnitude;
}
static size_t b2l_hid_wire_size(const IOHIDEventData *data)
{
    return data->type == kIOHIDEventTypeDigitizer ? sizeof(b2l_digitizer_wire) : data->size;
}
static size_t b2l_hid_wire_write(const IOHIDEventData *data, void *out, size_t capacity)
{
    size_t size = b2l_hid_wire_size(data);
    if (capacity < size) return 0;
    if (data->type != kIOHIDEventTypeDigitizer) { memcpy(out, data, size); return size; }
    if (data->size != sizeof(IOHIDDigitizerEventData)) return 0;
    const IOHIDDigitizerEventData *v = (const IOHIDDigitizerEventData *)data;
    b2l_digitizer_wire w = {};
    w.size = sizeof(w); w.type = data->type;
    memcpy(&w.options, &v->options, sizeof(w.options));
    w.depth = v->depth;
    w.x = b2l_fixed_double(v->position.x); w.y = b2l_fixed_double(v->position.y); w.z = b2l_fixed_double(v->position.z);
    w.index = v->transducerIndex; w.transducer_type = v->transducerType; w.identity = v->identity;
    w.event_mask = v->eventMask; w.child_mask = v->childEventMask; w.button_mask = v->buttonMask;
    w.pressure = b2l_fixed_double(v->pressure); w.aux_pressure = b2l_fixed_double(v->auxPressure); w.twist = b2l_fixed_double(v->twist);
    w.orientation_type = v->orientationType;
    int32_t values[6]; memcpy(values, &v->orientation, sizeof(values));
    for (unsigned i = 0; i < 6; i++) w.orientation[i] = b2l_fixed_double(values[i]);
    w.generation = v->generationCount; w.will_update = v->willUpdateMask; w.did_update = v->didUpdateMask;
    memcpy(out, &w, sizeof(w)); return sizeof(w);
}
static bool b2l_hid_wire_read(const void *bytes, size_t size, IOHIDEventData *out)
{
    if (size != sizeof(b2l_digitizer_wire)) return false;
    b2l_digitizer_wire w; memcpy(&w, bytes, sizeof(w));
    if (w.type != kIOHIDEventTypeDigitizer || w.size != size) return false;
    IOHIDDigitizerEventData *v = (IOHIDDigitizerEventData *)out;
    memset(v, 0, sizeof(*v)); v->size = sizeof(*v); v->type = w.type;
    memcpy(&v->options, &w.options, sizeof(w.options)); v->depth = w.depth;
    v->position.x = b2l_double_fixed(w.x); v->position.y = b2l_double_fixed(w.y); v->position.z = b2l_double_fixed(w.z);
    v->transducerIndex = w.index; v->transducerType = w.transducer_type; v->identity = w.identity;
    v->eventMask = w.event_mask; v->childEventMask = w.child_mask; v->buttonMask = w.button_mask;
    v->pressure = b2l_double_fixed(w.pressure); v->auxPressure = b2l_double_fixed(w.aux_pressure); v->twist = b2l_double_fixed(w.twist);
    v->orientationType = w.orientation_type;
    int32_t values[6]; for (unsigned i = 0; i < 6; i++) values[i] = b2l_double_fixed(w.orientation[i]);
    memcpy(&v->orientation, values, sizeof(values));
    v->generationCount = w.generation; v->willUpdateMask = w.will_update; v->didUpdateMask = w.did_update;
    return true;
}
#endif
