//=============================================================================
// U64Mutator -- the script VM checks for the 64-bit (LP64) port.
//
// Add it to a match (?mutator=U64Test.U64Mutator) and read the log:
//     U64 PASS <case> / U64 FAIL <case>, then U64 DONE passed=<n> failed=<n>.
// Every case must pass on the 32-bit build first (the control), then on 64-bit. The cases are the
// 64-bit mechanisms: script object references are 4-byte object indices, and a None context
// zero-fills its result with the compiler's (32-bit) size, which the VM must widen.
//=============================================================================
class U64Mutator expands Mutator;

var Actor           InstObj;
var Actor           InstArr[4];
var PointRegion     InstRegion;
var string          InstStr;
var bool            bFlagA, bFlagB;
var int             Passed, Failed;
var U64StateActor   Stater;

function Report( bool bOk, string Case )
{
	if( bOk )	{ Passed++; Log( "U64 PASS "$Case ); }
	else		{ Failed++; Log( "U64 FAIL "$Case ); }
}

function Actor GetSelf()				{ return self; }
final function Actor FinalGetSelf()		{ return self; }
function Actor Echo( Actor A )			{ return A; }
function bool IsNone( Actor A )			{ return A == None; }
function Actor ReturnNone( Actor Nobody )	{ return Nobody.Owner; }
function Actor AssignInsideThenSelf( Actor Nobody )
{
	InstObj = Nobody.Owner;		// a None-context fill of InstObj inside the callee...
	return self;				// ...must not clear the caller's `InstObj = <this>` afterwards.
}

function PostBeginPlay()
{
	Super.PostBeginPlay();
	RunTests();
	Stater = Spawn( class'U64StateActor' );
	SetTimer( 2.0, false );
}

function Timer()
{
	Report( Stater != None && Stater.Stage == 3, "state_labels_sleep_gotostate" );
	Log( "U64 DONE passed="$Passed$" failed="$Failed );
}

function RunTests()
{
	local Actor A, Nobody, It;
	local U64Mutator NoMut;
	local class<Actor> C;
	local PointRegion R, R2;
	local string S;
	local name NameVal;
	local byte B;
	local int i, n;
	local Pawn P;

	// Preconditions: the values the None cases overwrite must be live.
	Report( Region.Zone != None, "precondition_region_zone" );
	Report( Nobody == None && NoMut == None, "precondition_none_locals" );

	// A None context writes zeros straight into the destination, sized by the compiler.
	A = self;			A = Nobody.Owner;			Report( A == None, "none_ctx_local_object" );
	InstObj = self;		InstObj = Nobody.Owner;		Report( InstObj == None, "none_ctx_instance_object" );
	InstArr[2] = self;	InstArr[2] = Nobody.Owner;	Report( InstArr[2] == None && InstArr[1] == None, "none_ctx_array_element" );
	C = Class;			C = Nobody.Class;			Report( C == None, "none_ctx_class" );
	R = Region;			R = Nobody.Region;			Report( R.Zone == None && R.iLeaf == 0 && R.ZoneNumber == 0, "none_ctx_struct_with_pointer" );
	InstRegion = Region; InstRegion = Nobody.Region; Report( InstRegion.Zone == None, "none_ctx_instance_struct" );
	R2 = Region;		R2.Zone = Nobody.Region.Zone; Report( R2.Zone == None && R2.iLeaf == Region.iLeaf, "none_ctx_struct_member_dest" );
	A = self;			A = NoMut.GetSelf();		Report( A == None, "none_ctx_virtual_call_result" );
	A = self;			A = NoMut.FinalGetSelf();	Report( A == None, "none_ctx_final_call_result" );
	Report( IsNone( Nobody.Owner ), "none_ctx_as_parameter" );
	A = self;			A = Echo( Nobody.Owner );	Report( A == None, "none_ctx_nested_argument" );
	A = self;			A = ReturnNone( Nobody );	Report( A == None, "none_ctx_return_value" );
	InstObj = self;		InstObj = AssignInsideThenSelf( Nobody ); Report( InstObj == self, "none_ctx_callee_does_not_clear_caller" );
	S = "abc";			S = NoMut.InstStr;			Report( S == "abc", "none_ctx_string_keeps_value" );
	A = self;			A = Pawn(Nobody.Owner); Report( A == None, "none_ctx_under_cast" );

	// Object references read from the bytecode: variables, constants, casts, calls.
	Report( GetSelf() == self && FinalGetSelf() == self, "call_virtual_and_final" );
	Report( class'GameInfo' != None && class'GameInfo'.Name == 'GameInfo', "object_const_class" );
	Report( class'U64Mutator'.default.InstStr == "dflt", "class_default_property" );
	C = class<Actor>( DynamicLoadObject( "Engine.Pawn", class'Class' ) );
	Report( C == class'Pawn', "metacast" );
	A = Level.Game;		P = Pawn( A );				Report( P == None && GameInfo( A ) == Level.Game, "dynamic_cast" );
	bFlagA = true;		bFlagB = false;				Report( bFlagA && !bFlagB, "bool_instance_variables" );
	R = Region;			R2 = Region;				Report( R == R2, "struct_compare_equal" );
	R2.iLeaf++;										Report( R != R2, "struct_compare_differ" );

	// switch compares the compiled size; an object switch must compare whole pointers.
	A = self;	n = 0;
	switch( A ) { case None: n = 1; break; case Level: n = 2; break; case self: n = 3; break; default: n = 4; }
	Report( n == 3, "switch_object" );
	NameVal = 'Beta';	n = 0;
	switch( NameVal ) { case 'Alpha': n = 1; break; case 'Beta': n = 2; break; }
	Report( n == 2, "switch_name" );
	B = 7;		n = 0;
	switch( B ) { case 3: n = 1; break; case 7: n = 2; break; }
	Report( n == 2, "switch_byte" );
	S = "two";	n = 0;
	switch( S ) { case "one": n = 1; break; case "two": n = 2; break; }
	Report( n == 2, "switch_string" );

	// Iterators jump by code offset.
	n = 0;	foreach AllActors( class'Actor', It ) n++;
	Report( n > 10, "foreach_allactors" );
	n = 0;	foreach AllActors( class'Actor', It ) { n++; if( n == 3 ) break; }
	Report( n == 3, "foreach_break" );
}

defaultproperties
{
	InstStr="dflt"
}
