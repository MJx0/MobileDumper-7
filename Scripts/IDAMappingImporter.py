"""
IDAMappings importer for IDA Pro.

Imports:
    - global symbols
    - executable functions
    - named vtables
    - pointers/references to global symbols

Struct/enum type import is implemented but disabled for now (not needed
yet) - see the commented-out TypeImporter class below to bring it back.

The binary format is defined by IDAMappingsLayouts and uses:
    #pragma pack(push, 1)
"""

from __future__ import print_function

import os
import struct
import sys

import idaapi
import ida_bytes
import ida_funcs
import ida_kernwin
import ida_name
# import ida_typeinf
import ida_xref
# import idc


# ---------------------------------------------------------------------------
# Format definitions
# ---------------------------------------------------------------------------

class Format(object):
    """Constants defined by the on-disk format itself."""

    FILE_MAGIC = 0xD7
    MIN_VERSION = 2
    INVALID_STRING_OFFSET = 0xFFFFFFFF

    HEADER_SIZE = 51
    NAMED_VARIABLE_SIZE = 12
    NAMED_VTABLE_SIZE = 12
    EXEC_FUNC_SIZE = 20

    # Variable-length record layouts (IDAMappingsLayouts::Enum / ::Struct).
    #
    # struct Enum
    # {
    #     StringOffset Name;                   // 0
    #     uint8_t UnderlyingTypeSizeBytes;      // 4
    #     int32_t NumValues;                    // 5
    #     EnumValue Values[NumValues];          // 9
    # };
    # struct EnumValue { StringOffset Name; int64_t Value; };  // 12 bytes
    # ENUM_HEADER_SIZE = 9
    # ENUM_VALUE_SIZE = 12

    # struct Struct
    # {
    #     StringOffset Name;                    // 0
    #     StringOffset SuperName;                // 4
    #     int32_t Size;                          // 8
    #     int32_t Alignment;                     // 12
    #     int32_t NumMembers;                    // 16
    #     Member Members[NumMembers];             // 20
    # };
    # struct Member
    # {
    #     StringOffset Type;      // 0
    #     StringOffset Name;      // 4
    #     int32_t Offset;         // 8
    #     int32_t Size;           // 12
    #     int32_t ArrayDim;       // 16
    #     bool bIsPointer;        // 20
    #     uint8_t BitFieldBitCount; // 21 -- despite the name, this is the bit
    #                                       -- INDEX, not a width. 0xFF means
    #                                       -- "not a bitfield".
    # };                          // 22 bytes
    # STRUCT_HEADER_SIZE = 20
    # MEMBER_SIZE = 22

    # BITFIELD_NONE = 0xFF

    # IDAMappingsHeader field offsets.
    #
    # struct IDAMappingsHeader
    # {
    #     uint8_t Magic;                         // 0
    #     EIDAMappingsVersion Version;           // 1
    #     uint8_t Reserved;                      // 2
    #     uint32_t StringDataSizeBytes;          // 3
    #     InternalOffset StringDataOffset;       // 7
    #
    #     uint32_t NumEnums;                     // 11
    #     InternalOffset EnumDataOffset;         // 15
    #
    #     uint32_t NumStructs;                   // 19
    #     InternalOffset StructDataOffset;       // 23
    #
    #     uint32_t NumGlobalSymbols;             // 27
    #     InternalOffset GlobalSymbolDataOffset; // 31
    #
    #     uint32_t NumVTables;                   // 35
    #     InternalOffset VTableDataOffset;       // 39
    #
    #     uint32_t NumExecFunctions;             // 43
    #     InternalOffset ExecFunctionDataOffset; // 47
    # };
    HEADER_MAGIC = 0
    HEADER_VERSION = 1
    HEADER_STRING_DATA_SIZE = 3
    HEADER_STRING_DATA_OFFSET = 7

    HEADER_ENUM_COUNT = 11
    HEADER_ENUM_OFFSET = 15

    HEADER_STRUCT_COUNT = 19
    HEADER_STRUCT_OFFSET = 23

    HEADER_GLOBAL_COUNT = 27
    HEADER_GLOBAL_OFFSET = 31

    HEADER_VTABLE_COUNT = 35
    HEADER_VTABLE_OFFSET = 39

    HEADER_FUNCTION_COUNT = 43
    HEADER_FUNCTION_OFFSET = 47


# ---------------------------------------------------------------------------
# Binary reader
# ---------------------------------------------------------------------------

