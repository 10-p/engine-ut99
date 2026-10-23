/*=============================================================================
	NSDLInputTest.h: the input test channel (-INPUTTEST=<file> on the command line).

	A test harness appends command lines to <file>; the viewport runs them as they arrive, at the start of
	each input tick. Pad commands drive a virtual game controller made with SDL's virtual joystick API, so
	everything from SDL's game controller onwards is the real path. Commands:
	  pad attach | pad detach
	  pad button <sdl button name> <0|1>    a b x y back guide start leftstick rightstick leftshoulder
	                                        rightshoulder dpup dpdown dpleft dpright
	  pad axis <sdl axis name> <value>      leftx lefty rightx righty: -32768..32767 (y down is +);
	                                        lefttrigger righttrigger: 0..32767
	  mouse <x> <y>                         one absolute mouse motion to viewport pixel (x, y), pushed into
	                                        SDL's queue, so the viewport's own event handling is the real path
	  key <sdl key name> <0|1>              one keyboard key down or up (Escape, Return, A ...), pushed into SDL's queue
	  exec <console command>                run on the viewport
	  probe <tag>                           one "NSDLInputTest: probe <tag> ..." state line
	  watch <ticks> <tag>                   one "NSDLInputTest: watch <tag> <i> <seconds> <yaw> <pitch> <speed>
	                                        <cursor x> <cursor y>" line per input tick, for <ticks> ticks (speed: the
	                                        horizontal one; cursor: the absolute mouse position, viewport pixels)
	  log <text>                            a marker line
	While the channel is open every key the pad sends to the engine is logged ("NSDLInputTest: key ...").
	Without the switch nothing here runs.
=============================================================================*/

#include <stdio.h>

class FNSDLInputTest
{
public:
	FNSDLInputTest( const TCHAR* InPath );
	~FNSDLInputTest();
	// Runs the commands that have arrived, stopping after one that changes the virtual pad so that every
	// change is seen by at least one event poll.
	void Tick( class UNSDLViewport* Viewport );

private:
	FILE*			File;
	ANSICHAR		Partial[1024];
	INT				PartialLen;
	SDL_Joystick*	Virtual;
	SDL_JoystickID	VirtualID;
	INT				MouseX, MouseY;		// the last position the mouse command moved to
	INT				WatchLeft;
	INT				WatchIndex;
	FString			WatchTag;
	UBOOL Execute( UNSDLViewport* Viewport, const TCHAR* Line );
	void Probe( UNSDLViewport* Viewport, const TCHAR* Tag );
	void Detach();
};
