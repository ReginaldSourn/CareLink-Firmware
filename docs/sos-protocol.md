# CareLink raw-LoRa protocol v1

This document is the integration contract between a CareLink pendant and a
Rolork gateway. The radio transport is raw LoRa, not LoRaWAN. Multi-byte fields
are big-endian. Every message is exactly 74 bytes and uses the Rolork gateway
radio profile: 125 kHz bandwidth, spreading factor 9, coding rate 4/5, preamble
8, explicit header, payload CRC enabled and normal IQ.

## Frame layout

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | ASCII magic `CL` |
| 2 | 1 | protocol version, `1` |
| 3 | 1 | message type |
| 4 | 1 | flags; bit 0 means location is present |
| 5 | 2 | payload length, always `67` |
| 7 | 8 | pendant device ID |
| 15 | 8 | incident ID |
| 23 | 4 | incident packet sequence |
| 27 | 4 | boot session ID |
| 31 | 8 | monotonic replay counter |
| 39 | 1 | trigger |
| 40 | 1 | delivery tier |
| 41 | 2 | battery terminal millivolts; zero if unavailable |
| 43 | 2 | health flags |
| 45 | 1 | GNSS state |
| 46 | 4 | signed latitude × 10⁷ |
| 50 | 4 | signed longitude × 10⁷ |
| 54 | 4 | location age in seconds |
| 58 | 16 | first 16 bytes of HMAC-SHA-256 |

The authentication input is bytes 0 through 57 inclusive. Bytes 58 through 73
are the truncated tag. When location is absent, the flag is clear, latitude and
longitude are zero, and age is `0xffffffff`. Decoding a structurally valid frame
does not authenticate it; HMAC verification is mandatory before any state
change, ACK or backend publication.

Message types are `1` SOS start, `2` location update, `3` cancellation, `4`
device status, `5` radio ACK, `6` cloud ACK and `7` caregiver ACK. Trigger values
are `0` none, `1` manual, `2` fall and `3` remote test. Delivery values are `0`
unconfirmed, `1` radio received, `2` cloud accepted and `3` caregiver
acknowledged. GNSS values are `0` unavailable, `1` searching, `2` fresh and `3`
stale.

## Required gateway behavior

For an uplink, the gateway must perform these steps in order:

1. Reject the frame unless its length, magic, version, canonical fields and LoRa
   CRC are valid.
2. Resolve the pendant by `device_id` and load its unique key from protected
   provisioning storage. An unknown device or missing key is rejected.
3. Verify the HMAC in constant time and apply replay/deduplication checks using
   device, session, replay counter, incident and sequence.
4. Persist or enqueue the authenticated uplink before acknowledging it.
5. Transmit a type-5 radio ACK that echoes device ID, incident ID, incident
   sequence, boot session ID and trigger, sets delivery to radio-received, and
   carries a fresh gateway replay counter and valid HMAC.
6. Publish the original 74 payload bytes through the existing Rolork MQTT
   envelope. The envelope RSSI/SNR/frequency metadata remains gateway metadata,
   not part of the pendant HMAC.

MQTT PUBACK is not a CareLink radio ACK and is not caregiver acknowledgement.
After the backend durably accepts the incident it may request a type-6 cloud ACK.
A human acknowledgement generates type 7. These downlinks use the same
authenticated frame and must be serialized with the gateway's receive loop.

The pendant accepts an ACK only when HMAC verification succeeds and device,
session, incident, sequence, trigger, type and delivery tier match its durable
incident. Duplicate or stale ACKs are idempotently ignored. A cancellation stays
pending across reset until its exact sequence receives a radio ACK.

## Security and operations

- Use a unique uniformly random 256-bit HMAC key per pendant. Never use the MAC
  address, a fleet-wide constant or a compiled fallback as a key.
- Store keys with encryption at rest and restrict gateway/backend access. Key
  rotation needs an explicit dual-key migration design; v1 has no key ID field.
- The pendant reserves replay counters in NVS windows, so gaps are expected
  after reset. Counters must never move backward or be reused.
- Incident ID plus sequence is the business idempotency key. A radio retry may
  use a new replay counter while retaining incident and sequence.
- Configure legal frequency, conducted/radiated power, airtime and retry limits
  for the deployment country before enabling transmission.
- The gateway must prioritize emergency traffic and needs durable buffering;
  its current RAM-only queues and receive-only SX1262 driver are not sufficient
  for production CareLink acknowledgement semantics.