class BinaryReader(object):
    """Bounds-checked little-endian binary reader."""

    def __init__(self, data):
        self._data = data
        self._size = len(data)

    @property
    def size(self):
        return self._size

    def can_read(self, offset, size):
        if offset < 0 or size < 0:
            return False

        return (
            offset <= self._size and
            size <= self._size - offset
        )

    def u8(self, offset):
        self._require(offset, 1)
        return struct.unpack_from("<B", self._data, offset)[0]

    def u16(self, offset):
        self._require(offset, 2)
        return struct.unpack_from("<H", self._data, offset)[0]

    def u32(self, offset):
        self._require(offset, 4)
        return struct.unpack_from("<I", self._data, offset)[0]

    def i32(self, offset):
        self._require(offset, 4)
        return struct.unpack_from("<i", self._data, offset)[0]

    def i64(self, offset):
        self._require(offset, 8)
        return struct.unpack_from("<q", self._data, offset)[0]

    def bytes(self, offset, size):
        self._require(offset, size)
        return self._data[offset:offset + size]

    def _require(self, offset, size):
        if not self.can_read(offset, size):
            raise ValueError(
                "out-of-bounds read: offset=0x{:X}, size=0x{:X}".format(
                    offset,
                    size,
                )
            )


# ---------------------------------------------------------------------------
# Mapping parser
# ---------------------------------------------------------------------------

