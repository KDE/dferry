#!/usr/bin/python3

import os
import sys

def main():
    if len(sys.argv) != 4:
        print("Usage: codedegn.py <type signature> <class name> <file name>")
        return -1

    signature = sys.argv[1]
    class_name = sys.argv[2]
    out_filename = sys.argv[3]

    fer_code = ferCode.fer_encode_signature(signature)

    ferCode.optimize_fer_ops(fer_code)

    print(fer_code)


if __name__ == "__main__":
    # Work around Python's lame import system to import ferCode.py from current dir
    sys.path.append(os.path.dirname(os.path.realpath(__file__)))
    import ferCode

    main()
