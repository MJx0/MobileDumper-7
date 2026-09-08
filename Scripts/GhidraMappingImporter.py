# -*- coding: utf-8 -*-
"""
IDAMappings importer for Ghidra.

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


# from ghidra.program.model.data import (
    # ArrayDataType,
    # BooleanDataType,
    # ByteDataType,
    # CategoryPath,
    # CharDataType,
    # DataTypeConflictHandler,
    # DoubleDataType,
    # DWordDataType,
    # EnumDataType,
    # FloatDataType,
    # PointerDataType,
    # QWordDataType,
    # StructureDataType,
    # WordDataType,
# )


# ============================================================================
# Format definitions
# ============================================================================

class Format(object):
    """Constants defined by the on-disk IDAMappings format."""

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


# ============================================================================
# Binary reader
# ============================================================================

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

    def u64(self, offset):
        self._require(offset, 8)
        return struct.unpack_from("<Q", self._data, offset)[0]

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


# ============================================================================
# Mapping parser
# ============================================================================

class MappingParser(object):
    """
    Parser for the portions of IDAMappings used by this importer.

    This class has no dependency on Ghidra.
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
            raise ValueError(
                "file is smaller than IDAMappingsHeader"
            )

        self.magic = reader.u8(
            Format.HEADER_MAGIC
        )

        self.version = reader.u8(
            Format.HEADER_VERSION
        )

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
        Equivalent to the original MappingParser::GetNameFromOffset().

        StringOffset is relative to StringDataOffset and points to:

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

        if not reader.can_read(
            data_start,
            string_length,
        ):
            return ""

        raw = reader.bytes(
            data_start,
            string_length,
        )

        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode(
                "utf-8",
                "replace",
            )

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
                "cpp_signature": self.get_string(
                    cpp_signature_offset
                ),
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


# ============================================================================
# Type helpers (no Ghidra dependency - shared logic, unit-testable)
# ============================================================================

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


# def byte_type_for_size(size):
    # """Best-effort unsigned integer DataType for a given byte size."""

    # if size >= 8:
        # return QWordDataType.dataType
    # if size >= 4:
        # return DWordDataType.dataType
    # if size >= 2:
        # return WordDataType.dataType
    # return ByteDataType.dataType


# ============================================================================
# Ghidra type creation
#
# Uses ghidra.program.model.data (StructureDataType / EnumDataType /
# DataTypeManager), which has been stable across Ghidra releases. This code
# could not be exercised against a real Ghidra instance while writing it -
# every type-creation call is wrapped so a single unexpected failure only
# skips that one type/member instead of aborting the whole import. Please
# report anything that misbehaves.
#
# Disabled for now - not needed yet. Uncomment this class, the
# ghidra.program.model.data import block above, get_enums()/get_structs()/
# ask_import_type_info(), and the import_type_info wiring in main()/run(),
# to bring it back.
# ============================================================================

# TYPE_CATEGORY = CategoryPath("/IDAMappings")


