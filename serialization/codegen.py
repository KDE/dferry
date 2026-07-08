#!/usr/bin/python3

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
def parse_templates() -> Dict[str, TextTemplate]:
    tp_filename = os.path.dirname(os.path.realpath(__file__)) + '/argumentscgreader_t.cpp'

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

def main():
    if len(sys.argv) != 4:
        print("Usage: codedegn.py <type signature> <class name> <file name>")
        return -1

    signature = sys.argv[1]
    class_name = sys.argv[2]
    out_filename = sys.argv[3]

    fer_code = ferCode.fer_encode_signature(signature)

    ferCode.optimize_fer_ops(fer_code)

    #print(fer_code)

    templates = parse_templates()
    #print(templates)

    assert isinstance(fer_code[0], FerOp)
    assert fer_code[0].opcode == FerOpcode.BEGIN_METHOD_SIGNATURE

    reader_callback_decls = []

    decl_helper_methods = []
    def_helper_methods = []

    arg_reader_blocks_stack = [[]]
    arg_reader_blocks = arg_reader_blocks_stack[0]

    array_stack = []

    arg_num = 0 # for unique names for "receive argument callback" functions
    array_num = 0 # for unique names for "parse array" functions
    i = 1
    while i < len(fer_code):
        fer_op = fer_code[i]
        assert isinstance(fer_op, FerOp)

        match fer_op.opcode:
            case FerOpcode.COPY1 | FerOpcode.COPY2 | FerOpcode.COPY4 | FerOpcode.COPY8:
                data_state = fer_code[i - 1].io_state
                data_type = c_primitive_type(data_state)

                receiver_name = 'processArg' + str(arg_num)
                arg_num += 1
                if data_state != IoState.BOOLEAN:
                    reader_callback_decls.append(f'    void {receiver_name}({data_type} arg);\n')
                else:
                    reader_callback_decls.append(f'    void {receiver_name}(bool arg);\n')

                insertions = {}
                if fer_op.post_align_exponent != 0:
                    insertions['Align'] = templates['Align'].render({'PostAlign':
                                                                  (1 << fer_op.post_align_exponent)}, {})}
                    insertions['CheckPadding'] = templates['CheckPadding'].render({}, {})

                arg_reader_blocks.append(templates['ReadPrimitive'].render(
                    {'ReadType': data_type, 'ProcessArgFunc': receiver_name},
                    insertions))

            case FerOpcode.STRING | FerOpcode.OBJECT_PATH | FerOpcode.SIGNATURE:
                length_type = 'uint32' if fer_op.opcode == FerOpcode.STRING else 'byte'

                receiver_name = 'processArg' + str(arg_num)
                arg_num += 1
                reader_callback_decls.append(f'    void {receiver_name}(const char *ptr, uint32 len);\n')

                insertions = {}
                if fer_op.post_align_exponent != 0:
                    insertions['StringAlign'] = templates['Align'].render({'PostAlign':
                                                                        1 << fer_op.post_align_exponent}, {})}
                    insertions['CheckPaddingAfterString'] = templates['CheckPadding'].render({}, {})

                arg_reader_blocks.append(templates['ReadString'].render(
                    {'LengthType': length_type, 'ProcessArgFunc': receiver_name},
                    insertions))

            case FerOpcode.BEGIN_ARRAY:
                parse_array_name = 'readArray' + str(array_num)
                array_num += 1
                decl_helper_methods.append(f'    bool {parse_array_name}();\n')

                arg_reader_blocks.append(templates['ReadArray'].render(
                                         {'ReadArray': parse_array_name}, {}))

                arg_reader_blocks_stack.append([])
                arg_reader_blocks = arg_reader_blocks_stack[-1]

                array_stack.append((parse_array_name , fer_op.post_align_exponent)) # TODO using the right one here?


            case FerOpcode.END_ARRAY:
                parse_array_name, array_pre_align = array_stack.pop()

                insertions = {'ArgReaders': indent(''.join(arg_reader_blocks), '        ')}

                if array_pre_align != 0:
                    assert array_pre_align == 3
                    insertions['BeforeArray'] = templates['BeforeArrayAlign'].render({}, {})
                else:
                    insertions['BeforeArray'] = templates['BeforeArrayNoAlign'].render({}, {})

                if fer_op.post_align_exponent != 0:
                    insertions['AfterArrayAlign'] = templates['AfterArrayAlign'].render({},
                                                    {'AfterArrayAlign': 1 << fer_op.post_align_exponent})

                i +=1
                fer_repeat_array = fer_code[i]
                if fer_repeat_array.go_back_align_exponent != 0:
                    repeat_alignment = 1 << fer_repeat_array.go_back_align_exponent
                    insertions['ArrayRepeatAlign'] = templates['ArrayRepeatAlign'].render(
                                                     {'ArrayRepeatAlign': repeat_alignment}, {})

                def_helper_methods.append(templates['ParseArray'].render(
                    {'CgReader': class_name,
                     'ReadArray': parse_array_name},
                    insertions))

                arg_reader_blocks_stack.pop()
                arg_reader_blocks = arg_reader_blocks_stack[-1]


            case FerOpcode.ENTER_VARIANT | FerOpcode.END_VARIANT_SIGNATURE:
                assert false, "Struct callbacks are not implemented (struct elision is always on)"

            case FerOpcode.ENTER_VARIANT | FerOpcode.END_VARIANT_SIGNATURE:
                assert false, "Variants are not implemented for now"

            case FerOpcode.BEGIN_VARIANT_SIGNATURE | FerOpcode.BEGIN_METHOD_SIGNATURE:
                assert false, "These should never occur after the start of the list"

            case FerOpcode.END:
                assert i == len(fer_code) - 1

        i += 1

    ret = ''

    assert len(array_stack) == 0
    assert len(arg_reader_blocks_stack) == 1

    top_template = templates['TopDecl']
    ret = templates['TopDecl'].render(
        {'CgReader': class_name},
        {'ArgReaders': arg_reader_blocks,
         'DeclHelperMethods': decl_helper_methods,
         'DefHelperMethods': def_helper_methods,
         'ProcessArgCallbacks': reader_callback_decls})

    #print(ret)
    with open (out_filename, 'w') as f:
        f.write(ret)


if __name__ == "__main__":
    # Work around Python's lame import system to import ferCode.py from current dir
    sys.path.append(os.path.dirname(os.path.realpath(__file__)))
    import ferCode
    from ferCode import IoState, FerOpcode, FerOp, FerRepeatArray, FerNesting

    main()
