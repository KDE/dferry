#!/usr/bin/python3

from __future__ import annotations
from dataclasses import dataclass
import os
import sys
from textwrap import indent

# Not using string.templatelib because we need exactly two things there:
# - replace variables (supported by templatelib)
# - insert lists at particular points (not directly supported by templatelib)
class TextTemplate:
    def __init__(self):
        self.pieces = []

    def render(self, vars: Dict[str, Any], insertions: Dict[str, Any]) -> str:
        def stringify(arg):
            if isinstance(arg, list):
                ret = ''
                for a in arg:
                    ret += str(a)
                return ret
            else:
                return str(arg)

        rendered = []
        for p in self.pieces:
            if p.startswith('_Tinsert_'):
                key = p[len('_Tinsert_'):]
                if key in insertions:
                    rendered.append(stringify(insertions[key]))
            else:
                for var_name, value in vars.items():
                    p = p.replace('_Tvar_' + var_name, stringify(value))
                rendered.append(p)

        return ''.join(rendered)


# This template system is single-purpose and expects the template processing code to
# have some knowledge about template text contents. That allows to keep it very simple.
# The return value is a dict:
# {template name, template contents}
# Template contents are a list of "nodes", where a node can be:
# - literal string; note, may contain "variables" (specific strings) to be replaced
# - insertion point, just a string starting with _Tinsert_
def parse_templates(file_name: str) -> Dict[str, TextTemplate]:
    tp_filename = os.path.dirname(os.path.realpath(__file__)) + '/' + file_name

    ret: Dict[str, TextTemplate] = {}
    tp_name = ''
    tp_name_stack = []

    tpf = open(tp_filename, 'rt')
    for line in tpf:
        sline = line.strip()
        if sline.endswith('// _Tignore_'):
            continue

        if sline.startswith('// _TsnipBegin_'):
            tp_name = sline[len('// _TsnipBegin_'):]
            assert tp_name, "Snippet name may not be empty"
            assert tp_name not in ret, "Redefining a snippet is not allowed"
            tp_name_stack.append(tp_name)
            ret[tp_name] = TextTemplate()
        elif sline.startswith('// _TsnipEnd_'):
            tp_end_name = sline[len('// _TsnipEnd_'):]
            assert tp_name == tp_end_name, "Not closing the last opened snippet"
            assert tp_name_stack[-1] == tp_end_name
            tp_name_stack.pop()
            tp_name = ''
            if tp_name_stack:
                tp_name = tp_name_stack[-1]

        elif sline.startswith('// _Tinsert_'):
            insertion_point_name = sline[len('// '):] # keep the _Tinsert_ part to mark the insertion point
            ret[tp_name].pieces.append(insertion_point_name)

        elif tp_name:
            if len(ret[tp_name].pieces) == 0 or ret[tp_name].pieces[-1].startswith('_Tinsert_'):
                # append *a new* plaintext string section
                ret[tp_name].pieces.append(line)
            else:
                # append *to an existing* plaintext string section
                ret[tp_name].pieces[-1] += line

    tpf.close()
    return ret

def c_primitive_type(ios: IoState) -> str:
    table = {
        IoState.BYTE: "byte",
        IoState.BOOLEAN: "uint32",  # That is in the spec. Valid values are 0 and 1.
        IoState.INT16: "int16",
        IoState.UINT16: "uint16",
        IoState.INT32: "int32",
        IoState.UINT32: "uint32",
        IoState.INT64: "int64",
        IoState.UINT64: "uint64",
        IoState.DOUBLE: "double",
        IoState.UNIX_FD: "uint32",  # It's a uint32 index into an array of int32s
    }
    return table[ios]

def reader_name(ios: IoState) -> str: # TODO rename a lot
    table = {
        IoState.BYTE: "Byte",
        IoState.BOOLEAN: "Boolean",
        IoState.INT16: "Int16",
        IoState.UINT16: "Uint16",
        IoState.INT32: "Int32",
        IoState.UINT32: "Uint32",
        IoState.INT64: "Int64",
        IoState.UINT64: "Uint64",
        IoState.DOUBLE: "Double",
        IoState.UNIX_FD: "UnixFd",
        IoState.STRING: "String",
        IoState.OBJECT_PATH: "ObjectPath",
        IoState.SIGNATURE: "Signature",
    }
    return table[ios]

