# Building
- debug: ./c_build.ps1 -BuildType debug -Build
- release: ./c_build.ps1 -BuildType release -Build

c_build is an open source build system for c/c++.
The commands that are run on your computer are transparently shown

Output for this c_build_script.py:
```
cd "./build_clang++/debug" ; clang++ ../../Source/core.cpp ../../Source/pct.cpp -std=c++20 -c -Wall -Wno-deprecated -Wno-parentheses -Wno-unused-variable -Wno-int-to-void-pointer-cast -Wno-void-pointer-to-int-cast -Wno-reserved-user-defined-literal -Wno-unused-but-set-variable -Werror -g -O0; libtool -static -o PCT.lib core.o pct.o ; cd "/Users/jovannidjonaj/Documents/Code/C/PCT"
Compilation of PCT.lib successful

cd "./Test/PrintThread/build_clang++/debug" ; clang++ ../../main.c -std=c++20 -o print_thread.exe -Wall -Wno-deprecated -Wno-parentheses -Wno-unused-variable -Wno-int-to-void-pointer-cast -Wno-void-pointer-to-int-cast -Wno-reserved-user-defined-literal -Wno-unused-but-set-variable -Werror -g ../../../../build_clang++/debug/PCT.lib -O0 ; cd "/Users/jovannidjonaj/Documents/Code/C/PCT"
Compilation of print_thread.exe successful

cd "./Test/Prog3/build_clang++/debug" ; clang++ ../../thread.c ../../thread-main.c -std=c++20 -o prog3.exe -Wall -Wno-deprecated -Wno-parentheses -Wno-unused-variable -Wno-int-to-void-pointer-cast -Wno-void-pointer-to-int-cast -Wno-reserved-user-defined-literal -Wno-unused-but-set-variable -Werror -g ../../../../build_clang++/debug/PCT.lib -O0 ; cd "/Users/jovannidjonaj/Documents/Code/C/PCT"
Compilation of prog3.exe successful

cd "./Test/Prog3_Threads/build_clang++/debug" ; clang++ ../../thread.c ../../thread-main.c -std=c++20 -o prog3_threads.exe -Wall -Wno-deprecated -Wno-parentheses -Wno-unused-variable -Wno-int-to-void-pointer-cast -Wno-void-pointer-to-int-cast -Wno-reserved-user-defined-literal -Wno-unused-but-set-variable -Werror -g ../../../../build_clang++/debug/PCT.lib -O0 ; cd "/Users/jovannidjonaj/Documents/Code/C/PCT"
Compilation of prog3_threads.exe successful

cd "./Test/Prog4/build_clang++/debug" ; clang++ ../../thread.c ../../thread-main.c -std=c++20 -o prog4.exe -Wall -Wno-deprecated -Wno-parentheses -Wno-unused-variable -Wno-int-to-void-pointer-cast -Wno-void-pointer-to-int-cast -Wno-reserved-user-defined-literal -Wno-unused-but-set-variable -Werror -g ../../../../build_clang++/debug/PCT.lib -O0 ; cd "/Users/jovannidjonaj/Documents/Code/C/PCT"
Compilation of prog4.exe successful
```

# Running
run the exe like normal or for convience tack on the -Run flag

- debug: ./c_build.ps1 -BuildType debug -Run
- release: ./c_build.ps1 -BuildType release -Run