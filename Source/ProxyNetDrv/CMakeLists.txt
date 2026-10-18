project(ProxyNetDrv)

# ProxyNetDrv, "Proxy networking" (experimental, web only): a client-only net driver that joins an ordinary native
# server through a WSS<->UDP game proxy. IpDrv's UTcpNetDriver with the socket calls replaced by the optional
# proxynet ABI module (vendor/glue/ue_proxynet_abi.h -> proxynet-shim.js -> Module.UE.proxynet).
set(SRC_FILES
  "ProxyNetDrv.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/../vendor/glue
  # IpDrv/Inc for WebHttp.h and WebLinks.h only: the hook tables UHTTPDownload and TcpLink/UdpLink ask on
  # the web. Neither has socket types in it, and this package includes nothing else of IpDrv's.
  PRIVATE
  ${CMAKE_SOURCE_DIR}/IpDrv/Inc
)

target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE PROXYNETDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
