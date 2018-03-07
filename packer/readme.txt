this tool:
1. is used to append all binaries (e.g. stage0.bin, stage1, evmm) to evmm_pkg.bin
2. after that it will update the file offset header in evmm_pkg.bin file.
3. also, it does build time oversize check, to find error as early as possible.
4. will pack secondary guest image(lk.elf) if needed.


  --- end of file ---
