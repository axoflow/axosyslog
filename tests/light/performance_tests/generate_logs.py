#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Andras Mitzki <andras.mitzki@axoflow.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import json
import random
import string
import xml.etree.ElementTree as ET


COMMON_CEF_KEYS = [
    "_cefVer",
    "act",
    "agentZoneURI",
    "aggregationType",
    "agt",
    "ahost",
    "aid",
    "amac",
    "app",
    "art",
    "at",
    "atz",
    "av",
    "c6a1",
    "c6a1Label",
    "cat",
    "CAT",
    "catdt",
    "categoryBehavior",
    "categoryDeviceGroup",
    "categoryDeviceType",
    "categoryObject",
    "categoryOutcome",
    "categorySignificance",
    "categoryTupleDescription",
    "cn1",
    "cn1Label",
    "cn2",
    "cn2Label",
    "cn3",
    "cn3Label",
    "cnt",
    "cs1",
    "cs10",
    "cs10Label",
    "cs11",
    "cs11Label",
    "cs1Label",
    "cs2",
    "cs2Label",
    "cs3",
    "cs3Label",
    "cs4",
    "cs4Label",
    "cs5",
    "cs5Label",
    "cs6",
    "cs6Label",
    "cs7",
    "cs7Label",
    "cs8",
    "cs8Label",
    "cs9",
    "cs9Label",
    "destinationDnsDomain",
    "destinationServiceName",
    "destinationZoneURI",
    "deviceCustomDate1",
    "deviceCustomDate1Label",
    "deviceExternalId",
    "devicePayloadId",
    "deviceSeverity",
    "deviceZoneURI",
    "dhost",
    "dmac",
    "dntdom",
    "dpriv",
    "dpt",
    "dst",
    "dtz",
    "duser",
    "dvc",
    "dvchost",
    "dvcmac",
    "end",
    "eventId",
    "externalId",
    "fileHash",
    "filePath",
    "filePermission",
    "fileType",
    "flexNumber1",
    "flexNumber1Label",
    "flexNumber2",
    "flexNumber2Label",
    "flexNumber3",
    "flexNumber3Label",
    "fname",
    "fqdn",
    "fsize",
    "hit_count",
    "in",
    "method",
    "mrt",
    "msg",
    "nat",
    "nfpt",
    "nlpt",
    "oldFilePermission",
    "out",
    "outcome",
    "proto",
    "qtype",
    "reason",
    "repeatCount",
    "request",
    "requestClientApplication",
    "requestMethod",
    "rt",
    "shost",
    "smac",
    "spt",
    "src",
    "start",
    "suid",
    "suser",
    "TrendMicroDsDetectionConfidence",
    "TrendMicroDsFileSHA1",
    "TrendMicroDsFileSHA256",
    "TrendMicroDsMalwareTarget",
    "TrendMicroDsMalwareTargetType",
    "TrendMicroDsRelevantDetectionNames",
    "TrendMicroDsTenant",
    "TrendMicroDsTenantId",
    "view",
]

COMMON_LEEF_KEYS = [
    "cat",
    "dst",
    "dstPort",
    "sev",
    "src",
    "srcPort",
    "usrName",
]

COMMON_KV_KEYS = [
    "action",
    "app",
    "appcat",
    "appid",
    "applist",
    "apprisk",
    "countapp",
    "craction",
    "crlevel",
    "crscore",
    "date",
    "devid",
    "devname",
    "dstcountry",
    "dstintf",
    "dstintfrole",
    "dstip",
    "dstname",
    "dstport",
    "duration",
    "eventtime",
    "lanin",
    "lanout",
    "level",
    "logid",
    "mastersrcmac",
    "osname",
    "policyid",
    "policytype",
    "poluuid",
    "proto",
    "rcvdbyte",
    "rcvdpkt",
    "sentbyte",
    "sentpkt",
    "service",
    "sessionid",
    "srccountry",
    "srcintf",
    "srcintfrole",
    "srcip",
    "srcmac",
    "srcname",
    "srcport",
    "srcserver",
    "srcswversion",
    "subtype",
    "time",
    "trandisp",
    "transip",
    "transport",
    "type",
    "tz",
    "unauthuser",
    "unauthusersource",
    "utmaction",
    "vd",
    "wanin",
    "wanout",
]


def random_string(length):
    return "".join(
        random.choices(string.ascii_letters + string.digits + " ", k=length),
    ).lstrip()


def random_key(keys):
    return random.choice(keys)


def random_syslog_prefix():
    return random.choice(
        [
            # "<13>1 2019-01-18T11:07:53.520Z 192.168.1.1 ",
            "Jan 18 11:07:53 myhostname myprog[12345]: ",
            "<13>Jan 18 11:07:53 192.168.1.1 myprog[12345]: ",
        ],
    )


def generate_kv_pairs(key_list, delimiter=None):
    key = random_key(key_list)
    value_length = random.randint(5, 30)
    value = random_string(value_length)
    if delimiter:
        return f"{key}={value}{delimiter}"
    return f"{key}={value} "


def generate_cef_message(target_length):
    syslog_prefix = random_syslog_prefix()
    cef_header = "CEF:0|DemoVendor|DemoProduct|1.0|100|Test Event|5|"

    extension = ""
    while len(syslog_prefix + cef_header + extension) < target_length:
        extension += generate_kv_pairs(COMMON_CEF_KEYS, " ")

    full_message = (syslog_prefix + cef_header + extension).strip()
    return full_message[:target_length] + "\n"


