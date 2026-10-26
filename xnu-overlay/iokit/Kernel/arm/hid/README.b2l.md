# B2L HID source provenance

The IOHID event, event-service queue, user-client sources and supporting headers
originate from Apple's published IOHIDFamily 1915.140.3 sources. Their original
copyright and license notices remain in each file.

The minimal IOHIDSystem/EventService/Resource scaffolding and consumer buttons /
digitizer bridge come from this workspace's A5 implementation, patches 0046 and
0058, at A5 commit 506775feb4e06cfb0c2b1278324560fc2236dd94. B2L uses a dedicated
owned workloop, removes automatic synthetic button injection and normalizes
single-contact sec_ts coordinates to 0..4095. Hardware access lives separately
in B2LInput.cpp. This does not supply Apple's complete HID stack.

Physical pin and HSI2C/parser references are the user's UEFI_beyond2lte keypad,
GPIO and B2LTouchDxe sources. Bixby currently produces a Home consumer event.

The iOS26 wire adapter serializes current-format160-byte digitizer data with
a separate28-byte queue header for every child event, plus native depth and
subtree counts. See docs/build-0089.md and tests/test-hid-collection.py. This
format differs from the original IOHIDFamily flat legacy collection.