def reader_name_for_opcode(opcode: FerOpcode) -> str: # TODO rename a lot
    table = {
        FerOpcode.STRING: "String",
        FerOpcode.OBJECT_PATH: "ObjectPath",
        FerOpcode.SIGNATURE: "Signature",
    }
    return table[opcode]

def alignment_padding_length(addr_bit_before: int, addr_bit_after: int) -> int:
    # Handling wraparound is not necessary here because applying alignment may only move bits in
    # the address bitmask to higher positions
    for i in range(8):
        if (addr_bit_before << i) == addr_bit_after:
            return i
    assert False

def fixed_op_part_length(opcode: FerOpcode) -> int:
    table = {
        FerOpcode.COPY0: 0,
        FerOpcode.COPY1: 1,
        FerOpcode.COPY2: 2,
        FerOpcode.COPY4: 4,
        FerOpcode.COPY8: 8,
        FerOpcode.BEGIN_ARRAY: 4,
        FerOpcode.ENTER_VARIANT: 1, # starts with a signature, which has a 1-byte length field
        FerOpcode.STRING: 4,
        FerOpcode.OBJECT_PATH: 1,
        FerOpcode.SIGNATURE: 1,
    }
    return table[opcode]

@dataclass
class SpanItem:
    # all lengths and positions include the alignment padding following the current item!
    min_pos_after: int
    max_pos_after: int
    min_length: int
    max_length: int
    is_last: bool

def calculate_single_span(ops: List[FerCodeItem], before_span_addr_set: int,
                          span_start_index: int, span_end_index: int,
                          ret: Dict[int, SpanItem]):

    assert before_span_addr_set != 0

    # For each starting alignment, calculate span length and alignment padding lengths
    for align_bit in range(8):
        addr_set = before_span_addr_set & (1 << align_bit) # well, set of 1 - we look at one at a time
        if addr_set == 0:
            continue # this alignment value does not occur

        span_length = 0

        for i in range(span_start_index, span_end_index + 1):
            prev_span_length = span_length

            fer_op = ops[i]
            assert isinstance(fer_op, FerOp)

            fixed_part_length = fixed_op_part_length(fer_op.opcode)
            addr_set = apply_addition_direct(addr_set, fixed_part_length)
            span_length += fixed_part_length

            unaligned_addr_set = addr_set

            addr_set = apply_alignment(addr_set, fer_op.post_align_exponent)
            pad_length = alignment_padding_length(unaligned_addr_set, addr_set)
            span_length += pad_length

            element_length = span_length - prev_span_length

            if not is_basic_addition_op(fer_op.opcode):
                assert i == span_end_index

            if fer_op.opcode == FerOpcode.BEGIN_ARRAY:
                # HACK: Because the padding length check after the aray length field is included for free
                # with the overall array length check, there is no point in eliminating it and so that
                # section doesn't figure in eliminating length check calculations.
                # But there is a potential padding whose size may be determined to be fixed - for that, the
                # *element* length *should* include padding.
                # So we make element length and span length incongruous.
                # Since a BEGIN_ARRAY is always the last field of a span, the size of the mess is limited.
                span_length -= pad_length

            # set span and element length min and max values in ret
            if i not in ret:
                ret[i] = SpanItem(span_length, span_length, element_length, element_length, False)
            else:
                ret[i].min_pos_after = min(ret[i].min_pos_after, span_length)
                ret[i].max_pos_after = max(ret[i].max_pos_after, span_length)

                ret[i].min_length = min(ret[i].min_length, element_length)
                ret[i].max_length = max(ret[i].max_length, element_length)

            # possible optimization: if our current addresses etc are all *in a relevant way* (tbd)
            # the same as something we've previously handled, stop calculation for this addr_set
            # because we are only going to repeat configurations we've done before

    ret[span_end_index].is_last = True


