// iw3mp (Xbox 360 recomp) adapter for Bot Warfare.
// Chunk 1: stock-builtins-only stubs so the whole BW graph COMPILES + _bot::init runs.
// Movement (botmoveto/botaction/botstop) and file I/O (fs_*) are stubbed here; they will be
// switched to native builtins in Chunk 2. botangles uses the stock setplayerangles builtin.
init()
{
	level.bot_builtins[ "printconsole" ] = ::do_printconsole;
	level.bot_builtins[ "fileexists" ] = ::do_fileexists;
	level.bot_builtins[ "botaction" ] = ::do_botaction;
	level.bot_builtins[ "botstop" ] = ::do_botstop;
	level.bot_builtins[ "botmovement" ] = ::do_botmovement;
	level.bot_builtins[ "botmoveto" ] = ::do_botmoveto;
	level.bot_builtins[ "botmeleeparams" ] = ::do_botmeleeparams;
	level.bot_builtins[ "botangles" ] = ::do_botangles;
	level.bot_builtins[ "isbot" ] = ::do_isbot;
	level.bot_builtins[ "fs_fopen" ] = ::do_fs_fopen;
	level.bot_builtins[ "fs_fclose" ] = ::do_fs_fclose;
	level.bot_builtins[ "fs_readline" ] = ::do_fs_readline;
	level.bot_builtins[ "fs_writeline" ] = ::do_fs_writeline;
}

do_printconsole( s )
{
	println( s );
}

do_fileexists( file )
{
	return false;
}

do_botaction( action )
{
	self botaction( action );
}

do_botstop()
{
	self botstop();
}

do_botmovement( forward, right )
{
	self botmovement( forward, right );
}

do_botmoveto( where )
{
	self botmoveto( where );
}

do_botmeleeparams( yaw, dist )
{
}

do_botangles( angles )
{
	self setplayerangles( angles );
}

do_isbot()
{
	// iw3mp has no native isbot builtin; BW marks its own bots via self.pers["isBot"] (set in add_bot
	// right after addtestclient). is_bot() gates the whole bot-AI setup (_bot.gsc:501), so this must
	// return true for BW bots or their AI never starts.
	return ( isdefined( self.pers[ "isBot" ] ) && self.pers[ "isBot" ] );
}

do_fs_fopen( file, mode )
{
	return -1;
}

do_fs_fclose( fh )
{
}

do_fs_readline( fh )
{
	return "";
}

do_fs_writeline( fh, contents )
{
}
