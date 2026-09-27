//=============================================================================
// U64Bench -- ufront 2.83: the cost of resolving script object references (an index per variable,
// property, function or class operand). Time the process from outside:
//     /usr/bin/time ./ucc-bin U64Test.U64Bench 9000000
// and subtract a run with 0. Stays under the VM's 10,000,000-iteration runaway limit.
//=============================================================================
class U64Bench expands Commandlet;

var int Field;

final function int Add( int X, int Y ) { return X + Y; }

event int Main( string Parms )
{
	local int i, n, a, b, c;
	n = int( Parms );
	a = 1;	b = 2;
	for( i=0; i<n; i++ )
	{
		c = a + b;
		Field = Add( c, Field );
	}
	Log( "U64 BENCH iterations="$n$" field="$Field );
	return 0;
}
