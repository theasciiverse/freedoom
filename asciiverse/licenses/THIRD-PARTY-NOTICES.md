# Third-party notices

Freedoom's released builds link these libraries in, for the chafa picture
(Options, Picture). Their licence texts are in this folder.

| Library | Copyright | Licence | Text |
|---|---|---|---|
| chafa, by Hans Petter Jansson (hpjansson.org/chafa) | 2018 on, Hans Petter Jansson | LGPL-3.0-or-later | `LGPL-3.0.txt`, with `GPL-3.0.txt` |
| GLib, the GNOME project's core library (gitlab.gnome.org/GNOME/glib) | The GLib authors | LGPL-2.1-or-later | `LGPL-2.1.txt` |
| sysprof-capture, from Sysprof, when GLib uses it | 2016 on, Christian Hergert and others | BSD-2-Clause-Patent | below |
| PCRE2, when GLib uses it | 1997 on, University of Cambridge | BSD-3-Clause with the binary library-like packages exemption | below |

With chafa linked in, the game as a whole is distributed under the GPL,
version 3 or later (`GPL-3.0.txt`); its own source, doom-ascii and
Chocolate Doom's, is GPL-2.0-or-later. The full source, ours and the
engine's, is in the game's repository; chafa's and GLib's sources are at
the addresses above.

## sysprof-capture: BSD-2-Clause-Patent

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

Subject to the terms and conditions of this license, each copyright holder
and contributor hereby grants to those receiving rights under this license
a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
irrevocable (except for failure to satisfy the conditions of this license)
patent license to make, have made, use, offer to sell, sell, import, and
otherwise transfer this software, where such license applies only to those
patent claims, already acquired or hereafter acquired, licensable by such
copyright holder or contributor that are necessarily infringed by:

(a) their Contribution(s) (the licensed copyrights of copyright holders
    and non-copyrightable additions of contributors, in source or binary
    form) alone; or

(b) combination of their Contribution(s) with the work of authorship to
    which such Contribution(s) was added by such copyright holder or
    contributor, if, at the time the Contribution is added, such addition
    causes such combination to be necessarily infringed. The patent license
    shall not apply to any other combinations which include the
    Contribution.

Except as expressly stated above, no rights or licenses from any copyright
holder or contributor is granted under this license, whether expressly, by
implication, estoppel or otherwise.

DISCLAIMER

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


## PCRE2: BSD-3-Clause with exemption

Copyright (c) 1997 on, University of Cambridge. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

   * Neither the name of the University of Cambridge nor the names of any
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


EXEMPTION FOR BINARY LIBRARY-LIKE PACKAGES
------------------------------------------

The second condition in the BSD licence (covering binary redistributions) does
not apply all the way down a chain of software. If binary package A includes
PCRE2, it must respect the condition, but if package B is software that
includes package A, the condition is not imposed on package B unless it uses
PCRE2 independently.

