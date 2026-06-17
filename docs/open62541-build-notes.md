# open62541 Build Notes

The OPC UA probe used open62541 v1.5.4 with an intentionally reduced feature set.

The source was cloned separately and then built as an amalgamated `open62541.c` / `open62541.h` pair:

```bash
git clone --depth 1 --branch v1.5.4 https://github.com/open62541/open62541.git external/open62541

cmake -S external/open62541 \
  -B external/open62541-build \
  -DUA_ENABLE_AMALGAMATION=ON \
  -DUA_ENABLE_ENCRYPTION=OFF \
  -DUA_ENABLE_SUBSCRIPTIONS=OFF \
  -DUA_ENABLE_SUBSCRIPTIONS_EVENTS=OFF \
  -DUA_ENABLE_HISTORIZING=OFF \
  -DUA_ENABLE_DISCOVERY=OFF \
  -DUA_NAMESPACE_ZERO=REDUCED \
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

If regenerating open62541 from a fresh upstream checkout, refresh the amalgamated `open62541.c/.h` files and the vendored `arch/` and `deps/` support files together.

Observed result:

- Build succeeded.
- Flash succeeded.
- OPC UA task needed a 64 KB FreeRTOS task stack.
- Runtime internal heap was very tight.
