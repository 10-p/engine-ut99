//=============================================================================
// U64Decl -- ufront 2.83: the declarations the other fixture classes never compile -- an enum, a struct
// with object, class, string, static-array, vector and rotator members, consts, ArrayCount, vect()/rot()
// constants and struct defaults. Compile-only: its test is the compiler round-trip (a 64-bit `ucc make`
// must write the same bytes as the 32-bit one, but for the package GUID). Found by the coverage census:
// CompileEnum/CompileStruct/CompileConst had never run at 64-bit.
//=============================================================================
class U64Decl expands Info;

const DeclCount = 3;
const DeclName = "u64";

enum EDeclKind
{
	DK_None,
	DK_Object,
	DK_String
};

struct DeclEntry
{
	var EDeclKind Kind;
	var Actor Target;
	var string Label;
	var int Values[3];
	var class<Actor> TargetClass;
	var vector Where;
	var rotator Facing;
};

var DeclEntry Entries[3];
var DeclEntry Single;
var EDeclKind Kinds[4];

function int Fill( Actor Other )
{
	local DeclEntry E;
	local int i;

	E.Kind = DK_Object;
	E.Target = Other;
	E.Label = DeclName;
	E.Values[0] = DeclCount;
	E.Values[2] = DeclCount * 2;
	E.TargetClass = class'Info';
	E.Where = vect(1,2,3);
	E.Facing = rot(100,200,300);
	for( i=0; i<ArrayCount(Entries); i++ )
		Entries[i] = E;
	Single = Entries[1];
	if( Single == E && Single.Target == Other && Kinds[3] == DK_String )
		return ArrayCount(Entries) + ArrayCount(Kinds);
	return -1;
}

defaultproperties
{
	Single=(Kind=DK_String,Label="default",TargetClass=Class'Engine.Info',Where=(X=1.000000,Y=2.000000,Z=3.000000),Facing=(Pitch=4,Yaw=5,Roll=6))
	Kinds(0)=DK_Object
	Kinds(3)=DK_String
}
