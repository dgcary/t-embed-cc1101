#include "NetToolsUtils.h"
#include <WiFi.h>

bool resolveNetToolsTarget(const String &host, IPAddress &address) {
    if (host.length() == 0) return false;

    IPAddress literal;
    if (literal.fromString(host)) {
        address = literal;
        return true;
    }

    return WiFi.hostByName(host.c_str(), address) == 1;
}

uint32_t ipToHostOrder(const IPAddress &ip) {
    return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
           (static_cast<uint32_t>(ip[2]) << 8) | static_cast<uint32_t>(ip[3]);
}

IPAddress hostOrderToIp(uint32_t value) {
    return IPAddress(
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    );
}

String netToolsServiceName(uint16_t port) {
    switch (port) {
        case 20: return "FTP-DATA";
        case 21: return "FTP";
        case 22: return "SSH";
        case 23: return "TELNET";
        case 25: return "SMTP";
        case 53: return "DNS";
        case 80: return "HTTP";
        case 110: return "POP3";
        case 135: return "MSRPC";
        case 139: return "NETBIOS";
        case 143: return "IMAP";
        case 179: return "BGP";
        case 389: return "LDAP";
        case 443: return "HTTPS";
        case 445: return "SMB";
        case 465: return "SMTPS";
        case 514: return "SYSLOG";
        case 515: return "LPD";
        case 554: return "RTSP";
        case 587: return "SMTP-SUB";
        case 631: return "IPP";
        case 636: return "LDAPS";
        case 873: return "RSYNC";
        case 902: return "VMWARE";
        case 993: return "IMAPS";
        case 995: return "POP3S";
        case 1433: return "MSSQL";
        case 1521: return "ORACLE";
        case 1723: return "PPTP";
        case 1883: return "MQTT";
        case 2049: return "NFS";
        case 2375: return "DOCKER";
        case 2376: return "DOCKER-TLS";
        case 3306: return "MYSQL";
        case 3389: return "RDP";
        case 5432: return "POSTGRES";
        case 5900: return "VNC";
        case 5985: return "WINRM";
        case 5986: return "WINRM-TLS";
        case 6379: return "REDIS";
        case 6443: return "K8S-API";
        case 8000: return "HTTP-ALT";
        case 8080: return "HTTP-PROXY";
        case 8443: return "HTTPS-ALT";
        case 9000: return "HTTP-ALT";
        case 9100: return "JETDIRECT";
        case 9200: return "ELASTIC";
        case 10000: return "WEBMIN";
        case 27017: return "MONGODB";
        default: return "";
    }
}