def generate_kv_message(target_length):
    pri = "<189>"
    kv_pairs = ""
    while len(pri + kv_pairs) < target_length:
        kv_pairs += generate_kv_pairs(COMMON_KV_KEYS, " ")

    full_message = (pri + kv_pairs).strip()
    return full_message[:target_length] + "\n"


def generate_leef_message(target_length=512):
    syslog_prefix = random_syslog_prefix()
    leef_header = "LEEF:2.0|DemoVendor|DemoProduct|1.0|100|"

    extension = "devTime=May 06 2019 11:13:53 GMT "
    while len(syslog_prefix + leef_header + extension) < target_length:
        extension += generate_kv_pairs(COMMON_LEEF_KEYS, "\t")

    full_message = (syslog_prefix + leef_header + extension).strip()
    return full_message[:target_length] + "\n"


def generate_csv_message(target_length=512):
    syslog_prefix = random_syslog_prefix()
    csv_header_list = [
        "1,2014/01/28 01:28:35,007200001056,TRAFFIC,end,",
        "1,2025/03/14 16:28:52,111111111111111,CONFIG,0,",
        "1,2012/10/30 09:46:17,01606001116,THREAT,url,",
        "1,2025/04/02 17:55:34,1111111111111111111,HIPMATCH,0,",
        "1,2021/01/23 00:45:03,012001003714,SYSTEM,userid,",
        "1,2025/03/28 05:40:45,000000000000,USERID,login,",
    ]
    csv_part_list = [
        ":::::RSA",
        "",
        "0",
        "0x0",
        "0x8000000000000000",
        "1.1.1.1",
        "1.1.1.2",
        "1.1.1.3",
        "1",
        "10.1.1.1",
        "1111",
        "111111111111111",
        "2021/01/22 18:00:10",
        "2025-04-02T15:56:19.287+00:00",
        "4",
        "able-to-transfer-file",
        "browser-based",
        "browser-based",
        "encrypted-tunnel",
        "general-internet",
        "has-known-vulnerability",
        "Intermediate CA",
        "internet-utility",
        "N/A",
        "networking",
        "no",
        "None",
        "PADD PADD PADD PADD PADD PADD PADD PADD PADD",
        "pervasive-use",
        "tunnel-other-application",
        "url.com",
        "used-by-malware",
        "web-browsing",
    ]

    csv_header = random_key(csv_header_list)
    msg_content = ""
    while len(syslog_prefix + csv_header + msg_content) < target_length:
        msg_content += random_key(csv_part_list) + ","

    full_message = (syslog_prefix + csv_header + msg_content).strip()
    return full_message[:target_length] + "\n"


def generate_json_message(target_length=512, json_dict={}):
    syslog_prefix = random_syslog_prefix()

    while len(syslog_prefix + json.dumps(json_dict)) < target_length:
        key = random_key(COMMON_KV_KEYS)
        value = random_string(random.randint(5, 30))
        json_dict[key] = value

    full_message = syslog_prefix + json.dumps(json_dict, separators=(",", ":")) + "\n"
    return full_message


def generate_parsed_cef_json_message(target_length=512):
    json_dict = {
        "version": "0",
        "device_vendor": "fireeye",
        "device_product": "HX",
        "device_version": "4.8.0",
        "device_event_class_id": "IOC Hit Found",
        "name": "IOC Hit Found",
        "agent_severity": "10",
    }
    return generate_json_message(target_length, json_dict)


def generate_parsed_leef_json_message(target_length=512):
    json_dict = {
        "version": "1.0",
        "vendor": "Palo Alto Networks",
        "product_name": "PAN-OS Syslog Integration",
        "product_version": "8.1.6",
        "event_id": "trojan/PDF.gen.eiez(268198686)",
    }
    return generate_json_message(target_length, json_dict)


def generate_eventlog_xml_message(target_length=512):
    syslog_prefix = random_syslog_prefix()
    event = ET.Element(
        "Event", xmlns="http://schemas.microsoft.com/win/2004/08/events/event",
    )

    system = ET.SubElement(event, "System")
    ET.SubElement(
        system,
        "Provider",
        Name="DemoApp",
        Guid="{12345678-9abc-def0-1234-56789abcdef0}",
    )
    ET.SubElement(system, "EventID").text = str(random.randint(1000, 9999))
    ET.SubElement(system, "Version").text = "0"
    ET.SubElement(system, "Level").text = str(random.choice([0, 1, 2, 3, 4, 5]))
    ET.SubElement(system, "Task").text = "0"
    ET.SubElement(system, "Opcode").text = "0"
    ET.SubElement(system, "Keywords").text = "0x80000000000000"
    ET.SubElement(system, "TimeCreated", SystemTime="2025-08-04T14:13:00.0000000Z")
    ET.SubElement(system, "EventRecordID").text = str(random.randint(100000, 999999))
    ET.SubElement(system, "Channel").text = "Application"
    ET.SubElement(system, "Computer").text = "TEST-PC"
    ET.SubElement(system, "Security", UserID="S-1-5-18")

    eventdata = ET.SubElement(event, "EventData")
    while len(syslog_prefix + ET.tostring(event).decode("utf-8")) < target_length:
        data = ET.SubElement(eventdata, "Data", Name=random_string(5))
        value = random_string(random.randint(10, 50))
        data.text = value

    xml_bytes = ET.tostring(event, encoding="utf-8")
    xml_str = xml_bytes.decode("utf-8")

    if len(xml_str) < target_length:
        xml_str += "<!-- filler -->" * ((target_length - len(xml_str)) // 15)

    return syslog_prefix + xml_str[:target_length] + "\n"
