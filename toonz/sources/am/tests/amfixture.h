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
inline std::string rigidBoneSource() {
  const std::string placeholder = "<BONES><BONE>Name=Retained</BONE></BONES>";
  auto result                   = source;
  result.replace(result.find(placeholder), placeholder.size(), R"AM(<BONES>
<SEGMENT>
Name=main
Start=1 0 0
End=1 0 10
Rotate=0 0 0 1
BoneColor=255 0 0 255
<NONSKINNEDCPS>1 4 5 8</NONSKINNEDCPS>
<SEGMENT>
Name=central
Start=0 1 1
End=0 1 6
Rotate=0 0 0.7071067811865476 0.7071067811865476
Length=5
BoneColor=0 255 255 255
<NONSKINNEDCPS>2 3</NONSKINNEDCPS>
</SEGMENT>
</SEGMENT>
</BONES>)AM");
  return result;
}
inline const std::string splineSource = R"AM(<MODELFILE>
ProductVersion=19.5
<OBJECTS><MODEL>
Name=InkRegression
<MESH>
<SPLINE>
262145 0 1 -1 -0.5 0 . .
262145 0 2 1 -0.5 0 . .
</SPLINE>
<SPLINE>
262145 0 3 -1 0.5 0 . .
262145 0 4 1 0.5 0 . .
</SPLINE>
</MESH>
<GROUPS><GROUP>
Name=Lower
<CPS>1 2</CPS>
</GROUP></GROUPS>
</MODEL></OBJECTS></MODELFILE>)AM";
}  // namespace amtest