class MappingParser(object):
    """
    Parser for the portions of IDAMappings used by this importer.

    This class has no dependency on IDA and can therefore be unit-tested
    independently.
    """

    def __init__(self, path):
        self.path = os.path.abspath(path)

        with open(self.path, "rb") as stream:
            self._reader = BinaryReader(stream.read())

        self.magic = None
        self.version = None
        self.string_data_size = None
        self.string_data_offset = None

        self.enum_count = None
        self.enum_offset = None

        self.struct_count = None
        self.struct_offset = None

        self.global_count = None
        self.global_offset = None

        self.vtable_count = None
        self.vtable_offset = None

        self.function_count = None
        self.function_offset = None

    @property
    def reader(self):
        return self._reader

    def parse(self):
        self._parse_header()

        if not self._validate_header():
            raise ValueError("invalid IDAMappings header")

    def _parse_header(self):
        reader = self.reader

        if not reader.can_read(0, Format.HEADER_SIZE):
            raise ValueError("file is smaller than IDAMappingsHeader")

        self.magic = reader.u8(Format.HEADER_MAGIC)
        self.version = reader.u8(Format.HEADER_VERSION)

        self.string_data_size = reader.u32(
            Format.HEADER_STRING_DATA_SIZE
        )
        self.string_data_offset = reader.u32(
            Format.HEADER_STRING_DATA_OFFSET
        )

        self.enum_count = reader.u32(Format.HEADER_ENUM_COUNT)
        self.enum_offset = reader.u32(Format.HEADER_ENUM_OFFSET)

        self.struct_count = reader.u32(Format.HEADER_STRUCT_COUNT)
        self.struct_offset = reader.u32(Format.HEADER_STRUCT_OFFSET)

        self.global_count = reader.u32(
            Format.HEADER_GLOBAL_COUNT
        )
        self.global_offset = reader.u32(
            Format.HEADER_GLOBAL_OFFSET
        )

        self.vtable_count = reader.u32(
            Format.HEADER_VTABLE_COUNT
        )
        self.vtable_offset = reader.u32(
            Format.HEADER_VTABLE_OFFSET
        )

        self.function_count = reader.u32(
            Format.HEADER_FUNCTION_COUNT
        )
        self.function_offset = reader.u32(
            Format.HEADER_FUNCTION_OFFSET
        )

    def _validate_header(self):
        reader = self.reader

        if self.magic != Format.FILE_MAGIC:
            print(
                "[!] Invalid magic: expected 0x{:02X}, got 0x{:02X}".format(
                    Format.FILE_MAGIC,
                    self.magic,
                )
            )
            return False

        if self.version < Format.MIN_VERSION:
            print(
                "[!] Unsupported mapping version: {}".format(
                    self.version
                )
            )
            return False

        if not reader.can_read(
            self.string_data_offset,
            self.string_data_size,
        ):
            print("[!] StringData is outside the file")
            return False

        # Enum/Struct records are variable-length, so only the section start
        # can be sanity-checked here. get_enums()/get_structs() bounds-check
        # every individual record as they walk the section and stop early on
        # truncation rather than raising, so a malformed enum/struct section
        # degrades gracefully instead of blocking the rest of the import.
        if self.enum_count and not reader.can_read(self.enum_offset, 0):
            print("[!] EnumData start is outside the file")
            return False

        if self.struct_count and not reader.can_read(self.struct_offset, 0):
            print("[!] StructData start is outside the file")
            return False

        if not self._valid_array(
            self.global_offset,
            self.global_count,
            Format.NAMED_VARIABLE_SIZE,
        ):
            print("[!] GlobalSymbolData is outside the file")
            return False

        if not self._valid_array(
            self.vtable_offset,
            self.vtable_count,
            Format.NAMED_VTABLE_SIZE,
        ):
            print("[!] VTableData is outside the file")
            return False

        if not self._valid_array(
            self.function_offset,
            self.function_count,
            Format.EXEC_FUNC_SIZE,
        ):
            print("[!] ExecFunctionData is outside the file")
            return False

        return True

    def _valid_array(self, offset, count, element_size):
        reader = self.reader

        try:
            size = count * element_size
        except Exception:
            return False

        return reader.can_read(offset, size)

    def get_string(self, string_offset):
        """
        Equivalent to MappingParser::GetNameFromOffset().

        StringOffset is relative to StringDataOffset and points to the
        StringData header:

            uint16_t StringLength
            char     Utf8StringData[]
        """

        if string_offset == Format.INVALID_STRING_OFFSET:
            return ""

        if string_offset > self.string_data_size:
            return ""

        string_start = (
            self.string_data_offset +
            string_offset
        )

        reader = self.reader

        if not reader.can_read(string_start, 2):
            return ""

        string_length = reader.u16(string_start)

        last_accessed = (
            string_offset +
            2 +
            string_length
        )

        if last_accessed > self.string_data_size:
            return ""

        data_start = string_start + 2

        if not reader.can_read(data_start, string_length):
            return ""

        raw = reader.bytes(data_start, string_length)

        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("utf-8", "replace")

    def get_globals(self):
        reader = self.reader
        result = []

        for index in range(self.global_count):
            offset = (
                self.global_offset +
                index * Format.NAMED_VARIABLE_SIZE
            )

            variable_offset = reader.u32(offset)
            type_offset = reader.u32(offset + 4)
            name_offset = reader.u32(offset + 8)

            result.append({
                "offset": variable_offset,
                "type": self.get_string(type_offset),
                "name": self.get_string(name_offset),
            })

        return result

    def get_vtables(self):
        reader = self.reader
        result = []

        for index in range(self.vtable_count):
            offset = (
                self.vtable_offset +
                index * Format.NAMED_VTABLE_SIZE
            )

            vtable_offset = reader.u32(offset)
            super_vtable_offset = reader.u32(offset + 4)
            name_offset = reader.u32(offset + 8)

            result.append({
                "offset": vtable_offset,
                "super_offset": super_vtable_offset,
                "name": self.get_string(name_offset),
            })

        return result

    def get_functions(self):
        reader = self.reader
        result = []

        for index in range(self.function_count):
            offset = (
                self.function_offset +
                index * Format.EXEC_FUNC_SIZE
            )

            mangled_offset = reader.u32(offset)
            unmangled_offset = reader.u32(offset + 4)
            function_offset = reader.u32(offset + 8)
            cpp_signature_offset = reader.u32(offset + 12)
            fallback_signature_offset = reader.u32(offset + 16)

            result.append({
                "offset": function_offset,
                "mangled": self.get_string(mangled_offset),
                "unmangled": self.get_string(unmangled_offset),
                "cpp_signature": self.get_string(cpp_signature_offset),
                "fallback_signature": self.get_string(
                    fallback_signature_offset
                ),
            })

        return result

    # def get_enums(self):
        # """
        # Walk the variable-length Enum section.

        # Stops (rather than raising) at the first record that doesn't fit in
        # the file, so a truncated/corrupt tail degrades gracefully.
        # """

        # reader = self.reader
        # result = []
        # offset = self.enum_offset

        # for _ in range(self.enum_count):
            # if not reader.can_read(offset, Format.ENUM_HEADER_SIZE):
                # print("[!] EnumData truncated, stopping early")
                # break

            # name_offset = reader.u32(offset)
            # underlying_size = reader.u8(offset + 4)
            # num_values = reader.u32(offset + 5)

            # values_start = offset + Format.ENUM_HEADER_SIZE
            # values = []

            # for value_index in range(num_values):
                # value_offset = (
                    # values_start +
                    # value_index * Format.ENUM_VALUE_SIZE
                # )

                # if not reader.can_read(value_offset, Format.ENUM_VALUE_SIZE):
                    # print("[!] Enum value data truncated, stopping early")
                    # break

                # value_name_offset = reader.u32(value_offset)
                # value = reader.i64(value_offset + 4)

                # values.append({
                    # "name": self.get_string(value_name_offset),
                    # "value": value,
                # })

            # result.append({
                # "name": self.get_string(name_offset),
                # "underlying_size": underlying_size,
                # "values": values,
            # })

            # offset = values_start + num_values * Format.ENUM_VALUE_SIZE

        # return result

    # def get_structs(self):
        # """
        # Walk the variable-length Struct section.

        # Stops (rather than raising) at the first record that doesn't fit in
        # the file, so a truncated/corrupt tail degrades gracefully.
        # """

        # reader = self.reader
        # result = []
        # offset = self.struct_offset

        # for _ in range(self.struct_count):
            # if not reader.can_read(offset, Format.STRUCT_HEADER_SIZE):
                # print("[!] StructData truncated, stopping early")
                # break

            # name_offset = reader.u32(offset)
            # super_name_offset = reader.u32(offset + 4)
            # size = reader.i32(offset + 8)
            # alignment = reader.i32(offset + 12)
            # num_members = reader.u32(offset + 16)

            # members_start = offset + Format.STRUCT_HEADER_SIZE
            # members = []

            # for member_index in range(num_members):
                # member_offset = (
                    # members_start +
                    # member_index * Format.MEMBER_SIZE
                # )

                # if not reader.can_read(member_offset, Format.MEMBER_SIZE):
                    # print("[!] Struct member data truncated, stopping early")
                    # break

                # type_offset = reader.u32(member_offset)
                # member_name_offset = reader.u32(member_offset + 4)
                # member_field_offset = reader.i32(member_offset + 8)
                # member_size = reader.i32(member_offset + 12)
                # array_dim = reader.i32(member_offset + 16)
                # is_pointer = reader.u8(member_offset + 20) != 0
                # bitfield_bit = reader.u8(member_offset + 21)

                # members.append({
                    # "type": self.get_string(type_offset),
                    # "name": self.get_string(member_name_offset),
                    # "offset": member_field_offset,
                    # "size": member_size,
                    # "array_dim": array_dim,
                    # "is_pointer": is_pointer,
                    # "bitfield_bit": bitfield_bit,
                # })

            # super_name = (
                # self.get_string(super_name_offset)
                # if super_name_offset != Format.INVALID_STRING_OFFSET
                # else None
            # )

            # result.append({
                # "name": self.get_string(name_offset),
                # "super_name": super_name or None,
                # "size": size,
                # "alignment": alignment,
                # "members": members,
            # })

            # offset = members_start + num_members * Format.MEMBER_SIZE

        # return result


