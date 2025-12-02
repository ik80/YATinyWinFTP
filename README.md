# YATinyWinFTP

## Overview
Fast and tiny FTP server for Windows

At some point I found myself in a need of ftp server for windows to test things. 
Googling gave me ftpdmin, which is free and really tiny, also opensource. 
However it struck me as being real slow - downloads on 1GB network were mere 5MB/sec 
with one core fully busy. Looking inside it was thread per connection and 4kb blocking 
network calls. Of course I had to roll my own FTP server after that!

Uses IOCP via asio, io_service per core, all operations are async, preallocates ~1Mb per 
session. Uses TransmitFile for downloads which is virtually free. Borrows a bit of source
from ftpdmin.

HAS BUGS (one thing I`m sure of!)

HAS ZERO security hardening.

## Building
- Install Visual Studio Community Edition
- Install CMake

```
mkdir build
cd build
cmake ..
cmake --build .
```


## Usage
Usage: TinyWinFTP.exe \<AbsolutePath\> \<Port\>

Example: TinyWinFTP.exe E:\Temp 21
