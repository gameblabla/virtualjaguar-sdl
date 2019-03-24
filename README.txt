Virtual Jaguar SDL
==================

Stripped down port of Virtual Jaguar 1.07 SDL with some backported code from upstream. (gpu.c was fully updated from upstream, other code is only a mix)
This is mostly intended for OpenDingux platforms like the GCW0 & RS-97 but can also be easily adapted to other platforms like the Pandora, Bittboy etc...

The Musashi core has been updated to version 3.4, making it free software unlike the older version which forbade non-commercial usage.
(Newer Virtual Jaguar versions are using WinUAE M68K core instead on compatible setting, so it's actually much slower.)

Compatibility
=============

Currently not the best emulator for Jaguar games.
Obviously no CD support (preliminear CD code got removed as it is useless) and some games flat out won't run.
Make sure to use the DSP enabled binary for best compatibility.

License 
========

Virtual Jaguar is licensed under the GPLv2, see license in docs/gpl.txt for more details.

Musashi M68K v3.4 is licensed under these terms :

Copyright © 1998-2001 Karl Stenerud

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
