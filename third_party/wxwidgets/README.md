# wxWidgets sources for Android

OpenCPN on Android contains only the parts of wxWidgets that OpenCPN itself
uses. The files here are taken unchanged from wxWidgets 3.1.0
(https://github.com/wxWidgets/wxWidgets/releases/tag/v3.1.0) and are only
built into the Android plugin, for the classes it needs that are missing
there.

- `fs_mem.cpp`: wxMemoryFSHandler, for the icons in the HTML pages.

wxWidgets is distributed under the wxWindows Library Licence.
