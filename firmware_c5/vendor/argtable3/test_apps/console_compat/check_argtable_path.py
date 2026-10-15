import argparse
import subprocess
import sys
import os
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("elf_file", type=str, help="The ELF file to check")
    parser.add_argument("iec_path", type=str, help="The path to the idf-extra-components directory")
    parser.add_argument("idf_path", type=str, help="The path to the esp-idf directory")
    parser.add_argument("strings_program", type=str, help="The program to use to extract strings from the ELF file (e.g. 'strings')")
    args = parser.parse_args()


    # look for any files from argtable3 directory in 'strings app.elf'
    # This is more generic and works regardless of which argtable3 functions are used
    # Matches both IEC path (/argtable3/src/arg_*.c) and IDF path (/argtable3/arg_*.c)
    strings_output = subprocess.check_output([args.strings_program, args.elf_file], encoding="utf-8")
    lines_with_argtable = [line for line in strings_output.splitlines() if re.search(r'/argtable3/(src/)?arg_\w+\.c', line)]

    found_iec_path = False
    found_idf_path = False
    iec_files = []
    idf_files = []

    for line in lines_with_argtable:
        if os.path.abspath(args.iec_path) in line:
            found_iec_path = True
            iec_files.append(line.strip())
        if os.path.abspath(args.idf_path) in line:
            found_idf_path = True
            idf_files.append(line.strip())

    print("Found argtable3 files from IEC:", file=sys.stderr)
    for f in iec_files:
        print(f"  {f}", file=sys.stderr)
    
    print("Found argtable3 files from IDF:", file=sys.stderr)
    for f in idf_files:
        print(f"  {f}", file=sys.stderr)

    print(f"\nSummary - IDF files: {len(idf_files)}, IEC files: {len(iec_files)}", file=sys.stderr)
    if found_idf_path or not found_iec_path:
        print("Error: argtable3 files were found in IDF or not found in IEC", file=sys.stderr)
        raise SystemExit(1)

if __name__ == "__main__":
    main()
