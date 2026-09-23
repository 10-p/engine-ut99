// ufront 2.79p oracle: the five string natives OldUnreal 469 backported (238, 239, 322, 323, 324).
// Compiled by 469's ucc; run under stock 469e AND under our 436 -- the NT lines must be identical.
class NativesTest extends Commandlet;

function int Main( string Params )
{
	local string S, R;
	S = "abc";       S $= "def";          Log("NT CONCATEQ [" $ S $ "]");
	S = "abc";       R = (S $= "x");      Log("NT CONCATEQ_RET [" $ R $ "] [" $ S $ "]");
	S = "abc";       S $= 42;             Log("NT CONCATEQ_COERCE [" $ S $ "]");
	S = "hello";     S @= "world";        Log("NT ATEQ [" $ S $ "]");
	S = "";          S @= "x";            Log("NT ATEQ_EMPTY [" $ S $ "]");
	S = "hello";     R = (S @= "there");  Log("NT ATEQ_RET [" $ R $ "] [" $ S $ "]");
	S = "a-b-c-";    S -= "-";            Log("NT SUBEQ [" $ S $ "]");
	S = "aaaa";      S -= "aa";           Log("NT SUBEQ_OVERLAP [" $ S $ "]");
	S = "aaa";       S -= "aa";           Log("NT SUBEQ_ODD [" $ S $ "]");
	S = "abc";       S -= "x";            Log("NT SUBEQ_NONE [" $ S $ "]");
	S = "AbCabcABC"; S -= "abc";          Log("NT SUBEQ_CASE [" $ S $ "]");
	S = "xyzxyz";    R = (S -= "y");      Log("NT SUBEQ_RET [" $ R $ "] [" $ S $ "]");
	Log("NT REPL [" $ Repl("Hello World hello", "hello", "bye") $ "]");
	Log("NT REPL_CS [" $ Repl("Hello World hello", "hello", "bye", true) $ "]");
	Log("NT REPL_NONE [" $ Repl("abc", "x", "y") $ "]");
	Log("NT REPL_GROW [" $ Repl("aaa", "a", "aa") $ "]");
	Log("NT REPL_EMPTYWITH [" $ Repl("a.b.c", ".", "") $ "]");
	Log("NT REPL_COERCE [" $ Repl(12321, 2, 7) $ "]");
	Log("NT LOCS [" $ Locs("MiXeD 123 ABC xyz") $ "]");
	Log("NT DONE");
	return 0;
}
