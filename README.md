# DCC-P2P-BlockChain-Chat

An implementation of a toy P2P Blockchain distributed protocol node. It essentially keeps a distributed message archive, with "transactions" (message additions) being validated with MD5 hashes, that must be mined for each message.
Think mini-bitcoin, but for chatting instead of exchanging fictional currency.
Strictly IPv4, because IPv6 is too much work :D.


Extreme level of commenting, because it's a university assignment, so why not.


NOTE: MUST be compiled with gcc for 64 bit architectures, since we use a few nifty x64 implementation-specific things, such as 128 bit primitive types. Should compile on most Linux and MacOS environments, given OpenSSL is installed.
