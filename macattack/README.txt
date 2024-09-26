
MAC attack!
-----------

The files in this directory can be used to trick a program into
thinking that one of your network interfaces (*) has a particular
user-specified MAC address.

  (*) actually, all of them :-)

This can be used to run a program which requires a MAC-locked license
on any machine; e.g., if you switch machines, buy a new server, want
to run on an Amazon or Google cloud virtual/metal machine.

You can compile and use it like this:

----------------------------------------------------------------------
bash$ make
gcc -shared -o ioctl.so -fPIC ioctl.c
bash$ MAC=e8:b1:fc:0a:c7:29 LD_PRELOAD=$PWD/ioctl.so  ./my_locked_program
----------------------------------------------------------------------