def has_length_field(fer_code: FerCodeItem) -> bool:
    return isinstance(fer_code, FerOp) and \
        fer_code.opcode in [FerOpcode.BEGIN_ARRAY, FerOpcode.ENTER_VARIANT,
                            FerOpcode.STRING, FerOpcode.OBJECT_PATH, FerOpcode.SIGNATURE]

# For spans of fixed-length types (including length fields of variable length types, can only occur at
# the end of a span), calculate the min and max span length after each element, as well as the min and
# max length *of* each element. Min and max may differ due to possibly varying alignment of the starting
# address of each span.
# Length always excludes alignment padding before the first element and always (with one exception, see
# "HACK" comment in calculate_single_span()) includes alignment padding after every element.
#
# The goal is to (only) gather information for the following optimizations, if applicable:
# - Simplify "align to n bytes" to "align by moving position by a fixed distance of m (m < n) bytes";
#   this eliminates some instructions and especially conditional branches from marshalling code
# - Instead of checking input buffer length before each element, check input buffer length only once
#   at the beginning of the span. If the length of the span is slightly variable (due to above mentioned
#   varying alignment at the start of the span, therefore varying alignment padding lengths), check for
#   the minimum length of the span at its beginning and fall back to checking before each type at the end,
#   where the minimum length will be exceeded for some span starting addresses.
#
# The result is: Dict[op_index, SpanItem]
def calculate_span_addrs(ops: List[FerCodeItem], array_alignments: Dict[int, ArrayAlignments]) \
                        -> Dict[int, SpanItem]:

    ret: Dict[int, SpanItem] = {}

    begin_array_indexes: List[int] = []

    is_variant = ops[0].opcode == FerOpcode.BEGIN_VARIANT_SIGNATURE
    any_addr_set = 0b11111111  # all 8 alignments possible

    addr_set = any_addr_set if is_variant else 0b10000000  # 8-aligned / fully aligned

    span_start_index = -1
    before_span_addr_set = 0 # invalid if span_start_index < 0

    i = 3 if is_variant else 1

    while i < len(ops):
        prev_addr_set = addr_set

        fer_op = ops[i]
        assert isinstance(fer_op, FerOp)

        is_basic_addition = is_basic_addition_op(fer_op.opcode)
        # TODO is_basic_addition_op(FerOpcode.COPY0) is false, does it make sense to also treat
        # it as start of span?!
        if is_basic_addition:
            if span_start_index < 0:
                # start a new span at the current fixed-length type
                span_start_index = i
                before_span_addr_set = prev_addr_set
            # else we've already started a span, just continue it until we reach its end

            addr_set = apply_addition(addr_set, fer_op.opcode)

        elif span_start_index < 0:
            # Start and end a span on the current element if it has a length field followed by alignment;
            # this at least allows to merge length field and length field post padding size checks and to
            # simplify fixed padding length. As it turns out, only an array can have padding after its
            # length field: strings contain one-byte items, so no alignment padding.
            if fer_op.post_align_exponent != 0 and fer_op.opcode == FerOpcode.BEGIN_ARRAY:
                span_start_index = i
                before_span_addr_set = prev_addr_set


        # A span ends at anything that isn't an (entirely) fixed length type
        if not is_basic_addition and span_start_index >= 0:
            # If the current element has a length field, include it in the span; the variable length
            # part only comes *after* that
            span_end_index = i if has_length_field(fer_op) else i - 1
            calculate_single_span(ops, before_span_addr_set, span_start_index, span_end_index, ret)
            span_start_index = -1


        if fer_op.opcode == FerOpcode.BEGIN_ARRAY:
            begin_array_indexes.append(i)
            arr_align = array_alignments[i]

            # Our position is now before an arbitrary element of the array. .before is only before the
            # *first* element of the array. After is after an arbitrary element of the array, which is
            # also before the next element after that. So we need to merge the two address sets.
            addr_set = arr_align.before | arr_align.after

        elif fer_op.opcode == FerOpcode.END_ARRAY:
            begin_arr_idx = begin_array_indexes.pop()
            assert ops[begin_arr_idx].opcode == FerOpcode.BEGIN_ARRAY
            arr_align = array_alignments[begin_arr_idx]

            addr_set = arr_align.after

            i += 1 # skip FerRepeatArray

        else:
            if is_var_length_op(fer_op.opcode):
                addr_set = any_addr_set

        # Unlike in optimize_fer_ops(), post_align_exponent is actually *post* here because
        # optimize_fer_ops() has "left-shifted" the alignments (they are for the next element).
        addr_set = apply_alignment(addr_set, fer_op.post_align_exponent)
        i += 1

    return ret