# ---------------------------------------------------------------------------
# Type helpers (no IDA dependency - shared logic, unit-testable)
# ---------------------------------------------------------------------------

# Maps the exact C type strings emitted by IDAMappingGenerator::GetIDACppType
# / ConvertPredefinedTypeForIDA to their size in bytes.
# PRIMITIVE_SIZES = {
    # "bool": 1,
    # "char": 1,
    # "wchar_t": 2,
    # "unsigned __int8": 1,
    # "__int8": 1,
    # "unsigned __int16": 2,
    # "__int16": 2,
    # "unsigned int": 4,
    # "int": 4,
    # "float": 4,
    # "unsigned __int64": 8,
    # "__int64": 8,
    # "double": 8,
# }


# def strip_type_prefix(type_name):
    # """Strip a leading "struct "/"enum " so the bare type name can be looked up."""

    # if type_name.startswith("struct "):
        # return type_name[len("struct "):]

    # if type_name.startswith("enum "):
        # return type_name[len("enum "):]

    # return type_name


# def idc_type_for_byte_size(size):
    # """Best-effort unsigned integer type name for a given byte size."""

    # if size >= 8:
        # return "unsigned __int64"
    # if size >= 4:
        # return "unsigned int"
    # if size >= 2:
        # return "unsigned __int16"
    # return "unsigned __int8"


# def infer_bitfield_widths(members):
    # """
    # The on-disk format only stores each bitfield's starting bit index, not
    # its width. Reconstruct an approximate width by grouping bitfields that
    # share the same (offset, size) storage unit, sorting by bit index, and
    # taking the gap to the next bitfield in the same unit (or to the end of
    # the unit, in bits, for the last one).

    # Returns a dict mapping id(member) -> inferred bit width.
    # """

    # widths = {}
    # groups = {}

    # for member in members:
        # if member["bitfield_bit"] == Format.BITFIELD_NONE:
            # continue

        # key = (member["offset"], member["size"])
        # groups.setdefault(key, []).append(member)

    # for (_offset, size), group in groups.items():
        # group.sort(key=lambda m: m["bitfield_bit"])
        # unit_bits = max(size, 1) * 8

        # for index, member in enumerate(group):
            # bit_index = member["bitfield_bit"]

            # if index + 1 < len(group):
                # next_bit = group[index + 1]["bitfield_bit"]
            # else:
                # next_bit = unit_bits

            # width = next_bit - bit_index

            # if width <= 0:
                # width = 1

            # widths[id(member)] = width

    # return widths


# def sanitize_identifier(name, fallback):
    # """Make a name safe to use as a struct/enum/member identifier."""

    # if not name:
        # return fallback

    # out = []
    # for ch in name:
        # if ch.isalnum() or ch == "_":
            # out.append(ch)
        # else:
            # out.append("_")

    # cleaned = "".join(out)

    # if not cleaned or cleaned[0].isdigit():
        # cleaned = "_" + cleaned

    # return cleaned or fallback


