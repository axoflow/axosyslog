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
import string

complex_types_input_data = {
    "data_map": {"key1": 1, "key2": 2},
    "data_map2": {"key1": {"foo1": "value1", "bar1": 1, "baz1": 1.1}, "key2": {"foo1": "value2", "bar1": 2, "baz1": 2.2}},
    "data_array": ["value1", "value2"],
    "data_array2": [{"foo2": "value1", "bar2": 1, "baz2": 1.1}, {"foo2": "value2", "bar2": 2, "baz2": 2.2}],
    "data_tuple": {"foo3": "value1", "bar3": 3},
}

complex_types_table_schema = [
    ("data_map", "Map(String, Int32)"),
    ("data_map2", "Map(String, Tuple(foo1 String, bar1 Int32, baz1 Float32))"),
    ("data_array", "Array(String)"),
    ("data_array2", "Array(Tuple(foo2 String, bar2 Int32, baz2 Float32))"),
    ("data_tuple", "Tuple(foo3 String, bar3 Int32)"),
]

complex_types_proto_file_data = {
    "proto_file_name": "clickhouse_nested_types.proto",
    "server_side_schema_value": "clickhouse_nested_types:TestProto",
}

all_protobuf_types_input_data = {
    "data_bool": True,
    "data_double": 3.14,
    "data_fixed32": 12345678,
    "data_fixed64": 1234567890123456789,
    "data_float": 2.71,
    "data_int32": -12345678,
    "data_int64": -1234567890123456789,
    "data_sfixed32": -12345678,
    "data_sfixed64": -1234567890123456789,
    "data_sint32": -12345678,
    "data_sint64": -1234567890123456789,
    "data_string": "test_string",
    "data_uint32": 12345678,
    "data_uint64": 1234567890123456789,
}

all_protobuf_types_expected_data = {
    "data_bool": 1,
    "data_double": 3.14,
    "data_fixed32": 12345678,
    "data_fixed64": '1234567890123456789',
    "data_float": 2.71,
    "data_int32": -12345678,
    "data_int64": '-1234567890123456789',
    "data_sfixed32": -12345678,
    "data_sfixed64": '-1234567890123456789',
    "data_sint32": -12345678,
    "data_sint64": '-1234567890123456789',
    "data_string": "test_string",
    "data_uint32": 12345678,
    "data_uint64": '1234567890123456789',
}

all_protobuf_types_table_schema = [
    ("data_bool", "UInt8"),
    ("data_double", "Float64"),
    ("data_fixed32", "UInt32"),
    ("data_fixed64", "UInt64"),
    ("data_float", "Float32"),
    ("data_int32", "Int32"),
    ("data_int64", "Int64"),
    ("data_sfixed32", "Int32"),
    ("data_sfixed64", "Int64"),
    ("data_sint32", "Int32"),
    ("data_sint64", "Int64"),
    ("data_string", "String"),
    ("data_uint32", "UInt32"),
    ("data_uint64", "UInt64"),
]

all_protobuf_types_proto_file_data = {
    "proto_file_name": "clickhouse_non_nested_protobuf_types.proto",
    "server_side_schema_value": "clickhouse_non_nested_protobuf_types:TestProto",
}

MAX_INT8 = 127
MIN_INT8 = -128
MAX_INT16 = 32767
MIN_INT16 = -32768
MAX_INT32 = 2147483647
MIN_INT32 = -2147483648
MAX_INT64 = 9223372036854775807
MIN_INT64 = -9223372036854775808
MAX_UINT8 = 255
MIN_UINT8 = 0
MAX_UINT16 = 65535
MIN_UINT16 = 0
MAX_UINT32 = 4294967295
MIN_UINT32 = 0
UINT64 = 1234567890123456789
MIN_UINT64 = 0

MAX_FLOAT32 = 3.4028235e+38
MIN_FLOAT32 = -3.4028235e+38
MAX_FLOAT64 = 1.7976931348623157e+308
MIN_FLOAT64 = -1.7976931348623157e+308

INPUT_STRING = string.ascii_letters + string.digits + "Árvíztűrő tükörfúrógép" + ".,;:!?@#$%^&*_+-=~|[]{}()<>\n\t\r"

