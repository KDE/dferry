#!/usr/bin/python3

from __future__ import annotations
from dataclasses import dataclass
from typing import List, Tuple, Dict, Optional, Any, Union
from enum import IntEnum
import sys
import copy


# ============== Enums & Constants ==============

class IoState(IntEnum):
    NOT_STARTED = 0
    FINISHED = 1
    NEED_MORE_DATA = 2
    INVALID_DATA = 3
    ANY_DATA = 4
    DICT_KEY = 5
    BEGIN_ARRAY = 6
    END_ARRAY = 7
    BEGIN_DICT = 8
    END_DICT = 9
    BEGIN_STRUCT = 10
    END_STRUCT = 11
    BEGIN_VARIANT = 12
    END_VARIANT = 13
    BOOLEAN = 14
    BYTE = 15
    INT16 = 16
    UINT16 = 17
    INT32 = 18
    UINT32 = 19
    INT64 = 20
    UINT64 = 21
    DOUBLE = 22
    STRING = 23
    OBJECT_PATH = 24
    SIGNATURE = 25
    UNIX_FD = 26
    LAST_STATE = 27


class FerOpcode(IntEnum):
    COPY0 = 0
    COPY1 = 1
    COPY2 = 2
    COPY4 = 3
    COPY8 = 4
    STRING = 5
    OBJECT_PATH = 6
    SIGNATURE = 7
    BEGIN_ARRAY = 8
    END_ARRAY = 9
    ENTER_VARIANT = 10
    BEGIN_VARIANT_SIGNATURE = 11
    END_VARIANT_SIGNATURE = 12  # maps to EndVariant in C++ (intentionally)
    BEGIN_METHOD_SIGNATURE = 13
    END = 14


# ============== Data Classes ==============

@dataclass
class FerOp:
    post_align_exponent: int   # 0..3 → align 1, 2, 4, 8
    opcode: FerOpcode
    io_state: IoState


@dataclass
class FerRepeatArray:
    go_back_align_exponent: int   # for loop back
    go_back_op_index: int         # index of BeginArray


@dataclass
class FerNesting:
    array_depth: int
    paren_depth: int


# Type for heterogeneous bytecode list
FerCodeItem = Union[FerOp, FerRepeatArray, FerNesting]


# ============== Nesting Tracking ==============

class NestingWithMax:
    array_max = 32
    paren_max = 32
    total_max = 64

    def __init__(self):
        self.array = 0
        self.paren = 0
        self.variant = 0

        self.max_array = 0
        self.max_paren = 0
        self.max_combined = 0

    def begin_array(self) -> bool:
        self.array += 1
        self.max_array = max(self.max_array, self.array)
        self.max_combined = max(self.max_combined, self.total())
        return self.array <= self.array_max and self.total() <= self.total_max

    def end_array(self):
        assert self.array >= 1, "Unbalanced array nesting"
        self.array -= 1

    def begin_paren(self) -> bool:
        self.paren += 1
        self.max_paren = max(self.max_paren, self.paren)
        self.max_combined = max(self.max_combined, self.total())
        return self.paren <= self.paren_max and self.total() <= self.total_max

    def end_paren(self):
        assert self.paren >= 1, "Unbalanced paren nesting"
        self.paren -= 1

    def begin_variant(self) -> bool:
        self.variant += 1
        self.max_combined = max(self.max_combined, self.total())
        return self.total() <= self.total_max

    def end_variant(self):
        assert self.variant >= 1, "Unbalanced variant nesting"
        self.variant -= 1

    def total(self) -> int:
        return self.array + self.paren + self.variant


# ============== Core Encoding ==============