def addr_set_shift_distance(before: int, shifted: int) -> int:
    for i in range(8):
        if before << i == shifted:
            return i
    return -1

def generate_cg_reader(templates: Dict[str, TextTemplate], test_templates: Dict[str, TextTemplate],
                       signature: str, class_name: str,
                       out_filename: str, validate_utf8: bool, append: bool = False):

    fer_code = ferCode.fer_encode_signature(signature)
    #print(fer_code)

    array_alignments: Dict[int, ArrayAlignments] = ferCode.optimize_fer_ops(fer_code)

    span_addrs: Dict[int, SpanItem] = calculate_span_addrs(fer_code, array_alignments)

    assert isinstance(fer_code[0], FerOp)
    assert fer_code[0].opcode == FerOpcode.BEGIN_METHOD_SIGNATURE

    reader_callback_decls = []

    decl_helper_methods = []
    def_helper_methods = []
    test_process_arg_defs = []

    arg_reader_blocks_stack = [[]]
    arg_reader_blocks = arg_reader_blocks_stack[0]

    array_stack = []

    arg_num = 0 # for unique names for "receive argument callback" functions
    array_num = 0 # for unique names for "parse array" functions
    # (TOOD for variants, need to support alignment before first element)
    span_end_item = None
    i = 1
    while i < len(fer_code):
        fer_op = fer_code[i]
        assert isinstance(fer_op, FerOp)

        span_item = None
        span_item_is_first = False
        if i in span_addrs:
            span_item = span_addrs[i]

            if not span_end_item:
                # we are in a new span, remember that and also find the end item which is needed
                # to eliminate length checks
                span_item_is_first = True
                for j in range(i, len(fer_code)):
                    if span_addrs[j].is_last:
                        span_end_item = span_addrs[j]
                        break
                assert span_end_item

        match fer_op.opcode:
            case FerOpcode.COPY1 | FerOpcode.COPY2 | FerOpcode.COPY4 | FerOpcode.COPY8:
                try:
                    data_state = fer_code[i - 1].io_state
                except: # fer_code[i - 1] was probably a RepeatArrayInfo
                    data_state = fer_code[i - 2].io_state
                data_type = c_primitive_type(data_state)

                receiver_name = 'processArg' + str(arg_num)
                receiver_type = data_type
                if data_state == IoState.BOOLEAN:
                    receiver_type = 'bool'
                elif data_state == IoState.UNIX_FD:
                    receiver_type = 'int'
                arg_num += 1
                reader_callback_decls.append(f'    void {receiver_name}({receiver_type} arg);\n')

                insertions = {}
                if fer_op.post_align_exponent != 0:
                    # if min length with padding == max length with padding, the padding is fixed length
                    if span_item and span_item.min_length == span_item.max_length:
                        pad_length = span_item.min_length - fixed_op_part_length(fer_op.opcode)
                        insertions['Align'] = templates['AlignFixed'].render(
                                                    {'FixedAlign': pad_length}, {})
                    else:
                        insertions['Align'] = templates['Align'].render(
                                                    {'PostAlign': (1 << fer_op.post_align_exponent)}, {})
                    insertions['CheckPadding'] = templates['CheckPadding'].render({}, {})

                if span_item_is_first and span_item.max_pos_after <= span_end_item.min_pos_after:
                    # length check "ahead" as far as possible, usually eliminates some subsequent checks
                    insertions['CheckSpanLength'] = templates['CheckSpanLength'].render(
                                                    {'SpanLength': span_end_item.min_pos_after}, {})
                elif span_item and span_item.max_pos_after <= span_end_item.min_pos_after:
                    pass # No length check, this one is covered by the one at the first span item!
                else:
                    insertions['CheckLength'] = templates['CheckLength'].render({}, {})

                retTemplateName = 'ReturnUnixFd' if data_state == IoState.UNIX_FD else 'ReturnPrimitive'
                insertions['ReturnPrimitive'] = templates[retTemplateName].render(
                                                {'ProcessArgFunc': receiver_name}, {})

                arg_reader_blocks.append(templates['ReadPrimitive'].render(
                    {'ReadType': data_type},
                    insertions))

                if test_templates:
                    parg_def = test_templates['ProcessArgPrimitive'].render(
                        {'ReceiverName': receiver_name,
                         'ArgParameterType': receiver_type,
                         'ArgReadType': reader_name(data_state)}, {})
                    test_process_arg_defs.append(parg_def)

            case FerOpcode.STRING | FerOpcode.OBJECT_PATH | FerOpcode.SIGNATURE:
                length_type = 'uint32' if fer_op.opcode == FerOpcode.STRING else 'byte'

                receiver_name = 'processArg' + str(arg_num)
                arg_num += 1
                reader_callback_decls.append(f'    void {receiver_name}(const char *ptr, uint32 len);\n')

                insertions = {}
                if fer_op.post_align_exponent != 0:
                    insertions['StringAlign'] = templates['Align'].render(
                                                    {'PostAlign': 1 << fer_op.post_align_exponent}, {})
                    insertions['CheckPaddingAfterString'] = templates['CheckPadding'].render({}, {})

                if span_item and span_item.max_pos_after <= span_end_item.min_pos_after:
                    assert span_item == span_end_item # var length items always end a span
                    pass # No length check, this one is covered by the one at the first span item!
                else:
                    insertions['CheckStringLengthFieldLength'] = templates['CheckLength'].render({}, {})

                if fer_op.opcode == FerOpcode.STRING:
                    insertions['ValidateString'] = templates['ValidateString'].render(
                        {'ValidateUtf8': 'true' if validate_utf8 else 'false'}, {})
                elif fer_op.opcode == FerOpcode.OBJECT_PATH:
                    insertions['ValidateString'] = templates['ValidateObjectPath'].render({}, {})
                elif fer_op.opcode == FerOpcode.SIGNATURE:
                    insertions['ValidateString'] = templates['ValidateSignature'].render({}, {})

                arg_reader_blocks.append(templates['ReadString'].render(
                    {'LengthType': length_type, 'ProcessArgFunc': receiver_name},
                    insertions))

                if test_templates:
                    parg_def = test_templates['ProcessArgString'].render(
                        {'ReceiverName': receiver_name,
                         'ArgReadType': reader_name_for_opcode(fer_op.opcode)}, {})
                    test_process_arg_defs.append(parg_def)

            case FerOpcode.BEGIN_ARRAY:
                parse_array_name = 'readArray' + str(array_num)
                array_num += 1
                decl_helper_methods.append(f'    bool {parse_array_name}();\n')

                assert not span_item or span_item == span_end_item # var length items always end a span

                insertions = {}
                # Note that max_pos_after does not include alignment padding (for our convenience,
                # cf. comment in calculate_single_span()). We check that padding length "for free" anyway
                # with: if (endPtr > m_dataEnd || ...) in the C++ code.
                if span_item and span_item.max_pos_after <= span_end_item.min_pos_after:
                    pass # No length check, this one is covered by the one at the first span item!
                else:
                    insertions['CheckArrayLengthFieldLength'] = templates['CheckLength'].render({}, {})

                array_pre_align = fer_op.post_align_exponent
                if array_pre_align != 0:
                    assert array_pre_align == 3
                    align_insertions = {}
                    if span_item and span_item.min_length == span_item.max_length:
                        # Fixed length = 0 padding is already eliminated by padding elimination logic,
                        # so under the condition array_pre_align != 0, the only possible fixed length is 4.
                        # But let's calculate it properly and verify.
                        fixed_pad_length = span_item.min_length - fixed_op_part_length(fer_op.opcode)
                        assert fixed_pad_length == 4
                        align_insertions['AlignInBeforeArrayAlign'] = templates['AlignFixed'].render(
                                                    {'FixedAlign': fixed_pad_length}, {})
                    else:
                        align_insertions['AlignInBeforeArrayAlign'] = templates['Align'].render(
                                                    {'PostAlign': (1 << fer_op.post_align_exponent)}, {})

                    insertions['BeforeArray'] = templates['BeforeArrayAlign'].render({}, align_insertions)
                else:
                    insertions['BeforeArray'] = templates['BeforeArrayNoAlign'].render({}, {})

                arg_reader_blocks.append(templates['ReadArray'].render(
                                         {'ReadArray': parse_array_name}, insertions))

                arg_reader_blocks_stack.append([])
                arg_reader_blocks = arg_reader_blocks_stack[-1]

                array_stack.append((parse_array_name, array_alignments[i].after))

            case FerOpcode.END_ARRAY:
                parse_array_name, end_array_addr_set = array_stack.pop()

                insertions = {'ArgReaders': indent(''.join(arg_reader_blocks), '        ')}

                if fer_op.post_align_exponent != 0:
                    # Apply fixed or variable length alignment (and find out which one we have)
                    aligned_after_addrs = apply_alignment(end_array_addr_set, fer_op.post_align_exponent)
                    after_shift = addr_set_shift_distance(end_array_addr_set, aligned_after_addrs)
                    assert after_shift != 0 # post_align_exponent would be 0 in that case

                    if after_shift >= 0:
                        align_insertions = {'AlignmentForAfterArrayAlign': templates['AlignFixed'].render(
                                                    {'FixedAlign': after_shift}, {})}
                    else:
                        align_insertions = {'AlignmentForAfterArrayAlign': templates['Align'].render(
                                                    {'PostAlign': (1 << fer_op.post_align_exponent)}, {})}

                    insertions['AfterArrayAlign'] = templates['AfterArrayAlign'].render(
                                                    {}, align_insertions)

                i +=1
                fer_repeat_array = fer_code[i]
                if fer_repeat_array.go_back_align_exponent != 0:
                    # Apply fixed or variable length alignment (and find out which one we have)
                    aligned_repeat_addrs = apply_alignment(end_array_addr_set,
                                                           fer_repeat_array.go_back_align_exponent)
                    repeat_shift = addr_set_shift_distance(end_array_addr_set, aligned_repeat_addrs)
                    assert repeat_shift != 0 # go_back_align_exponent would be 0 in that case

                    if repeat_shift >= 0:
                        align_ins = templates['AlignFixed'].render({'FixedAlign': repeat_shift}, {})
                    else:
                        repeat_alignment = 1 << fer_repeat_array.go_back_align_exponent
                        align_ins = templates['Align'].render({'PostAlign': repeat_alignment}, {})

                    align_insertions = {'AlignmentForArrayRepeatAlign': indent(align_ins, '        ')}
                    insertions['ArrayRepeatAlign'] = templates['ArrayRepeatAlign'].render(
                                                     {}, align_insertions)

                def_helper_methods.append(templates['ParseArray'].render(
                    {'CgReader': class_name,
                     'ReadArray': parse_array_name},
                    insertions))

                arg_reader_blocks_stack.pop()
                arg_reader_blocks = arg_reader_blocks_stack[-1]

            # TODO is struct elision REALLY on? It doesn't look so from the code, I may be wrong
            case FerOpcode.ENTER_VARIANT | FerOpcode.END_VARIANT_SIGNATURE:
                assert false, "Struct callbacks are not implemented (struct elision is always on)"

            case FerOpcode.ENTER_VARIANT | FerOpcode.END_VARIANT_SIGNATURE:
                assert false, "Variants are not implemented for now"

            case FerOpcode.BEGIN_VARIANT_SIGNATURE | FerOpcode.BEGIN_METHOD_SIGNATURE:
                assert false, "These should never occur after the start of the list"

            case FerOpcode.END:
                assert i == len(fer_code) - 1

        if span_item and span_item.is_last:
            span_end_item = None # this triggers "new span" detection for the next one
        i += 1

    ret = ''

    assert len(array_stack) == 0
    assert len(arg_reader_blocks_stack) == 1

    top_insertions = {'ArgReaders': arg_reader_blocks,
                      'DeclHelperMethods': decl_helper_methods,
                      'DefHelperMethods': def_helper_methods,
                      'ProcessArgCallbacks': reader_callback_decls}
    if test_templates:
        top_insertions['CgReadTester'] = test_templates['CgReadTester'].render(
            {'TestConsumer': class_name.replace('CgReader', 'CgConsumer')},
            {'ProcessArgDefinitions': test_process_arg_defs})

    if not append:
        # Write things that need to be in the output file exactly once
        top_insertions['Includes'] = templates['Includes'].render({}, {})
        top_insertions['Utilities'] = templates['Utilities'].render({}, {})
        if test_templates:
            top_insertions['TestIncludes'] = test_templates['TestIncludes'].render({}, {})
            top_insertions['TestHelpers'] = test_templates['TestHelpers'].render({}, {})

    ret = templates['TopDecl'].render(
        {'CgReader': class_name},
        top_insertions)

    #print(ret)
    with open (out_filename, 'a' if append else 'w') as f:
        f.write(ret)

