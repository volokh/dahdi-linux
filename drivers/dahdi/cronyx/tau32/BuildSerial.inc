/*
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id: $
 */

#ifndef BUILD_VERSION_INFO__TIMESTAMP
#   define BUILD_VERSION_INFO__TIMESTAMP 0x465F3778ul
#endif

#ifndef BUILD_VERSION_INFO__SALT
#   define BUILD_VERSION_INFO__SALT 0x468B9B14ul
#endif

#ifndef BUILD_VERSION_INFO__SALT2
#   define BUILD_VERSION_INFO__SALT2 0xC3F1B717ul
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
struct tag__LY_BuildVersionInfo
    {
        unsigned __int16 MajorVersion, MinorVersion, Revision, BuildSerial;
        unsigned __int32 SourcesDigest128[4];
        unsigned __int32 SourcesSize, SourcesFiles, SourcesLines;
        unsigned __int32 TimestampUTC;
        const char *TimestampText;
        const char *SelfFormatCPP;
#else
struct tag__LY_BuildVersionInfo
    {
        unsigned short MajorVersion, MinorVersion, Revision, BuildSerial;
        unsigned SourcesDigest128[4];
        unsigned SourcesSize, SourcesFiles, SourcesLines;
        unsigned TimestampUTC;
        const char *TimestampText;
        const char *SelfFormatCPP;
#endif
    };

extern const
#ifdef _WIN32
   __declspec(dllexport)
#endif
char __LY_BuildVersionInfo_format[];

extern const
#ifdef _WIN32
   __declspec(dllexport)
#endif
struct tag__LY_BuildVersionInfo __LY_BuildVersionInfo;

#ifdef __LY_BuildVersionInfo_INSTALL

#ifdef _WIN32
#pragma comment(linker, "/version:1.0")
#endif

const
#ifdef _WIN32
   __declspec(selectany)
#endif
char __LY_BuildVersionInfo_format[] =
    "\r\n"
    "\r\n"
    "extern \"C\" __declspec(dllexport) const struct tag__LY_BuildVersionInfo\r\n"
    "{\r\n"
    "    unsigned __int16 MajorVersion, MinorVersion, Revision, BuildSerial;\r\n"
    "    unsigned __int32 SourcesDigest128[4];\r\n"
    "    unsigned __int32 SourcesSize, SourcesFiles, SourcesLines;\r\n"
    "    unsigned __int32 TimestampUTC;\r\n"
    "    const char *TimestampText = __TIMESTAMP__;\r\n"
    "    const char *SelfFormatCPP = \"This text\";\r\n"
    "} __LY_BuildVersionInfo;\r\n"
    "\r\n"
    "\r\n";

const
#ifdef _WIN32
   __declspec(selectany)
#endif
struct tag__LY_BuildVersionInfo __LY_BuildVersionInfo =
    {
        /* 4631B927-CB9F-4fae-88DF-C9C3ECE0BD53 */
        1, 0, 0, 130,
        {2252830769ul, 2742750695ul, 130622428ul, 1784936322ul},
        544631ul, 17ul, 14262ul,
        1180645240ul,
        "Fri Jun 01 01:00:40 2007",
       __LY_BuildVersionInfo_format
        /* C035E8AE-F463-4d20-BCEB-077D6A8C4693 */
    };

#endif

#ifdef __cplusplus
}
#endif

/*
Source files:
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\munich32x.c
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\tau32-ddk.c
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\tau32.c

Include files:
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\ct32reg.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\ds21554.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\firmware-tau32-lite.inc
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\firmware-tau32.inc
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\munich32x-debug.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\munich32x.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\queue_defs.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\tau32.h
    c:\Devel\_cvs\adapters\sync\tau32\ddk\tau32-ddk.h
    C:\Devel\Lib\Whistler\__stdinc.h
    C:\Devel\Lib\Whistler\Style.h
    C:\Devel\Lib\Whistler\WhistlerCrt.h

Other files:
    c:\Devel\_cvs\adapters\sync\tau32\ddk\nobf-tokens.txt
    c:\Devel\_cvs\adapters\sync\tau32\ddk\tau32-ddk.comment

Include search dirs:
    c:\Devel\_cvs\adapters\sync\tau32\ddk
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private
    C:\Devel\Lib\C++
    C:\Devel\Lib\Whistler
    C:\WINDDK\3790\inc\ddk\w2k
    C:\WINDDK\3790\inc\ddk\wdm\w2k
    C:\WINDDK\3790\inc\w2k

Excluded files:
    c:\Devel\_cvs\adapters\sync\tau32\ddk\private\BuildSerial.inc

Sources files: 3, include: 12, binary: 0, other: 2, excluded: 1, include search dirs: 7.
Total 17 files, 14262 lines in project 'ddk', size is 531.87 Kbytes, ripemd-128 digest is '86477831A37B0DE707C923DC6A63F782'.

*/
