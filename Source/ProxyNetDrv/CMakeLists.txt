project(ProxyNetDrv)

# ufront step 2.79 — "Proxy networking": a client-only UE1 net driver that joins an ordinary NATIVE server
# through a WSS<->UDP game proxy. Modelled on IpDrv's UTcpNetDriver, with the BSD socket calls replaced by the
# OPTIONAL proxynet ABI module (vendor/glue/ue_proxynet_abi.h -> proxynet-shim.js -> Module.UE.proxynet).
#
# EXPERIMENTAL and DETACHED BY DECISION (ufrontend docs/12 ADR-16): it shares nothing with WebRTCDrv and edits
# none of its files. -DUT_WITH_PROXYNET=OFF removes the package, its JS shim and its link inputs.
# 436-ONLY: this lives on feat/v436-compat and never on base main — native servers run 436 and later.
set(SRC_FILES
  "ProxyNetDrv.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/../vendor/glue
  # IpDrv/Inc/WebHttp.h only: the hook table UHTTPDownload asks for a redirect download. A header with no
  # socket types in it — this package still includes nothing else of IpDrv's.
  PRIVATE
  ${CMAKE_SOURCE_DIR}/IpDrv/Inc
)

target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE PROXYNETDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
