#ifndef __NETTOOLS_UTILS_H__
#define __NETTOOLS_UTILS_H__

#include <Arduino.h>
#include <IPAddress.h>
#include <stdint.h>

bool resolveNetToolsTarget(const String &host, IPAddress &address);
uint32_t ipToHostOrder(const IPAddress &ip);
IPAddress hostOrderToIp(uint32_t value);
String netToolsServiceName(uint16_t port);

#endif