clickhouse_server_types_input_data = {
    # Int8:    TINYINT, INT1, BYTE, TINYINT SIGNED, INT1 SIGNED
    "data_int8": MAX_INT8,
    "data_tinyint": MIN_INT8,
    "data_int1": MAX_INT8,
    "data_tinyint_signed": MIN_INT8,
    "data_int1_signed": MAX_INT8,
    # UInt8:   TINYINT UNSIGNED, INT1 UNSIGNED
    "data_uint8": MAX_UINT8,
    "data_tinyint_unsigned": MIN_UINT8,
    "data_int1_unsigned": MAX_UINT8,
    # Int16:   SMALLINT, SMALLINT SIGNED
    "data_int16": MAX_INT16,
    "data_smallint": MIN_INT16,
    "data_smallint_signed": MAX_INT16,
    # UInt16:  SMALLINT UNSIGNED
    "data_uint16": MAX_UINT16,
    "data_smallint_unsigned": MIN_UINT16,
    # Int32:   INT, INTEGER, MEDIUMINT, MEDIUMINT SIGNED, INT SIGNED, INTEGER SIGNED
    "data_int32": MAX_INT32,
    "data_int": MIN_INT32,
    "data_integer": MAX_INT32,
    "data_mediumint": MIN_INT32,
    "data_mediumint_signed": MAX_INT32,
    "data_int_signed": MIN_INT32,
    "data_integer_signed": MAX_INT32,
    # UInt32:  MEDIUMINT UNSIGNED, INT UNSIGNED, INTEGER UNSIGNED
    "data_uint32": MAX_UINT32,
    "data_mediumint_unsigned": MIN_UINT32,
    "data_int_unsigned": MAX_UINT32,
    "data_integer_unsigned": MIN_UINT32,
    # Int64:   BIGINT, SIGNED, BIGINT SIGNED, TIME
    "data_int64": MAX_INT64,
    "data_bigint": MIN_INT64,
    "data_signed": MAX_INT64,
    "data_bigint_signed": MIN_INT64,
    # UInt64:  UNSIGNED, BIGINT UNSIGNED, BIT, SET
    "data_uint64": UINT64,
    "data_unsigned": MIN_UINT64,
    "data_bigint_unsigned": UINT64,
    "data_bit": MIN_UINT64,
    "data_set": UINT64,
    # Float32: FLOAT, REAL, SINGLE
    "data_float32": MAX_FLOAT32,
    "data_float": MIN_FLOAT32,
    "data_real": MAX_FLOAT32,
    "data_single": MIN_FLOAT32,
    # Float64: DOUBLE, DOUBLE PRECISION
    "data_float64": MAX_FLOAT64,
    "data_double": MIN_FLOAT64,
    "data_double_precision": MAX_FLOAT64,
    # String: LONGTEXT, MEDIUMTEXT, TINYTEXT, TEXT, LONGBLOB, MEDIUMBLOB, TINYBLOB, BLOB, VARCHAR, CHAR, CHAR LARGE OBJECT, CHAR VARYING, CHARACTER LARGE OBJECT, CHARACTER VARYING, NCHAR LARGE OBJECT, NCHAR VARYING, NATIONAL CHARACTER LARGE OBJECT, NATIONAL CHARACTER VARYING, NATIONAL CHAR VARYING, NATIONAL CHARACTER, NATIONAL CHAR, BINARY LARGE OBJECT, BINARY VARYING
    "data_binary_large_object": INPUT_STRING,
    "data_binary_varying": INPUT_STRING,
    "data_blob": INPUT_STRING,
    "data_char_large_object": INPUT_STRING,
    "data_char_varying": INPUT_STRING,
    "data_char": INPUT_STRING,
    "data_character_large_object": INPUT_STRING,
    "data_character_varying": INPUT_STRING,
    "data_longblob": INPUT_STRING,
    "data_longtext": INPUT_STRING,
    "data_mediumblob": INPUT_STRING,
    "data_mediumtext": INPUT_STRING,
    "data_national_char_varying": INPUT_STRING,
    "data_national_char": INPUT_STRING,
    "data_national_character_large_object": INPUT_STRING,
    "data_national_character_varying": INPUT_STRING,
    "data_national_character": INPUT_STRING,
    "data_nchar_large_object": INPUT_STRING,
    "data_nchar_varying": INPUT_STRING,
    "data_string": INPUT_STRING,
    "data_text": INPUT_STRING,
    "data_tinyblob": INPUT_STRING,
    "data_tinytext": INPUT_STRING,
    "data_varchar": INPUT_STRING,
    "data_bool": 1,
    "data_uuid": "123e4567-e89b-12d3-a456-426614174000",
    "data_ipv4": "192.168.1.1",
    "data_date": 17897,
    "data_date32": 47482,
    "data_datetime": 1546300800,
    "data_datetime64": 1546300800,
    "data_datetime64_string": '2019-01-01 00:00:00',
}

