/* SPDX-License-Identifier: BSD-2-Clause
 * External-method OOL descriptors are not prepared by IOUserClient.
 * Balance our own wire reference around every physical byte-copy operation.
 */
#ifndef B2L_MEMORY_IO_H
#define B2L_MEMORY_IO_H
// Descriptor is templated so host tests execute this exact implementation.
template<class Descriptor> static uint64_t
b2l_descriptor_read(Descriptor *md, uint64_t offset, void *bytes, uint64_t count)
{
    if (!md || !bytes || !count) return 0;
    uint64_t length = md->getLength();
    if (offset > length || count > length - offset) return 0;
    if (md->prepare(kIODirectionOut) != kIOReturnSuccess) return 0;
    uint64_t copied = md->readBytes(offset, bytes, count);
    IOReturn done = md->complete(kIODirectionOut);
    return done == kIOReturnSuccess ? copied : 0;
}
template<class Descriptor> static uint64_t
b2l_descriptor_write(Descriptor *md, uint64_t offset, const void *bytes, uint64_t count)
{
    if (!md || !bytes || !count) return 0;
    uint64_t length = md->getLength();
    if (offset > length || count > length - offset) return 0;
    if (md->prepare(kIODirectionIn) != kIOReturnSuccess) return 0;
    uint64_t copied = md->writeBytes(offset, bytes, count);
    IOReturn done = md->complete(kIODirectionIn);
    return done == kIOReturnSuccess ? copied : 0;
}
#endif
