# DCC-P2P-BlockChain-Chat

An implementation of a toy P2P Blockchain distributed protocol node. It
essentially keeps a distributed message archive, with "transactions" (message
additions) being validated with MD5 hashes, that must be mined for each message.
Think mini-bitcoin, but for chatting instead of exchanging fictional currency.
Strictly IPv4, because IPv6 is too much work :D.


Extreme level of commenting, because it's a university assignment, so why not.


#Building

To build, simply run "make", the Makefile target rules should work for most
Linux distributions as well as MacOS, as long as OpenSSL is installed.


NOTE: MUST be compiled with gcc for 64 bit architectures, since we use a few
nifty x64 implementation-specific things, such as 128 bit primitive types.


#Running

To run the program from the command line, use the following syntax:

	./blockchain <initial peer IP> <local IP>

Where initial peer IP is the IPv4 address for a peer that you wish to actively
connect to at the beginning of execution. Type in a bogus IP to not connect to
anyone and simply listen for connections passively.

Local IP should be the IPv4 address for the interface where the program will be
listening for connections, to avoid self-connection attempts. This could have
been implemented more elegantly using a STUN protocol, but that would have added
significant complexity to the project, so we use this workaround.

#Functionalities

When the program is running, the terminal will prompt the user for messages to
be added to the currently active archive. If any message typed in is valid, it
will be inserted in the archive, and the new archive will be published to all
the currently connected peers.

For each connected peer, the implementation creates a log file in the running
folder, with format "x.log", where x is the file descriptor ID associated with
the peer. This is to avoid the standard output streams (stderr/stdout) from
being flooded with information from all the different peers. To observe the
communication behavior with any given peer, simply peer into the corresponding
log file.

If "exit" is typed into the main terminal, the program exits, to guarantee that
the output buffers are all flushed appropriately, which doesn't happen when
interrupting with the usual CTRL+C.