# ---------------------------------------------------------------------------
# IDA type creation
#
# Uses the ida_typeinf "Local Types" API (tinfo_t / udt_type_data_t /
# enum_type_data_t), which is the current, forward-compatible way to define
# structs and enums in IDA (the legacy ida_struct/ida_enum modules were
# removed in IDA 9.0). This code could not be exercised against a real IDA
# instance while writing it - every type-creation call is wrapped so a
# single unexpected failure only skips that one type/member instead of
# aborting the whole import. Please report anything that misbehaves.
#
# Disabled for now - not needed yet. Uncomment this class and the
# get_enums()/get_structs()/ask_import_type_info() pieces above/below, and
# the import_type_info wiring in main()/run(), to bring it back.
# ---------------------------------------------------------------------------

# class TypeImporter(object):

    # def __init__(self, parser):
        # self.parser = parser
        # self.til = ida_typeinf.get_idati()

        # self.enums = parser.get_enums()
        # self.structs = parser.get_structs()

        # self.known_enum_names = set(
            # e["name"] for e in self.enums if e["name"]
        # )
        # self.known_struct_names = set(
            # s["name"] for s in self.structs if s["name"]
        # )

        # self.stats = {
            # "enums": {"created": 0, "failed": 0},
            # "structs": {"created": 0, "failed": 0},
            # "members": {"applied": 0, "skipped": 0, "bitfields_merged": 0},
        # }

    # -- primitive/basic type resolution ---------------------------------

    # def get_primitive_type(self, c_decl):
        # try:
            # parsed = idc.parse_decl(c_decl + " x;", idc.PT_SILENT)
        # except Exception:
            # parsed = None

        # if not parsed:
            # return None

        # _name, type_bytes, field_bytes = parsed

        # tif = ida_typeinf.tinfo_t()

        # try:
            # if tif.deserialize(self.til, type_bytes, field_bytes):
                # return tif
        # except Exception:
            # pass

        # return None

    # def get_named_type(self, name):
        # tif = ida_typeinf.tinfo_t()

        # try:
            # if tif.get_named_type(self.til, name):
                # return tif
        # except Exception:
            # pass

        # return None

    # def resolve_member_type(self, type_name, is_pointer, array_dim, fallback_size):
        # base_name = strip_type_prefix(type_name)

        # tif = None

        # if type_name in PRIMITIVE_SIZES:
            # tif = self.get_primitive_type(type_name)
        # elif base_name in self.known_enum_names:
            # tif = self.get_named_type(base_name)
        # elif base_name in self.known_struct_names:
            # tif = self.get_named_type(base_name)

        # if tif is None:
            # Unknown/unsupported type - fall back to an opaque byte blob of
            # the reported size so the struct's overall layout still lines
            # up, rather than dropping the member (and everything after it)
            # entirely.
            # tif = self.get_primitive_type("unsigned __int8")

            # if tif is None:
                # return None

            # if fallback_size and fallback_size > 1:
                # array_tif = ida_typeinf.tinfo_t()
                # if array_tif.create_array(tif, fallback_size):
                    # return array_tif

            # return tif

        # if is_pointer:
            # ptr_tif = ida_typeinf.tinfo_t()
            # if ptr_tif.create_ptr(tif):
                # tif = ptr_tif

        # if array_dim and array_dim > 1:
            # array_tif = ida_typeinf.tinfo_t()
            # if array_tif.create_array(tif, array_dim):
                # return array_tif

        # return tif

    # -- enums -------------------------------------------------------------

    # def create_enum(self, enum_info):
        # name = sanitize_identifier(enum_info["name"], None)

        # if not name:
            # self.stats["enums"]["failed"] += 1
            # return False

        # try:
            # edt = ida_typeinf.enum_type_data_t()
            # seen_names = set()

            # for value_info in enum_info["values"]:
                # value_name = sanitize_identifier(value_info["name"], None)

                # if not value_name or value_name in seen_names:
                    # continue

                # seen_names.add(value_name)

                # edm = ida_typeinf.edm_t()
                # edm.name = value_name
                # edm.value = value_info["value"] & 0xFFFFFFFFFFFFFFFF

                # edt.push_back(edm)

            # tif = ida_typeinf.tinfo_t()

            # if not tif.create_enum(edt):
                # raise ValueError("create_enum failed")

            # tif.set_named_type(self.til, name, ida_typeinf.NTF_REPLACE)

            # self.stats["enums"]["created"] += 1
            # return True

        # except Exception as exc:
            # print(
                # "[!] Could not create enum {!r}: {}".format(
                    # enum_info["name"],
                    # exc,
                # )
            # )
            # self.stats["enums"]["failed"] += 1
            # return False

    # -- structs -------------------------------------------------------------

    # def create_struct_shell(self, name):
        # try:
            # udt = ida_typeinf.udt_type_data_t()
            # tif = ida_typeinf.tinfo_t()

            # if not tif.create_udt(udt, ida_typeinf.BTF_STRUCT):
                # return False

            # return bool(
                # tif.set_named_type(self.til, name, ida_typeinf.NTF_REPLACE)
                # or True
            # )

        # except Exception as exc:
            # print(
                # "[!] Could not create struct shell {!r}: {}".format(
                    # name,
                    # exc,
                # )
            # )
            # return False

    # def populate_struct(self, struct_info):
        # name = sanitize_identifier(struct_info["name"], None)

        # if not name:
            # self.stats["structs"]["failed"] += 1
            # return False

        # try:
            # tif = self.get_named_type(name)

            # if tif is None:
                # raise ValueError("shell type not found")

            # udt = ida_typeinf.udt_type_data_t()

            # if not tif.get_udt_details(udt):
                # raise ValueError("get_udt_details failed")

            # udt.clear()

            # members = struct_info["members"]
            # bitfield_widths = infer_bitfield_widths(members)

            # seen_bitfield_units = set()
            # used_names = set()
            # applied = 0
            # skipped = 0
            # merged = 0

            # def unique_name(base):
                # candidate = base
                # suffix = 0
                # while candidate in used_names:
                    # suffix += 1
                    # candidate = "{}_{}".format(base, suffix)
                # used_names.add(candidate)
                # return candidate

            # for member in sorted(members, key=lambda m: m["offset"]):
                # is_bitfield = member["bitfield_bit"] != Format.BITFIELD_NONE

                # if is_bitfield:
                    # unit_key = (member["offset"], member["size"])

                    # if unit_key in seen_bitfield_units:
                        # continue

                    # seen_bitfield_units.add(unit_key)

                    # group = sorted(
                        # (
                            # m for m in members
                            # if m["bitfield_bit"] != Format.BITFIELD_NONE
                            # and (m["offset"], m["size"]) == unit_key
                        # ),
                        # key=lambda m: m["bitfield_bit"],
                    # )

                    # member_tif = self.get_primitive_type(
                        # idc_type_for_byte_size(member["size"])
                    # )

                    # if member_tif is None:
                        # skipped += len(group)
                        # continue

                    # bit_desc = ", ".join(
                        # "{}(bit {}, ~{} wide)".format(
                            # m["name"] or "?",
                            # m["bitfield_bit"],
                            # bitfield_widths.get(id(m), 1),
                        # )
                        # for m in group
                    # )

                    # udm = ida_typeinf.udm_t()
                    # udm.name = unique_name(
                        # sanitize_identifier(
                            # (group[0]["name"] or "bitfield") + "_bits",
                            # "bitfield_{:X}".format(member["offset"]),
                        # )
                    # )
                    # udm.type = member_tif
                    # udm.offset = member["offset"] * 8

                    # try:
                        # udm.size = member_tif.get_size() * 8
                    # except Exception:
                        # udm.size = max(member["size"], 1) * 8

                    # udm.cmt = (
                        # "Merged bitfield (widths are inferred, not exact): "
                        # + bit_desc
                    # )

                    # udt.push_back(udm)
                    # applied += 1
                    # merged += len(group) - 1
                    # continue

                # member_tif = self.resolve_member_type(
                    # member["type"],
                    # member["is_pointer"],
                    # member["array_dim"],
                    # member["size"],
                # )

                # if member_tif is None:
                    # skipped += 1
                    # continue

                # udm = ida_typeinf.udm_t()
                # udm.name = unique_name(
                    # sanitize_identifier(
                        # member["name"],
                        # "field_{:X}".format(member["offset"]),
                    # )
                # )
                # udm.type = member_tif
                # udm.offset = member["offset"] * 8

                # try:
                    # udm.size = member_tif.get_size() * 8
                # except Exception:
                    # udm.size = max(member["size"], 1) * 8

                # udt.push_back(udm)
                # applied += 1

            # new_tif = ida_typeinf.tinfo_t()

            # if not new_tif.create_udt(udt, ida_typeinf.BTF_STRUCT):
                # raise ValueError("create_udt failed while populating members")

            # new_tif.set_named_type(self.til, name, ida_typeinf.NTF_REPLACE)

            # Best-effort: make sure the type's overall size matches what was
            # reflected, in case trailing bytes were never covered by a
            # member (eg. all-skipped tail, or reserved padding).
            # try:
                # if struct_info["size"] and new_tif.get_size() < struct_info["size"]:
                    # pad_udt = ida_typeinf.udt_type_data_t()
                    # if new_tif.get_udt_details(pad_udt):
                        # pad_size = struct_info["size"] - new_tif.get_size()
                        # pad_tif = self.get_primitive_type("unsigned __int8")

                        # if pad_tif is not None:
                            # array_tif = ida_typeinf.tinfo_t()
                            # if array_tif.create_array(pad_tif, pad_size):
                                # pad_member = ida_typeinf.udm_t()
                                # pad_member.name = unique_name("_pad_tail")
                                # pad_member.type = array_tif
                                # pad_member.offset = new_tif.get_size() * 8
                                # pad_udt.push_back(pad_member)

                                # padded_tif = ida_typeinf.tinfo_t()
                                # if padded_tif.create_udt(pad_udt, ida_typeinf.BTF_STRUCT):
                                    # padded_tif.set_named_type(
                                        # self.til,
                                        # name,
                                        # ida_typeinf.NTF_REPLACE,
                                    # )
            # except Exception:
                # pass

            # self.stats["structs"]["created"] += 1
            # self.stats["members"]["applied"] += applied
            # self.stats["members"]["skipped"] += skipped
            # self.stats["members"]["bitfields_merged"] += merged
            # return True

        # except Exception as exc:
            # print(
                # "[!] Could not populate struct {!r}: {}".format(
                    # struct_info["name"],
                    # exc,
                # )
            # )
            # self.stats["structs"]["failed"] += 1
            # return False

    # -- driver --------------------------------------------------------------

    # def run(self):
        # print("[*] Importing enums...")

        # for enum_info in self.enums:
            # self.create_enum(enum_info)

        # print("[*] Creating struct shells...")

        # shells_ok = set()

        # for struct_info in self.structs:
            # name = sanitize_identifier(struct_info["name"], None)

            # if name and self.create_struct_shell(name):
                # shells_ok.add(struct_info["name"])

        # print("[*] Populating struct members...")

        # for struct_info in self.structs:
            # if struct_info["name"] not in shells_ok:
                # continue

            # self.populate_struct(struct_info)

        # return self.stats