# class TypeImporter(object):

    # def __init__(self, parser, program):
        # self.parser = parser
        # self.program = program
        # self.dtm = program.getDataTypeManager()

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

    # -- lookups -------------------------------------------------------------

    # def find_named_type(self, name):
        # try:
            # return self.dtm.getDataType(TYPE_CATEGORY, name)
        # except Exception:
            # return None

    # def get_primitive_type(self, type_name):
        # try:
            # if type_name == "bool":
                # return BooleanDataType.dataType
            # if type_name == "char":
                # return CharDataType.dataType
            # if type_name == "wchar_t":
                # return WordDataType.dataType
            # if type_name == "float":
                # return FloatDataType.dataType
            # if type_name == "double":
                # return DoubleDataType.dataType
            # if type_name in PRIMITIVE_SIZES:
                # return byte_type_for_size(PRIMITIVE_SIZES[type_name])
        # except Exception:
            # pass

        # return None

    # def resolve_member_type(self, type_name, is_pointer, array_dim, fallback_size):
        # base_name = strip_type_prefix(type_name)

        # data_type = None

        # if type_name in PRIMITIVE_SIZES:
            # data_type = self.get_primitive_type(type_name)
        # elif base_name in self.known_enum_names:
            # data_type = self.find_named_type(base_name)
        # elif base_name in self.known_struct_names:
            # data_type = self.find_named_type(base_name)

        # if data_type is None:
            # Unknown/unsupported type - fall back to an opaque byte blob of
            # the reported size so the struct's overall layout still lines
            # up, rather than dropping the member (and everything after it)
            # entirely.
            # if fallback_size and fallback_size > 1:
                # return ArrayDataType(
                    # ByteDataType.dataType,
                    # fallback_size,
                    # 1,
                # )

            # return ByteDataType.dataType

        # if is_pointer:
            # data_type = PointerDataType(data_type)

        # if array_dim and array_dim > 1:
            # data_type = ArrayDataType(
                # data_type,
                # array_dim,
                # data_type.getLength(),
            # )

        # return data_type

    # -- enums -----------------------------------------------------------------

    # def create_enum(self, enum_info):
        # name = sanitize_identifier(enum_info["name"], None)

        # if not name:
            # self.stats["enums"]["failed"] += 1
            # return False

        # try:
            # size = enum_info["underlying_size"] or 4

            # if size not in (1, 2, 4, 8):
                # size = 4

            # enum_dt = EnumDataType(TYPE_CATEGORY, name, size)
            # seen_names = set()

            # for value_info in enum_info["values"]:
                # value_name = sanitize_identifier(value_info["name"], None)

                # if not value_name or value_name in seen_names:
                    # continue

                # seen_names.add(value_name)

                # mask = (1 << (size * 8)) - 1
                # enum_dt.add(value_name, value_info["value"] & mask)

            # self.dtm.addDataType(
                # enum_dt,
                # DataTypeConflictHandler.REPLACE_HANDLER,
            # )

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

    # -- structs ---------------------------------------------------------------

    # def create_struct_shell(self, name):
        # try:
            # struct_dt = StructureDataType(TYPE_CATEGORY, name, 0)

            # self.dtm.addDataType(
                # struct_dt,
                # DataTypeConflictHandler.REPLACE_HANDLER,
            # )

            # return True

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
            # struct_dt = self.find_named_type(name)

            # if struct_dt is None:
                # raise ValueError("shell type not found")

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

                    # member_dt = byte_type_for_size(member["size"])

                    # bit_desc = ", ".join(
                        # "{}(bit {}, ~{} wide)".format(
                            # m["name"] or "?",
                            # m["bitfield_bit"],
                            # bitfield_widths.get(id(m), 1),
                        # )
                        # for m in group
                    # )

                    # field_name = unique_name(
                        # sanitize_identifier(
                            # (group[0]["name"] or "bitfield") + "_bits",
                            # "bitfield_{:X}".format(member["offset"]),
                        # )
                    # )

                    # struct_dt.insertAtOffset(
                        # member["offset"],
                        # member_dt,
                        # member_dt.getLength(),
                        # field_name,
                        # "Merged bitfield (widths are inferred, not exact): "
                        # + bit_desc,
                    # )

                    # applied += 1
                    # merged += len(group) - 1
                    # continue

                # member_dt = self.resolve_member_type(
                    # member["type"],
                    # member["is_pointer"],
                    # member["array_dim"],
                    # member["size"],
                # )

                # if member_dt is None:
                    # skipped += 1
                    # continue

                # field_name = unique_name(
                    # sanitize_identifier(
                        # member["name"],
                        # "field_{:X}".format(member["offset"]),
                    # )
                # )

                # struct_dt.insertAtOffset(
                    # member["offset"],
                    # member_dt,
                    # member_dt.getLength(),
                    # field_name,
                    # None,
                # )

                # applied += 1

            # Best-effort: make sure the structure's overall size matches
            # what was reflected, in case trailing bytes were never covered
            # by a member (eg. all-skipped tail, or reserved padding).
            # try:
                # if struct_info["size"] and struct_dt.getLength() < struct_info["size"]:
                    # struct_dt.growStructure(
                        # struct_info["size"] - struct_dt.getLength()
                    # )
            # except Exception:
                # pass

            # self.dtm.addDataType(
                # struct_dt,
                # DataTypeConflictHandler.REPLACE_HANDLER,
            # )

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


# ============================================================================
# Ghidra importer
# ============================================================================

