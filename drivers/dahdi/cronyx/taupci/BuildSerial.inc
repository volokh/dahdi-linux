/*
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id:  $
 */

#ifndef BUILD_VERSION_INFO__TIMESTAMP
#   define BUILD_VERSION_INFO__TIMESTAMP 0x465F3D45ul
#endif

#ifndef BUILD_VERSION_INFO__SALT
#   define BUILD_VERSION_INFO__SALT 0x5CD5959Eul
#endif

#ifndef BUILD_VERSION_INFO__SALT2
#   define BUILD_VERSION_INFO__SALT2 0x5E7E0A2Bul
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
        1, 0, 0, 14,
        {2909494131ul, 320239084ul, 4293106451ul, 2341481491ul},
        778499ul, 17ul, 15040ul,
        1180646725ul,
        "Fri Jun 01 01:25:25 2007",
       __LY_BuildVersionInfo_format
        /* C035E8AE-F463-4d20-BCEB-077D6A8C4693 */
    };

#endif

#ifdef __cplusplus
}
#endif

/*
Source files:
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\cpddk.c
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\taupci-ddk.c

Include files:
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\ctaupci.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\ds2153.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\fw-2e1-reva.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\fw-2e1-revb.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\fw-4e1-reva.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\fw-4e1-revb.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\fw-e3-revb.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\lxt318.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\peb20534.h
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\taupci-ddk.h
    C:\Devel\Lib\Whistler\__stdinc.h
    C:\Devel\Lib\Whistler\Style.h
    C:\Devel\Lib\Whistler\WhistlerCrt.h

Other files:
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\nobf-tokens.txt
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\taupci-ddk.comment

Include search dirs:
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private
    C:\Devel\Lib\C++
    C:\Devel\Lib\Whistler
    C:\WINDDK\3790\inc\ddk\w2k
    C:\WINDDK\3790\inc\ddk\wdm\w2k
    C:\WINDDK\3790\inc\w2k

Excluded files:
    c:\Devel\_cvs\adapters\sync\tau-pci\ddk\private\BuildSerial.inc

Sources files: 2, include: 13, binary: 0, other: 2, excluded: 1, include search dirs: 7.
Total 17 files, 15040 lines in project 'ddk', size is 760.25 Kbytes, ripemd-128 digest is 'AD6B5B73131675ECFFE39B138B902C13'.

*/