def fer_encode_signature(signature: str, sig_type: str = "method") -> Optional[List[FerCodeItem]]:
    """
    Encode DBus signature into FerCode IR list.
    `sig_type` can be `"method"` or `"variant"`.
    Returns None on invalid signature.
    """
    nest = NestingWithMax()
    out: List[FerCodeItem] = []

    if not signature or len(signature) > 255:
        return None

    if sig_type == "variant":
        # BeginVariantSignature header: 3 items: FerOp + 2x FerNesting
        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.BEGIN_VARIANT_SIGNATURE,
                         io_state=IoState.NOT_STARTED))
        out.append(FerNesting(array_depth=0, paren_depth=0))
        out.append(FerNesting(array_depth=0, paren_depth=0))

        # Encode one complete type
        if not fer_encode_single_complete_type(signature, 0, nest, out):
            return None

        if len(signature) != 0:
            return None  # signature must be fully consumed

        # Patch nesting depth fields
        out[1].array_depth = nest.max_array
        out[1].paren_depth = nest.max_paren
        out[2].array_depth = nest.max_combined  # really combined depth

        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.END_VARIANT_SIGNATURE,
                         io_state=IoState.END_VARIANT))

    else:  # method signature
        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.BEGIN_METHOD_SIGNATURE,
                         io_state=IoState.NOT_STARTED))

        i = 0
        while i < len(signature):
            success, new_i = fer_encode_single_complete_type(signature, i, nest, out)
            if not success:
                return None
            i = new_i

        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.END, io_state=IoState.FINISHED))

    assert nest.array == 0
    assert nest.paren == 0
    assert nest.variant == 0

    return out


# ============== Recursive Type Parser ==============

def fer_encode_single_complete_type(signature: str, pos: int,
                                    nest: NestingWithMax,
                                    out: List[FerCodeItem]) -> Tuple[bool, int]:
    """
    Parse one complete type at position `pos`, append to `out`.
    Returns (success, new_pos).
    """
    if pos >= len(signature):
        return False, pos

    ch = signature[pos]

    # —— Basic types —————————————————————————————————————————————————————
    if ch in "ybnqiuxtdh":  # numeric & boolean + handle (fd)
        ty = type_info(ch)
        align = ty["alignment"]
        opcode = FerOpcode.COPY1
        align_exp = 0
        if align == 2:
            opcode = FerOpcode.COPY2
            align_exp = 1
        elif align == 4:
            opcode = FerOpcode.COPY4
            align_exp = 2
        elif align == 8:
            opcode = FerOpcode.COPY8
            align_exp = 3

        out.append(FerOp(post_align_exponent=align_exp, opcode=opcode, io_state=ty["state"]))
        return True, pos + 1

    # —— String ———————————————————————————————————————————————————————
    elif ch == 's':
        out.append(FerOp(post_align_exponent=2, opcode=FerOpcode.STRING, io_state=IoState.STRING))
        return True, pos + 1

    # —— Object path ———————————————————————————————————————————————————
    elif ch == 'o':
        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.OBJECT_PATH, io_state=IoState.OBJECT_PATH))
        return True, pos + 1

    # —— Signature ————————————————————————————————————————————————————
    elif ch == 'g':
        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.SIGNATURE, io_state=IoState.SIGNATURE))
        return True, pos + 1

    # —— Variant ———————————————————————————————————————————————————————
    elif ch == 'v':
        if not nest.begin_variant():
            return False, pos
        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.ENTER_VARIANT, io_state=IoState.BEGIN_VARIANT))
        out.append(FerNesting(array_depth=nest.array, paren_depth=nest.paren))
        # Consume the variant body (one complete type)
        success, new_pos = fer_encode_single_complete_type(signature, pos + 1, nest, out)
        if not success:
            return False, pos
        nest.end_variant()
        return True, new_pos

    # —— Struct (paren) ———————————————————————————————————————————————
    elif ch == '(':
        if not nest.begin_paren():
            return False, pos

        out.append(FerOp(post_align_exponent=3, opcode=FerOpcode.COPY0, io_state=IoState.BEGIN_STRUCT))

        # Parse zero or more elements
        i = pos + 1
        elements = 0
        while i < len(signature) and signature[i] != ')':
            success, i = fer_encode_single_complete_type(signature, i, nest, out)
            if not success:
                return False, pos
            elements += 1

        if i >= len(signature) or signature[i] != ')' or elements == 0:
            return False, pos

        out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.COPY0, io_state=IoState.END_STRUCT))

        nest.end_paren()
        return True, i + 1

    # —— Array ————————————————————————————————————————————————————————
    elif ch == 'a':
        if not nest.begin_array():
            return False, pos

        go_back_index = len(out) + 1  # index after BeginArray

        i = pos + 1
        # Dict entry?
        if i < len(signature) and signature[i] == '{':
            # Enter dict
            if not nest.begin_paren() or len(signature) - i < 3:
                return False, pos
            i += 1

            # BeginArray for dict
            out.append(FerOp(post_align_exponent=2, opcode=FerOpcode.BEGIN_ARRAY, io_state=IoState.BEGIN_DICT))

            # Parse key (must be basic type)
            success, i = fer_encode_basic_type(signature, i, nest, out)
            if not success:
                return False, pos

            # Patch: key must be 8-aligned
            out[-1].post_align_exponent = 3

            # Parse value (any single complete type)
            success, i = fer_encode_single_complete_type(signature, i, nest, out)
            if not success:
                return False, pos

            if i >= len(signature) or signature[i] != '}':
                return False, pos
            i += 1
            nest.end_paren()

            # EndArray for dict
            out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.END_ARRAY, io_state=IoState.END_DICT))
        else:
            # Regular array
            out.append(FerOp(post_align_exponent=2, opcode=FerOpcode.BEGIN_ARRAY, io_state=IoState.BEGIN_ARRAY))

            success, i = fer_encode_single_complete_type(signature, i, nest, out)
            if not success:
                return False, pos

            out.append(FerOp(post_align_exponent=0, opcode=FerOpcode.END_ARRAY, io_state=IoState.END_ARRAY))

        nest.end_array()

        # Go-back annotation
        out.append(FerRepeatArray(go_back_align_exponent=0, go_back_op_index=go_back_index))

        return True, i

    # —— Unknown ———————————————————————————————————————————————————————
    return False, pos


