#pragma once
#include <string>
namespace amtest {
inline const std::string source = R"AM(<MODELFILE>
ProductVersion=19.5
<OBJECTS><MODEL>
Name=Regression
<MESH>
<SPLINE>
262145 0 1 -1 -1 0 . .
262145 0 2 1 -1 0 . .
</SPLINE>
<SPLINE>
262145 1 3 2 . .
262145 0 4 1 1 0 . .
</SPLINE>
<SPLINE>
262145 1 5 4 . .
262145 0 6 -1 1 0 . .
</SPLINE>
<SPLINE>
262145 1 7 6 . .
262145 1 8 1 . .
</SPLINE>
<PATCHES>
24 1 2 4 6
</PATCHES>
</MESH>
<GROUPS><GROUP>
Name=Red
DiffuseColor=255 0 0
<CPS>1 2 4 6</CPS>
</GROUP></GROUPS>
<BONES><BONE>Name=Retained</BONE></BONES>
<UNKNOWN>Preserve me</UNKNOWN>
</MODEL></OBJECTS></MODELFILE>)AM";
}