def generate_test_read_func(test_templates: Dict[str, TextTemplate], sigs_classes: Dict[str, str],
                            out_filename: str):
    map_entries = []
    for sig, c in sigs_classes.items():
        map_vars = {'TesterSignature': sig,
                    'CgReader': c,
                    'TestConsumer': c.replace('CgReader', 'CgConsumer')}
        map_entries.append(test_templates['TesterMapEntry'].render(map_vars, {}))

    ret = test_templates['TestReadFunc'].render({}, {'TesterMapEntries': map_entries})

    with open (out_filename, 'a' if append else 'w') as f:
        f.write(ret)

if __name__ == "__main__":
    # Work around Python's lame import system to import ferCode.py from current dir
    sys.path.append(os.path.dirname(os.path.realpath(__file__)))
    import ferCode
    from ferCode import apply_alignment, apply_addition, apply_addition_direct, is_basic_addition_op, \
         is_var_length_op, IoState, FerOpcode, FerOp, FerRepeatArray, FerNesting

    gen_tests = False
    argv = sys.argv.copy()

    gen_tests = '-t' in argv[1:3]
    if gen_tests:
        argv.remove('-t')

    validate_utf8 = '-U' not in argv[1:2]
    if not validate_utf8:
        argv.remove('-U')

    if len(argv) != 4:
        print("Usage 1: codegen.py [-t] [-U] <type signature> <class name> <output file>\n"
              "Usage 2: codegen.py [-t] [-U] <output file> -i <input file>\n"
              "-t generates code for testing, -U disables unicode validation for strings")
        sys.exit(-1)

    templates = parse_templates('argumentscgreader_t.cpp')
    test_templates = parse_templates('argumentscgtester_t.cpp') if gen_tests else {}

    sigs_classes = {}

    if argv[2] == '-i':
        append = False
        output_file = argv[1]
        with open (argv[3], 'r') as input_file:
            for line in input_file:
                signature, class_name = line.strip('\n').split(' ', 2)
                # ### empty signatures and variants are not supported for now; empty signatures should
                #     perhaps be made to work for convenience in "automated situations" such as this one,
                #     variant support is planned but takes more work.
                if len(signature) > 0 and not 'v' in signature:
                    sigs_classes[signature] = class_name
                    generate_cg_reader(templates, test_templates, signature, class_name, output_file,
                                       validate_utf8, append)
                    append = True
    else:
        output_file = argv[3]
        sigs_classes[argv[1]] = argv[2]
        generate_cg_reader(templates, test_templates, argv[1], argv[2], output_file, validate_utf8)

    if test_templates:
        # TODO skip the same classes that are skipped in
        generate_test_read_func(test_templates, sigs_classes, output_file)
