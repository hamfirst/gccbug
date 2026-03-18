/usr/bin/c++ -fmodules -freflection -g -std=gnu++26 -fdiagnostics-color=always -E -x c++ Types.ixx -MT Types.ixx.o.ddi -MD -MF Types.ixx.o.ddi.d -fmodules-ts -fdeps-file=Types.ixx.o.ddi -fdeps-target=Types.ixx.o -fdeps-format=p1689r5 -o Types.ixx.o.ddi.i
/usr/bin/c++ -fmodules -freflection -g -std=gnu++26 -fdiagnostics-color=always -E -x c++ BlockTable.ixx -MT BlockTable.ixx.o.ddi -MD -MF BlockTable.ixx.o.ddi.d -fmodules-ts -fdeps-file=BlockTable.ixx.o.ddi -fdeps-target=BlockTable.ixx.o -fdeps-format=p1689r5 -o BlockTable.ixx.o.ddi.i
/usr/bin/c++ -fmodules -freflection -g -std=gnu++26 -fdiagnostics-color=always -E -x c++ YT.ixx -MT YT.ixx.o.ddi -MD -MF YT.ixx.o.ddi.d -fmodules-ts -fdeps-file=YT.ixx.o.ddi -fdeps-target=YT.ixx.o -fdeps-format=p1689r5 -o YT.ixx.o.ddi.i

/usr/bin/cmake -E cmake_ninja_dyndep --tdi=./CXXDependInfo.json --lang=CXX --modmapfmt=gcc --dd=./CXX.dd BlockTable.ixx.o.ddi Types.ixx.o.ddi YT.ixx.o.ddi

/usr/bin/c++ -fmodules -freflection -g -std=gnu++26 -fdiagnostics-color=always -MD -MT Types.ixx.o -MF Types.ixx.o.d -fmodules-ts -fmodule-mapper=Types.ixx.o.modmap -MD -fdeps-format=p1689r5 -x c++ -o Types.ixx.o -c Types.ixx
/usr/bin/c++ -fmodules -freflection -g -std=gnu++26 -fdiagnostics-color=always -MD -MT BlockTable.ixx.o -MF BlockTable.ixx.o.d -fmodules-ts -fmodule-mapper=BlockTable.ixx.o.modmap -MD -fdeps-format=p1689r5 -x c++ -o BlockTable.ixx.o -c BlockTable.ixx