class GhidraImporter(object):

    def __init__(self, parser):
        self.parser = parser

        self.program = currentProgram
        self.memory = self.program.getMemory()
        self.symbol_table = self.program.getSymbolTable()
        self.reference_manager = (
            self.program.getReferenceManager()
        )

        self.image_base = (
            self.program.getImageBase().getOffset()
        )

        # Determine pointer size from the current Ghidra program.
        self.pointer_size = (
            self.program.getDefaultPointerSize()
        )

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

    def to_address(self, relative_offset):
        return self.program.getAddressFactory().getDefaultAddressSpace().getAddress(
            self.image_base + relative_offset
        )

    def get_address(self, value):
        try:
            return self.program.getAddressFactory().getDefaultAddressSpace().getAddress(
                value
            )
        except Exception:
            return None

    def rename(self, address, name, stats):
        """
        Rename an address using Ghidra's symbol table.

        Passing the stats object directly avoids category-string mismatches.
        """

        if address is None or name is None or not name:
            stats["failed"] += 1
            return False

        try:
            old_symbol = (
                self.symbol_table.getPrimarySymbol(address)
            )

            old_name = (
                old_symbol.getName()
                if old_symbol is not None
                else None
            )

            if old_name == name:
                stats["renamed"] += 1
                return True

            # Remove the existing primary symbol if there is one.
            if old_symbol is not None:
                try:
                    self.symbol_table.removeSymbol(
                        old_symbol
                    )
                except Exception:
                    pass

            self.symbol_table.createLabel(
                address,
                name,
                SourceType.USER_DEFINED,
            )

            print(
                "[+] 0x{:X}: {!r} -> {!r}".format(
                    address.getOffset(),
                    old_name,
                    name,
                )
            )

            stats["renamed"] += 1
            return True

        except Exception as exc:
            print(
                "[!] Could not rename 0x{:X} to {!r}: {}".format(
                    address.getOffset(),
                    name,
                    exc,
                )
            )

            stats["failed"] += 1
            return False

    def ensure_function(self, address):
        """
        Ensure Ghidra has a function at the supplied address.
        """

        if address is None:
            return False

        try:
            existing = getFunctionAt(address)

            if existing is not None:
                return True

            createFunction(
                address,
                None,
            )

            return getFunctionAt(address) is not None

        except Exception as exc:
            print(
                "[!] Could not create function at 0x{:X}: {}".format(
                    address.getOffset(),
                    exc,
                )
            )
            return False

    def read_pointer(self, address):
        """
        Read a pointer-sized value from Ghidra memory.

        The mapping/importer is intended primarily for 64-bit binaries,
        but the current program's pointer size is respected.
        """

        try:
            if self.pointer_size == 8:
                return self.memory.getLong(
                    address
                ) & 0xFFFFFFFFFFFFFFFF

            if self.pointer_size == 4:
                return self.memory.getInt(
                    address
                ) & 0xFFFFFFFF

            if self.pointer_size == 2:
                return self.memory.getShort(
                    address
                ) & 0xFFFF

        except Exception:
            return None

        return None

    def is_data_reference(self, reference):
        """
        Determine whether a reference originates from data rather than
        an instruction.

        Ghidra references have a source address. We inspect the code unit
        at that address and reject instructions.
        """

        try:
            source = reference.getFromAddress()
            listing = self.program.getListing()
            code_unit = listing.getCodeUnitAt(source)

            if code_unit is None:
                return False

            # Instructions are CodeUnits but have a FlowType and can be
            # identified through the instruction API.
            instruction = listing.getInstructionAt(source)

            if instruction is not None:
                return False

            return True

        except Exception:
            return False

    def rename_global_pointer_refs(self, address, name):
        """
        Find data locations that contain a direct pointer to `address`.

        Rename them:

            <name>_ptr_0
            <name>_ptr_1
            ...

        Only data references are considered.

        Pointer indices are assigned in ascending address order.
        """

        if address is None or not name:
            return

        refs = []

        try:
            iterator = (
                self.reference_manager.getReferencesTo(address)
            )

            for reference in iterator:
                if reference is None:
                    continue

                # Only references that originate from data are relevant.
                if not self.is_data_reference(reference):
                    continue

                ref_address = reference.getFromAddress()

                if ref_address is None:
                    continue

                # Verify that the actual pointer-sized value stored at
                # the reference location equals the global address.
                value = self.read_pointer(ref_address)

                if value is None:
                    continue

                if value != address.getOffset():
                    continue

                refs.append(ref_address)

        except Exception as exc:
            print(
                "[!] Could not enumerate references to {}: {}".format(
                    name,
                    exc,
                )
            )
            return

        # Deterministic ordering.
        refs.sort(
            key=lambda x: x.getOffset()
        )

        for index, ref_address in enumerate(refs):
            pointer_name = "{}_ptr_{}".format(
                name,
                index,
            )

            stats = self.stats["globals"]

            try:
                old_symbol = (
                    self.symbol_table.getPrimarySymbol(
                        ref_address
                    )
                )

                old_name = (
                    old_symbol.getName()
                    if old_symbol is not None
                    else None
                )

                if old_name == pointer_name:
                    stats["pointers"] += 1
                    continue

                if old_symbol is not None:
                    try:
                        self.symbol_table.removeSymbol(
                            old_symbol
                        )
                    except Exception:
                        pass

                self.symbol_table.createLabel(
                    ref_address,
                    pointer_name,
                    SourceType.USER_DEFINED,
                )

                print(
                    "[+] Global pointer 0x{:X}: "
                    "{!r} -> {!r}".format(
                        ref_address.getOffset(),
                        old_name,
                        pointer_name,
                    )
                )

                stats["pointers"] += 1

            except Exception as exc:
                print(
                    "[!] Could not rename global pointer "
                    "0x{:X} to {!r}: {}".format(
                        ref_address.getOffset(),
                        pointer_name,
                        exc,
                    )
                )

    def import_globals(self):
        stats = self.stats["globals"]

        for entry in self.parser.get_globals():
            address = self.to_address(
                entry["offset"]
            )

            name = entry["name"]

            self.rename(
                address,
                name,
                stats,
            )

            # Perform the pointer-reference pass even if the symbol was
            # already named correctly.
            if name:
                self.rename_global_pointer_refs(
                    address,
                    name,
                )

    def import_functions(self):
        stats = self.stats["functions"]

        for entry in self.parser.get_functions():
            address = self.to_address(
                entry["offset"]
            )

            self.ensure_function(
                address
            )

            # Prefer the unmangled/demangled representation.
            name = (
                entry["unmangled"] or
                entry["mangled"]
            )

            self.rename(
                address,
                name,
                stats,
            )

    def import_vtables(self):
        stats = self.stats["vtables"]

        for entry in self.parser.get_vtables():
            address = self.to_address(
                entry["offset"]
            )

            self.rename(
                address,
                entry["name"],
                stats,
            )

    # def import_types(self):
        # self.type_stats = TypeImporter(self.parser, self.program).run()

    def run(self, import_type_info):
        print("")
        print("=" * 72)
        print("IDAMappings importer - Ghidra")
        print("=" * 72)

        print(
            "Mapping:    {}".format(
                self.parser.path
            )
        )

        print(
            "Image base: 0x{:X}".format(
                self.image_base
            )
        )

        print(
            "Pointer size: {} bytes".format(
                self.pointer_size
            )
        )

        print(
            "Version:    {}".format(
                self.parser.version
            )
        )

        print(
            "Globals:    {}".format(
                self.parser.global_count
            )
        )

        print(
            "Functions:  {}".format(
                self.parser.function_count
            )
        )

        print(
            "VTables:    {}".format(
                self.parser.vtable_count
            )
        )

        print(
            "Enums:      {}".format(
                self.parser.enum_count
            )
        )

        print(
            "Structs:    {}".format(
                self.parser.struct_count
            )
        )

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

        for category in (
            "globals",
            "functions",
            "vtables",
        ):
            stats = self.stats[category]

            line = (
                "{:<12} renamed={} failed={}".format(
                    category,
                    stats["renamed"],
                    stats["failed"],
                )
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


# ============================================================================
# File selection
# ============================================================================

def select_mapping_file():
    """
    Ask Ghidra for the IDAMappings file.

    Uses Ghidra's file chooser when available.
    """

    chooser = askFile(
        "Select IDAMappings file",
        "Select",
    )

    if chooser is None:
        return None

    return chooser.getAbsolutePath()


# def ask_import_type_info(struct_count, enum_count):
    # if struct_count == 0 and enum_count == 0:
        # return False

    # try:
        # return bool(
            # askYesNo(
                # "Import type information",
                # "Also import {} struct(s) and {} enum(s) as data types?\n\n"
                # "This creates/replaces data types by name in the "
                # "/IDAMappings category and can take a while on large "
                # "mappings. Bitfields are approximated (the format only "
                # "records their starting bit, not their width) and merged "
                # "into a single commented member per byte/word/dword/"
                # "qword.".format(
                    # struct_count,
                    # enum_count,
                # )
            # )
        # )
    # except Exception:
        # return False


# ============================================================================
# Entry point
# ============================================================================

def main(path=None):
    if path is None:
        path = select_mapping_file()

    if not path:
        print("[*] No mapping file selected.")
        return

    if not os.path.isfile(path):
        print(
            "[!] Mapping file does not exist: {}".format(
                path
            )
        )
        return

    try:
        parser = MappingParser(path)
        parser.parse()

        # Struct/enum type import is disabled for now - see the
        # commented-out TypeImporter class and ask_import_type_info() above.
        import_type_info = False

        GhidraImporter(parser).run(import_type_info)

    except (IOError, OSError, ValueError) as exc:
        print(
            "[!] Import failed: {}".format(
                exc
            )
        )
        raise


# ============================================================================
# Script entry point
# ============================================================================

main()