EXPECTED_OUTPUT_STRING = INPUT_STRING
clickhouse_server_types_expected_data = {
    # Int8:    TINYINT, INT1, BYTE, TINYINT SIGNED, INT1 SIGNED
    "data_int8": MAX_INT8,
    "data_tinyint": MIN_INT8,
    "data_int1": MAX_INT8,
    "data_tinyint_signed": MIN_INT8,
    "data_int1_signed": MAX_INT8,
    # UInt8:   TINYINT UNSIGNED, INT1 UNSIGNED
    "data_uint8": MAX_UINT8,
    "data_tinyint_unsigned": MIN_UINT8,
    "data_int1_unsigned": MAX_UINT8,
    # Int16:   SMALLINT, SMALLINT SIGNED
    "data_int16": MAX_INT16,
    "data_smallint": MIN_INT16,
    "data_smallint_signed": MAX_INT16,
    # UInt16:  SMALLINT UNSIGNED
    "data_uint16": MAX_UINT16,
    "data_smallint_unsigned": MIN_UINT16,
    # Int32:   INT, INTEGER, MEDIUMINT, MEDIUMINT SIGNED, INT SIGNED, INTEGER SIGNED
    "data_int32": MAX_INT32,
    "data_int": MIN_INT32,
    "data_integer": MAX_INT32,
    "data_mediumint": MIN_INT32,
    "data_mediumint_signed": MAX_INT32,
    "data_int_signed": MIN_INT32,
    "data_integer_signed": MAX_INT32,
    # UInt32:  MEDIUMINT UNSIGNED, INT UNSIGNED, INTEGER UNSIGNED
    "data_uint32": MAX_UINT32,
    "data_mediumint_unsigned": MIN_UINT32,
    "data_int_unsigned": MAX_UINT32,
    "data_integer_unsigned": MIN_UINT32,
    # Int64:   BIGINT, SIGNED, BIGINT SIGNED, TIME
    "data_int64": str(MAX_INT64),
    "data_bigint": str(MIN_INT64),
    "data_signed": str(MAX_INT64),
    "data_bigint_signed": str(MIN_INT64),
    # UInt64:  UNSIGNED, BIGINT UNSIGNED, BIT, SET
    "data_uint64": str(UINT64),
    "data_unsigned": str(MIN_UINT64),
    "data_bigint_unsigned": str(UINT64),
    "data_bit": str(MIN_UINT64),
    "data_set": str(UINT64),
    # Float32: FLOAT, REAL, SINGLE
    "data_float32": MAX_FLOAT32,
    "data_float": MIN_FLOAT32,
    "data_real": MAX_FLOAT32,
    "data_single": MIN_FLOAT32,
    # Float64: DOUBLE, DOUBLE PRECISION
    "data_float64": MAX_FLOAT64,
    "data_double": MIN_FLOAT64,
    "data_double_precision": MAX_FLOAT64,
    # String: LONGTEXT, MEDIUMTEXT, TINYTEXT, TEXT, LONGBLOB, MEDIUMBLOB, TINYBLOB, BLOB, VARCHAR, CHAR, CHAR LARGE OBJECT, CHAR VARYING, CHARACTER LARGE OBJECT, CHARACTER VARYING, NCHAR LARGE OBJECT, NCHAR VARYING, NATIONAL CHARACTER LARGE OBJECT, NATIONAL CHARACTER VARYING, NATIONAL CHAR VARYING, NATIONAL CHARACTER, NATIONAL CHAR, BINARY LARGE OBJECT, BINARY VARYING
    "data_binary_large_object": EXPECTED_OUTPUT_STRING,
    "data_binary_varying": EXPECTED_OUTPUT_STRING,
    "data_blob": EXPECTED_OUTPUT_STRING,
    "data_char_large_object": EXPECTED_OUTPUT_STRING,
    "data_char_varying": EXPECTED_OUTPUT_STRING,
    "data_char": EXPECTED_OUTPUT_STRING,
    "data_character_large_object": EXPECTED_OUTPUT_STRING,
    "data_character_varying": EXPECTED_OUTPUT_STRING,
    "data_longblob": EXPECTED_OUTPUT_STRING,
    "data_longtext": EXPECTED_OUTPUT_STRING,
    "data_mediumblob": EXPECTED_OUTPUT_STRING,
    "data_mediumtext": EXPECTED_OUTPUT_STRING,
    "data_national_char_varying": EXPECTED_OUTPUT_STRING,
    "data_national_char": EXPECTED_OUTPUT_STRING,
    "data_national_character_large_object": EXPECTED_OUTPUT_STRING,
    "data_national_character_varying": EXPECTED_OUTPUT_STRING,
    "data_national_character": EXPECTED_OUTPUT_STRING,
    "data_nchar_large_object": EXPECTED_OUTPUT_STRING,
    "data_nchar_varying": EXPECTED_OUTPUT_STRING,
    "data_string": EXPECTED_OUTPUT_STRING,
    "data_text": EXPECTED_OUTPUT_STRING,
    "data_tinyblob": EXPECTED_OUTPUT_STRING,
    "data_tinytext": EXPECTED_OUTPUT_STRING,
    "data_varchar": EXPECTED_OUTPUT_STRING,
    "data_bool": True,
    "data_uuid": "123e4567-e89b-12d3-a456-426614174000",
    "data_ipv4": "192.168.1.1",
    "data_date": '2019-01-01',
    "data_date32": '2100-01-01',
    "data_datetime": '2019-01-01 01:00:00',
    "data_datetime64": '2019-01-01 01:00:00.000',
    "data_datetime64_string": '2019-01-01 01:00:00.000',
}

