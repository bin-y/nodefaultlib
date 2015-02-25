# nodefaultlib
--------------
A tool to remove [the default C and C++ libraries that a program will link with](http://support.microsoft.com/kb/154753) (msvcrt.lib libcmt.lib etc.) from .lib and .obj files
It can also remove the equivalent code generation option (-MT/-MD/-ML) from [/LTCG](https://msdn.microsoft.com/en-us/library/xbf3tbeh.aspx) optimized libraries. (EXPERIMENTAL)

A solution to ignore libcmt/msvcrt conflicts.
