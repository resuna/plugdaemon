![Plugademon Logo](https://raw.githubusercontent.com/resuna/plugdaemon/master/plugdaemon2.gif)

# Plugdaemon

Plugdaemon is a tool that is inspired by, but not based on, the plug-gw from Trusted Information Systems. It was originally a simpler wannabe, but the connection balancing code and other features makes it a lot more useful (as of 2.3.1 it's also a feature-complete replacement for plug-gw).

Not all the planned features are implemented in 2.x. Eventually plugdaemon will provide a complete load balancing and monitoring package for servers.

Plugdaemon is released for any use, commercial or otherwise, so long as attribution is retained. If you do anything interesting with it, let me know. If you feel generous, tip me.

The 1.2 code is missing some functionality, but is simpler code and will compile on K&R compilers for SunOS 4 and Xenix fans. Check out the 1.2.2 branch.

### Recent changes:

2.5.5 has some cleanup for warnings in more recent versions of gcc. This includes a possible format injection in syslog detected by -Wformat-security.

2.5.4 fixes a problem where a remote host closed a connection too quickly, and getpeername failed. Plugdaemon was treating this as something that could only happen after the child was forked, and was aborting, instead of logging the error and closing the socket.

2.5.3 is stable and has fixes for several bugs missed when I integrated various patches, thanks to Alexander Lazic and Ben Low, as well as an improved table lookup that should prevent a process runaway under load.

### Ironic statement from ten years ago about future plans:

I'm going to completely redo the command line syntax in 3.0, because it's gotten rather cruddy. Suggestions will be appreciated... also ideas for new features would be timely.

## Documentation: (the version an option was added is in parentheses)

### PLUG(1)

### NAME
  plug -- Plug proxy daemon.

### SYNOPSIS
  plug -V

  plug [-nflkd...] [-i addr] [-p addr] [-t seconds] [-r seconds] [-a net[/bits]]... [-h addr:port] port addr[:port] [addr[:port]]...

### DESCRIPTION
  Plugdaemon acts as a "dumb proxy", forwarding a TCP/IP stream from a port
  on one host to a possibly different port on a separate host. It runs as a
  daemon to reduce latency in setting up a connection, and optionally logs
  every connection via syslog.

### OPTIONS

<dl compact>
<dt>-f<dd>  Forces a given client address to continue to connect to the same host on subsequent attempts, for proxying HTTP connections so that subsequent hits will be on the same mirror.  
<dt>-k<dd>  Turns on SO_KEEPALIVE on the plug. You want to use this on frequent short term connections like HTTP requests where response time is more important than reliability on flakey links, and leave it off on long- term connections that may go a long time without transferring data.  
<dt>-l <i>(2.5)</i>
<dd>  turns on connection logging.
<dt>-P pidfile <i>(2.2)</i> <dd>    maintains a file that contains the process ID of the master plug dae- mon, followed by the process IDs of all the active children. This can be used for cleanup or monitoring. The file is deleted when the parent process exits.  
<dt>-d<dd>  turns on debugging output and stops plug from logging errors to syslog. Errors in this mode are displayed on standard error.  Additional -d options add more output. (implies -n) 
<dt>-i interface
<dd>    Bind the plug to the named interface, for use on dual-homed hosts.  
<dt>-p interface <i>(2.0.2)</i>
<dd>    Bind the source port of the proxied connection to the named interface, likewise.  
<dt>-a accept_rule <i>(2.3)</i>
<dd>    Accept connections that match the rule.  Currently, the  rule is an ip address and an optional subnet, e.g. -a 192.168.2.0/24 to accept connections  from the  Class-C  subnet 192.168.2. All 4 octets of the address must be provided. If no rules are specified connections are allowed from any address.  
<dt>-t timeout <i>(2.4)</i>
<dd>    Timeout for forced connections, after no attempts in this period it will connect to a new (pseudo-)randomly selected server. The default is 1 hour.  
<dt>-o <i>(2.4)</i>
<dd>    Direct all connections to the first valid server instead of load- balancing.  
<dt>-r retry <i>(2.4)</i>
<dd>    Timeout for downed servers; if specified, then a dead server is retried after this many seconds.  If not specified, then a dead server stays out of the pool until all have failed or plugdaemon is res- tarted, then all are retried again.  
<dt>-V <i>(1.1.3)</i>
<dd>    Display version and exit.  
<dt>-n <i>(2.5)</i>
<dd>   Don't detach, run in the foreground.  
<dt>-S filename <i>(2.5)</i>
<dd>    log sessions to the named file. If the file is "-", log to standard output (which implies -n).  
<dt>-h address:port <i>(2.5)</i>
<dd>    Use HTTPS proxy at address:port to make connections, rather than connecting directly.
</dl>

### EXAMPLES

  To proxy an NNTP connection through a firewall to a host at 10.0.3.15:

  plug -i 192.168.0.14 119 10.0.3.15

### BUGS

  Plugdaemon only accepts numeric IP addresses and services.

  The syntax is rather clumsy, but I'm deferring cleanup until version
  3.0. The main thing I'd like to do is get rid of the -i option and
  allow any of the following forms for the source: port, :port,
  *:port, address:port, or source/interface (to specify
  the outgoing interface).

   As well as regularise the various flags other people have added that
   I've kept to keep from breaking their scripts.

### SECURITY FEATURES

  Plugdaemon only accepts numeric IP addresses and services.

  I don't call gethostbyname anywhere to keep someone from managing
  to fake it out by spoofing the firewall's DNS lookups, but I think
  that should be an option so 3.0 will probably disable it unless
  selected at compile time.

### LICENSE

  Plugdaemon is released under a "Berkeley" style license. See the file
  LICENSE for details.

### AUTHOR

  Peter da Silva <resuna at gmail.com>

----------

[1] There is a bug in 2.2 and earlier versions that can lead to a corrupted internal process table under extremely high load conditions, and a plug "hanging" as it follows a wild rabbit down the rabbit hole forever (I can guarantee that an Alphaserver ES40 opening and closing connections as fast as it can will trigger it, and I'm glad it happened though I *was* rather cross at the time). This is a process leak, caused by multiple signal handlers writing process IDs in the same locations in a table. Security issue: denial of service.