def fer_encode_basic_type(signature: str, pos: int,
                          nest: NestingWithMax,
                          out: List[FerCodeItem]) -> Tuple[bool, int]:
    """Encode a 'basic' type (string, numeric, objectpath, etc.)"""
    if pos >= len(signature):
        return False, pos
    ch = signature[pos]
    if ch in "ybnqiuxtdhsog":
        return fer_encode_single_complete_type(signature, pos, nest, out)
    return False, pos


# ============== TypeInfo Mapping ==============

def type_info(ch: str) -> Dict[str, Any]:
    """
    DBus type → alignment, io_state, etc.
    """
    table = {
        'y': {"alignment": 1, "state": IoState.BYTE},
        'b': {"alignment": 4, "state": IoState.BOOLEAN},   # DBus bool is uint32
        'n': {"alignment": 2, "state": IoState.INT16},
        'q': {"alignment": 2, "state": IoState.UINT16},
        'i': {"alignment": 4, "state": IoState.INT32},
        'u': {"alignment": 4, "state": IoState.UINT32},
        'x': {"alignment": 8, "state": IoState.INT64},
        't': {"alignment": 8, "state": IoState.UINT64},
        'd': {"alignment": 8, "state": IoState.DOUBLE},
        'h': {"alignment": 4, "state": IoState.UNIX_FD},   # file descriptor is uint32
        's': {"alignment": 4, "state": IoState.STRING},
        'o': {"alignment": 4, "state": IoState.OBJECT_PATH},
        'g': {"alignment": 1, "state": IoState.SIGNATURE},  # signature is byte string
    }
    return table[ch]


# ============== Optimization Passes ==============

def apply_alignment(addr_set: int, align_exp: int) -> int:
    """Simulate alignment (C++ `applyAlignment`).
    `addr_set`: bitmask of possible addresses 1..8 → bit n-1 set if address n possible.
    """
    if align_exp == 0:
        return addr_set
    elif align_exp == 1:  # align 2
        addr_set |= addr_set << 1
        return addr_set & 0b10101010
    elif align_exp == 2:  # align 4
        addr_set |= addr_set << 1
        addr_set |= addr_set << 2
        return addr_set & 0b10001000
    elif align_exp == 3:  # align 8
        return 0b10000000
    else:
        raise ValueError("Invalid alignment exponent")