# ---------------------------------------------------------------------------
# IDA symbol handling
# ---------------------------------------------------------------------------

class IDAImporter(object):

    def __init__(self, parser):
        self.parser = parser
        self.image_base = idaapi.get_imagebase()

        self.stats = {
            "globals": {
                "renamed": 0,
                "failed": 0,
                "pointers": 0,
            },
            "functions": {
                "renamed": 0,
                "failed": 0,
            },
            "vtables": {
                "renamed": 0,
                "failed": 0,
            },
        }

        self.type_stats = None

    def to_ea(self, relative_offset):
        return self.image_base + relative_offset

    def rename(self, ea, name, stats):
        """
        Rename an IDA address and update the supplied statistics object.

        Passing the stats object directly avoids having a separate category
        string that must match self.stats exactly.
        """

        if not name:
            stats["failed"] += 1
            return False

        if ea == idaapi.BADADDR:
            stats["failed"] += 1
            return False

        old_name = ida_name.get_name(ea)

        # Don't unnecessarily call set_name when the requested name is
        # already present.
        if old_name == name:
            stats["renamed"] += 1
            return True

        try:
            success = ida_name.set_name(
                ea,
                name,
                ida_name.SN_NOWARN,
            )
        except Exception as exc:
            print(
                "[!] Rename failed at 0x{:X}: {}".format(
                    ea,
                    exc,
                )
            )
            success = False

        if not success:
            print(
                "[!] Could not rename 0x{:X} to {!r}".format(
                    ea,
                    name,
                )
            )
            stats["failed"] += 1
            return False

        print(
            "[+] 0x{:X}: {!r} -> {!r}".format(
                ea,
                old_name,
                name,
            )
        )

        stats["renamed"] += 1
        return True

    def ensure_function(self, ea):
        if ida_funcs.get_func(ea) is not None:
            return True

        try:
            return bool(ida_funcs.add_func(ea))
        except Exception as exc:
            print(
                "[!] Could not create function at 0x{:X}: {}".format(
                    ea,
                    exc,
                )
            )
            return False

    def rename_global_pointer_refs(self, ea, name):
        """
        Find data locations that directly contain a pointer to the global
        at `ea` and rename those locations to:

            <name>_ptr_<index>

        Only data references are considered. Code references such as:

            mov rax, cs:GObjects

        are intentionally left alone.

        Pointer indices are assigned in ascending address order so the
        generated names are deterministic.
        """

        if not name:
            return

        refs = []

        ref = ida_xref.get_first_dref_to(ea)

        while ref != idaapi.BADADDR:
            flags = ida_bytes.get_flags(ref)

            # Ignore code/instruction references.
            if ida_bytes.is_data(flags):
                try:
                    value = ida_bytes.get_qword(ref)
                except Exception:
                    value = None

                # Only accept an actual pointer-sized data value that
                # contains the address of this global.
                if value == ea:
                    refs.append(ref)

            ref = ida_xref.get_next_dref_to(ea, ref)

        # Make pointer numbering deterministic.
        refs.sort()

        for index, ref in enumerate(refs):
            pointer_name = "{}_ptr_{}".format(
                name,
                index,
            )

            old_name = ida_name.get_name(ref)

            if old_name == pointer_name:
                self.stats["globals"]["pointers"] += 1
                continue

            try:
                success = ida_name.set_name(
                    ref,
                    pointer_name,
                    ida_name.SN_NOWARN,
                )
            except Exception as exc:
                print(
                    "[!] Could not rename global pointer "
                    "0x{:X} to {!r}: {}".format(
                        ref,
                        pointer_name,
                        exc,
                    )
                )
                continue

            if success:
                print(
                    "[+] Global pointer 0x{:X}: "
                    "{!r} -> {!r}".format(
                        ref,
                        old_name,
                        pointer_name,
                    )
                )

                self.stats["globals"]["pointers"] += 1

    def import_globals(self):
        stats = self.stats["globals"]

        for entry in self.parser.get_globals():
            ea = self.to_ea(entry["offset"])
            name = entry["name"]

            renamed = self.rename(
                ea,
                name,
                stats,
            )

            # Even if the global was already named correctly, we still
            # perform the pointer-reference pass.
            if name:
                self.rename_global_pointer_refs(
                    ea,
                    name,
                )

    def import_functions(self):
        stats = self.stats["functions"]

        for entry in self.parser.get_functions():
            ea = self.to_ea(entry["offset"])

            self.ensure_function(ea)

            # The C++ parser exposes both. Prefer the demangled/unmangled
            # representation when available because it is more useful inside
            # IDA.
            name = (
                entry["unmangled"] or
                entry["mangled"]
            )

            self.rename(
                ea,
                name,
                stats,
            )

    def import_vtables(self):
        stats = self.stats["vtables"]

        for entry in self.parser.get_vtables():
            ea = self.to_ea(entry["offset"])

            self.rename(
                ea,
                entry["name"],
                stats,
            )

    # def import_types(self):
        # self.type_stats = TypeImporter(self.parser).run()

    def run(self, import_type_info):
        print("")
        print("=" * 72)
        print("IDAMappings importer")
        print("=" * 72)

        print("Mapping:    {}".format(self.parser.path))
        print("Image base: 0x{:X}".format(self.image_base))
        print("Version:    {}".format(self.parser.version))
        print("Globals:    {}".format(self.parser.global_count))
        print("Functions:  {}".format(self.parser.function_count))
        print("VTables:    {}".format(self.parser.vtable_count))
        print("Enums:      {}".format(self.parser.enum_count))
        print("Structs:    {}".format(self.parser.struct_count))

        print("")
        print("[*] Importing globals...")
        self.import_globals()

        print("[*] Importing functions...")
        self.import_functions()

        print("[*] Importing vtables...")
        self.import_vtables()

        # if import_type_info:
            # self.import_types()

        print("")
        print("-" * 72)
        print("Import summary")
        print("-" * 72)

        for category in ("globals", "functions", "vtables"):
            stats = self.stats[category]

            line = "{:<12} renamed={} failed={}".format(
                category,
                stats["renamed"],
                stats["failed"],
            )

            if category == "globals":
                line += " pointers={}".format(
                    stats["pointers"]
                )

            print(line)

        # if self.type_stats is not None:
            # print(
                # "{:<12} created={} failed={}".format(
                    # "enums",
                    # self.type_stats["enums"]["created"],
                    # self.type_stats["enums"]["failed"],
                # )
            # )
            # print(
                # "{:<12} created={} failed={}".format(
                    # "structs",
                    # self.type_stats["structs"]["created"],
                    # self.type_stats["structs"]["failed"],
                # )
            # )
            # print(
                # "{:<12} applied={} skipped={} bitfields_merged={}".format(
                    # "members",
                    # self.type_stats["members"]["applied"],
                    # self.type_stats["members"]["skipped"],
                    # self.type_stats["members"]["bitfields_merged"],
                # )
            # )

        print("=" * 72)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def select_mapping_file():
    return ida_kernwin.ask_file(
        False,
        "*.*",
        "Select IDAMappings file",
    )


# def ask_import_type_info(struct_count, enum_count):
    # if struct_count == 0 and enum_count == 0:
        # return False

    # answer = ida_kernwin.ask_yn(
        # 1,
        # "Also import {} struct(s) and {} enum(s) as Local Types?\n\n"
        # "This creates/replaces Local Types by name and can take a while on "
        # "large mappings. Bitfields are approximated (the format only "
        # "records their starting bit, not their width) and merged into a "
        # "single commented member per byte/word/dword/qword.".format(
            # struct_count,
            # enum_count,
        # ),
    # )

    # return answer == 1


def main(path=None):
    if path is None:
        # Permit invocation with a path while still working normally as an
        # interactive IDA script.
        if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
            path = sys.argv[1]
        else:
            path = select_mapping_file()

    if not path:
        print("[*] No mapping file selected.")
        return

    try:
        parser = MappingParser(path)
        parser.parse()

        # Struct/enum type import is disabled for now - see the
        # commented-out TypeImporter class and ask_import_type_info() above.
        import_type_info = False

        IDAImporter(parser).run(import_type_info)

    except (IOError, OSError, ValueError) as exc:
        print("[!] Import failed: {}".format(exc))
        raise


if __name__ == "__main__":
    main()
