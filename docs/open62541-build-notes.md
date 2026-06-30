# open62541 Build Notes

The OPC UA probe used open62541 v1.5.4 with an intentionally reduced feature set.

See `opcua-experiment-log.md` for the chronological investigation notes and the settings that were proven too heavy or incompatible.

The source was cloned separately and then built as an amalgamated `open62541.c` / `open62541.h` pair:

```bash
git clone --depth 1 --branch v1.5.4 https://github.com/open62541/open62541.git external/open62541

cmake -S external/open62541 \
  -B external/open62541-build \
  -DUA_ENABLE_AMALGAMATION=ON \
  -DUA_ENABLE_ENCRYPTION=OFF \
  -DUA_ENABLE_SUBSCRIPTIONS=ON \
  -DUA_ENABLE_SUBSCRIPTIONS_EVENTS=OFF \
  -DUA_ENABLE_HISTORIZING=OFF \
  -DUA_ENABLE_DISCOVERY=OFF \
  -DUA_ENABLE_METHODCALLS=OFF \
  -DUA_ENABLE_NODEMANAGEMENT=ON \
  -DUA_ENABLE_DIAGNOSTICS=OFF \
  -DUA_ENABLE_JSON_ENCODING=OFF \
  -DUA_ENABLE_XML_ENCODING=OFF \
  -DUA_ENABLE_TYPEDESCRIPTION=ON \
  -DUA_ENABLE_STATUSCODE_DESCRIPTIONS=OFF \
  -DUA_ENABLE_PUBSUB=OFF \
  -DUA_ENABLE_PUBSUB_INFORMATIONMODEL=OFF \
  -DUA_NAMESPACE_ZERO=REDUCED \
  -DUA_LOGLEVEL=700 \
  -DUA_ARCHITECTURE=lwip \
  -DUA_MULTITHREADING=0 \
  -DCMAKE_BUILD_TYPE=MinSizeRel

cmake --build external/open62541-build --target open62541-amalgamation -j2
```

The generated files were copied into:

```text
firmware/opcua_probe/components/open62541/
```

The ESP-IDF component vendors the open62541 architecture and dependency sources it needed from the original open62541 tree:

```text
arch/common/eventloop_common.c
arch/common/timer.c
arch/lwip/eventloop_lwip.c
arch/lwip/eventloop_lwip_tcp.c
arch/lwip/eventloop_lwip_udp.c
```

The component must define both `UA_ARCHITECTURE_LWIP` and
`UA_ARCHITECTURE_FREERTOS`. Without the FreeRTOS definition, the lwIP event loop
selects a generic monotonic-clock path that does not drive subscription timers
correctly on this ESP-IDF target.

If regenerating open62541 from a fresh upstream checkout, refresh the amalgamated `open62541.c/.h` files and the vendored `arch/` and `deps/` support files together.

Observed result:

- Build succeeded with the reduced profile above.
- Flash succeeded.
- The tuned firmware image was about `0x77d00` bytes and left about 68% of the default large app partition free.
- The ESP32 answered ping on the W5500 Ethernet link after the Pi-side direct Ethernet interface was assigned `192.168.50.1/24`.
- TCP port `4840` accepted connections.
- A raw OPC UA TCP `HEL` message received a valid `ACKF` response.
- The OPC UA task still uses a conservative 64 KB FreeRTOS stack, but that stack is allocated from PSRAM with `xTaskCreateWithCaps`.

Notes:

- `UA_NAMESPACE_ZERO=REDUCED` plus node management is the current
  browse-compatible profile.
- Disabling XML encoding also required keeping XML value extraction out of scope.
- `UA_ENABLE_TYPEDESCRIPTION=OFF` caused a generated-code compile failure because `UA_DataTypeMember.memberName` was still referenced. Leave typedescription enabled unless the generator/configuration issue is investigated further.
- Turning off `UA_ENABLE_NODEMANAGEMENT` did not prevent variable creation in
  the MINIMAL probe, but REDUCED with node management disabled produced
  malformed session data. Keep node management enabled for this profile.
- REDUCED plus node management restored standard endpoint, namespace, and tree
  browsing while XML, diagnostics, and status-code descriptions remained off.
