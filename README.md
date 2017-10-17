# YATinyWinFTP
Fast and tiny FTP server for Windows

At some point I found myself in a need of ftp server for windows to test things. 
Googling gave me ftpdmin, which is free and really tiny, also opensource. 
However it struck me as being real slow - downloads on 1GB network were mere 5MB/sec 
with one core fully busy. Looking inside it was thread per connection and 4kb blocking 
network calls. Of course I had to roll your own FTP server after that!

Uses IOCP via asio, io_service per core, uses async (with some ugly hacks in uploads).
Also uses TransmitFile for downloads which is virtually free. Borrows a bit of source
from ftpdmin.

HAS BUGS (one thing I`m sure of)

HAS ZERO security hardening.

Usage: TinyWinFTP.exe Dir Port