def apply_addition(addr_set: int, add_opcode: FerOpcode) -> int:
    """Simulate data addition (C++ `applyAddition`).
    Rotates address set left by `addend`.
    """
    if add_opcode == FerOpcode.COPY1:
        addend = 1
    elif add_opcode == FerOpcode.COPY2:
        addend = 2
    elif add_opcode == FerOpcode.COPY4:
        addend = 4
    elif add_opcode in (FerOpcode.COPY0, FerOpcode.COPY8):
        return addr_set  # no change for 8-alignment
    else:
        raise ValueError("Invalid addition opcode")
    return apply_addition_direct(addr_set, addend)

def apply_addition_direct(addr_set: int, addend: int) -> int:
    # rotate left (with 8-bit wrap)
    addr_set = addr_set << addend;
    addr_set |= addr_set >> 8;
    addr_set &= 0b11111111;

    return addr_set


def is_basic_addition_op(opcode: FerOpcode) -> bool:
    return opcode in (FerOpcode.COPY1, FerOpcode.COPY2, FerOpcode.COPY4, FerOpcode.COPY8)


def is_var_length_op(opcode: FerOpcode) -> bool:
    return opcode in (FerOpcode.STRING, FerOpcode.OBJECT_PATH, FerOpcode.SIGNATURE, FerOpcode.ENTER_VARIANT)

def optimize_fer_ops(ops: List[FerCodeItem]) -> Dict[int, Dict[str, int]]:
    """
    Main optimizer: align merging, array loopback tuning, variant/struct/variant nesting tracking.
    """
    # key: BeginArray index → {"before": addr_set, "after": addr_set}
    array_alignments: Dict[int, Dict[str, int]] = {}
    begin_array_indexes: List[int] = []

    is_variant = ops[0].opcode == FerOpcode.BEGIN_VARIANT_SIGNATURE
    any_addr_set = 0b11111111  # all 8 alignments possible

    addr_set = any_addr_set if is_variant else 0b10000000  # 8-aligned / fully aligned

    prev_align_index = 0
    prev_io_state_index = 0

    i = 3 if is_variant else 1

    while i < len(ops):
        prev_addr_set = addr_set

        fer_op = ops[i]
        assert isinstance(fer_op, FerOp)

        # Apply alignment BEFORE opcode
        addr_set = apply_alignment(addr_set, fer_op.post_align_exponent)

        # Merge alignment into previous alignment-defining op if no data op intervened
        if addr_set != prev_addr_set:
            ops[prev_align_index].post_align_exponent = fer_op.post_align_exponent

        # Move io_state into previous opcode
        ops[prev_io_state_index].io_state = fer_op.io_state

        # Zero out in current op
        fer_op.post_align_exponent = 0
        fer_op.io_state = IoState.INVALID_DATA

        # Payload update (alignment + data address)
        if is_basic_addition_op(fer_op.opcode):
            addr_set = apply_addition(addr_set, fer_op.opcode)

        # Update tracking indices if opcode does data
        if fer_op.opcode != FerOpcode.COPY0:
            prev_align_index = i
        prev_io_state_index = i

        if fer_op.opcode == FerOpcode.BEGIN_ARRAY:
            # Process array: pre-compute alignment info
            addr_set = apply_addition(addr_set, FerOpcode.COPY4)  # array length field

            if i not in array_alignments:
                optimize_arrays(ops, addr_set, i, array_alignments)

            arr_align = array_alignments[i]
            addr_set = arr_align["before"]

            begin_array_indexes.append(i)

        elif fer_op.opcode == FerOpcode.END_ARRAY:
            begin_arr_idx = begin_array_indexes.pop()
            assert ops[begin_arr_idx].opcode == FerOpcode.BEGIN_ARRAY

            arr_align = array_alignments[begin_arr_idx]

            go_back_idx = begin_arr_idx + 1

            # Can we skip alignment on loopback?
            loop_back_align = ops[begin_arr_idx].post_align_exponent
            if loop_back_align:
                after_contents_aligned = apply_alignment(arr_align["after"], loop_back_align)
                if after_contents_aligned == arr_align["after"]:
                    loop_back_align = 0

            addr_set = arr_align["after"]

            i += 1
            ops[i] = FerRepeatArray(go_back_align_exponent=loop_back_align, go_back_op_index=go_back_idx)

        else:
            if is_var_length_op(fer_op.opcode):
                addr_set = any_addr_set

        i += 1

    return array_alignments