clickhouse_server_types_table_schema = [
    ("data_int8", "Int8"),
    ("data_tinyint", "TINYINT"),
    ("data_int1", "INT1"),
    ("data_tinyint_signed", "TINYINT SIGNED"),
    ("data_int1_signed", "INT1 SIGNED"),
    ("data_uint8", "UInt8"),
    ("data_tinyint_unsigned", "TINYINT UNSIGNED"),
    ("data_int1_unsigned", "INT1 UNSIGNED"),
    ("data_int16", "Int16"),
    ("data_smallint", "SMALLINT"),
    ("data_smallint_signed", "SMALLINT SIGNED"),
    ("data_uint16", "UInt16"),
    ("data_smallint_unsigned", "SMALLINT UNSIGNED"),
    ("data_int32", "Int32"),
    ("data_int", "INT"),
    ("data_integer", "INTEGER"),
    ("data_mediumint", "MEDIUMINT"),
    ("data_mediumint_signed", "MEDIUMINT SIGNED"),
    ("data_int_signed", "INT SIGNED"),
    ("data_integer_signed", "INTEGER SIGNED"),
    ("data_uint32", "UInt32"),
    ("data_mediumint_unsigned", "MEDIUMINT UNSIGNED"),
    ("data_int_unsigned", "INT UNSIGNED"),
    ("data_integer_unsigned", "INTEGER UNSIGNED"),
    ("data_int64", "Int64"),
    ("data_bigint", "BIGINT"),
    ("data_signed", "SIGNED"),
    ("data_bigint_signed", "BIGINT SIGNED"),
    ("data_uint64", "UInt64"),
    ("data_unsigned", "UNSIGNED"),
    ("data_bigint_unsigned", "BIGINT UNSIGNED"),
    ("data_bit", "BIT"),
    ("data_set", "SET"),
    ("data_float32", "Float32"),
    ("data_float", "FLOAT"),
    ("data_real", "REAL"),
    ("data_single", "SINGLE"),
    ("data_float64", "Float64"),
    ("data_double", "DOUBLE"),
    ("data_double_precision", "DOUBLE PRECISION"),
    ("data_binary_large_object", "BINARY LARGE OBJECT"),
    ("data_binary_varying", "BINARY VARYING"),
    ("data_blob", "BLOB"),
    ("data_char_large_object", "CHAR LARGE OBJECT"),
    ("data_char_varying", "CHAR VARYING"),
    ("data_char", "CHAR"),
    ("data_character_large_object", "CHARACTER LARGE OBJECT"),
    ("data_character_varying", "CHARACTER VARYING"),
    ("data_longblob", "LONGBLOB"),
    ("data_longtext", "LONGTEXT"),
    ("data_mediumblob", "MEDIUMBLOB"),
    ("data_mediumtext", "MEDIUMTEXT"),
    ("data_national_char_varying", "NATIONAL CHAR VARYING"),
    ("data_national_char", "NATIONAL CHAR"),
    ("data_national_character_large_object", "NATIONAL CHARACTER LARGE OBJECT"),
    ("data_national_character_varying", "NATIONAL CHARACTER VARYING"),
    ("data_national_character", "NATIONAL CHARACTER"),
    ("data_nchar_large_object", "NCHAR LARGE OBJECT"),
    ("data_nchar_varying", "NCHAR VARYING"),
    ("data_string", "String"),
    ("data_text", "TEXT"),
    ("data_tinyblob", "TINYBLOB"),
    ("data_tinytext", "TINYTEXT"),
    ("data_varchar", "VARCHAR"),
    ("data_bool", "BOOL"),
    ("data_uuid", "UUID"),
    ("data_ipv4", "IPv4"),
    ("data_date", "DATE"),
    ("data_date32", "DATE32"),
    ("data_datetime", "DateTime('Europe/Budapest')"),
    ("data_datetime64", "DateTime64(3, 'Europe/Budapest')"),
    ("data_datetime64_string", "DateTime64(3, 'Europe/Budapest')"),
]

clickhouse_server_types_proto_file_data = {
    "proto_file_name": "clickhouse_server_types.proto",
    "server_side_schema_value": "clickhouse_server_types:TestProto",
}