def optimize_arrays(ops: List[FerCodeItem], addr_set: int,
                    begin_array_index: int,
                    array_alignments: Dict[int, Dict[str, int]]) -> int:
    """Pre-pass to collect possible alignment states for array elements."""
    assert ops[begin_array_index].opcode == FerOpcode.BEGIN_ARRAY

    if begin_array_index not in array_alignments:
        array_alignments[begin_array_index] = {"before": addr_set, "after": 0}
    else:
        array_alignments[begin_array_index]["before"] |= addr_set

    i = begin_array_index + 1
    while i < len(ops):
        op = ops[i]
        assert isinstance(op, FerOp)

        addr_set = apply_alignment(addr_set, op.post_align_exponent)

        if is_basic_addition_op(op.opcode):
            addr_set = apply_addition(addr_set, op.opcode)

        elif is_var_length_op(op.opcode):
            addr_set = 0b11111111

        elif op.opcode == FerOpcode.BEGIN_ARRAY:
            # Nested array
            addr_set = apply_addition(addr_set, FerOpcode.COPY4)
            inner_begin_array_index = i

            i = optimize_arrays(ops, addr_set, i, array_alignments)
            assert ops[i].opcode == FerOpcode.END_ARRAY
            i += 1 # skip FerEndArray

            # After inner array
            inner_align = array_alignments[inner_begin_array_index]
            addr_set = inner_align["after"]

        elif op.opcode == FerOpcode.END_ARRAY:
            no_more_addr_set_changes = False

            arr_data = array_alignments[begin_array_index]
            if arr_data["after"] != 0:
                # We've seen this array before
                addr_set |= arr_data["after"]
                no_more_addr_set_changes = arr_data["after"] == addr_set
            else:
                # First pass: initial alignment = after alignment?
                no_more_addr_set_changes = arr_data["before"] == addr_set

            arr_data["after"] = addr_set

            if (no_more_addr_set_changes):
                return i
            else:
                i = begin_array_index # note, the loop will do i += 1 - that is intended
        i += 1

    return i


def elide_structs(ops: List[FerCodeItem]) -> None:
    """
    Remove BEGIN/END_STRUCT from output, keep alignment effect only.
    *** WARNING: this clobbers goBackOpIndex of arrays! ***
    Must be called *before* `optimize_fer_ops`.
    """
    is_variant = ops[0].opcode == FerOpcode.BEGIN_VARIANT_SIGNATURE
    alignment_from_preceding_struct = False

    out_index = 3 if is_variant else 1
    i = out_index

    while i < len(ops):
        assert out_index <= i

        op = ops[i]
        assert isinstance(op, FerOp)

        if op.io_state == IoState.BEGIN_STRUCT:
            # Drop this, but remember alignment needed for next element
            alignment_from_preceding_struct = True
        elif op.io_state == IoState.END_STRUCT:
            # This one has no effect at all except requiring an endStruct() call - drop it entirely
            pass
        else:
            ops[out_index] = op
            if alignment_from_preceding_struct:
                alignment_from_preceding_struct = False
                ops[out_index].post_align_exponent = 3

            out_index += 1

            # Skip associated FerRepeatArray or FerNesting for these
            if op.opcode in (FerOpcode.END_ARRAY, FerOpcode.ENTER_VARIANT):
                # Next element is not a FerOp, copy and ignore
                i += 1
                ops[out_index] = ops[i]
                out_index += 1

        i += 1

    del ops[out_index:]
