/*
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 *
 */

#ifndef OSHeaders_H
#define OSHeaders_H
#include <limits.h>

#define kSInt16_Max USHRT_MAX
#define kUInt16_Max USHRT_MAX

#define kSInt32_Max LONG_MAX
#define kUInt32_Max ULONG_MAX

#define kSInt64_Max LONG_LONG_MAX
#define kUInt64_Max ULONG_LONG_MAX


#ifndef TRUE
    #define TRUE 1
#endif

#ifndef FALSE
    #define FALSE 0
#endif



/* Platform-specific components */
#if __linux__ || __linuxppc__ || __FreeBSD__ || __MacOSX__
    
    /* Defines */
    #define _64BITARG_ "q"

    /* paths */
    #define kEOLString "\n"
    #define kPathDelimiterString "/"
    #define kPathDelimiterChar '/'
    #define kPartialPathBeginsWithDelimiter 0

    /* Includes */
    #include <sys/types.h>
    
    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    typedef unsigned int        PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned long       UInt32;
    typedef signed long         SInt32;
    typedef signed long long    SInt64;
    typedef unsigned long long  UInt64;
    typedef float               Float32;
    typedef double              Float64;
    typedef UInt16              Bool16;
    typedef UInt8               Bool8;
    
    typedef unsigned long       FourCharCode;
    typedef FourCharCode        OSType;

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif
        
    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )







#elif __Win32__
    
    /* Defines */
    #define _64BITARG_ "I64"

    /* paths */
    #define kEOLString "\r\n"
    #define kPathDelimiterString "\\"
    #define kPathDelimiterChar '\\'
    #define kPartialPathBeginsWithDelimiter 0
    
    #define crypt(buf, salt) ((char*)buf)
    
    /* Includes */

	//Amended by FYM on 2008-09-23 22:23:36 目的是指示编译器不要包含与MFC相关的操作,避免MFC的winsock代码与QTSS自有的冲突
	#define WIN32_LEAN_AND_MEAN

/*
Minimum System Required 					Macros to Define
Windows 95 and Windows NT 4.0 				WINVER=0x0400
Windows 98 and Windows NT 4.0			 	_WIN32_WINDOWS=0x0410 and WINVER=0x0400
Windows NT 4.0								_WIN32_WINNT=0x0400 and WINVER=0x0400
Windows 98 									_WIN32_WINDOWS=0x0410
Windows 2000 								_WIN32_WINNT=0x0500 and WINVER=0x0500
Windows Me 									_WIN32_WINDOWS=0x0490
Windows XP and Windows .NET Server 			_WIN32_WINNT=0x0501 and WINVER=0x0501
Windows Server 2003							_WIN32_WINNT=0x0502 and WINVER=0x0502
Windows Vista								_WIN32_WINNT=0x0600

Internet Explorer 3.0, 3.01, 3.02 			_WIN32_IE=0x0300
Internet Explorer 4.0 						_WIN32_IE=0x0400
Internet Explorer 4.01 						_WIN32_IE=0x0401
Internet Explorer 5.0, 5.0a, 5.0b 			_WIN32_IE=0x0500
Internet Explorer 5.01, 5.5 				_WIN32_IE=0x0501
Internet Explorer 6.0 						_WIN32_IE=0x0560 or _WIN32_IE=0x0600

如果_WIN32_WINNT=0x0600表明程序支持Windows Vista以及以前的版本
*/
	//Amended by FYM on 2008-09-23 22:23:36 to remove #include <wspiapi.h> in other head files
	#ifdef _WIN32_WINNT
	#undef _WIN32_WINNT
	#define _WIN32_WINNT 0x0600
	#else
	#define _WIN32_WINNT 0x0600
	#endif



    #include <windows.h>
    #include <winsock2.h>
    #include <mswsock.h>
    #include <process.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <direct.h>
    #include <errno.h>

	//Amended by FYM on 2008-09-23 22:23:36 for timeGetTime functions because define WIN32_LEAN_AND_MEAN
	#include <mmsystem.h>

	//Amended by FYM on 2008-09-23 22:23:36 for "warning C4996: '_snprintf' was declared deprecated"
	#pragma warning(disable:4996)

    #define R_OK 0
    #define W_OK 1
        
    // POSIX errorcodes
    #define ENOTCONN 1002
    #define EADDRINUSE 1004
    #define EINPROGRESS 1007
    #define ENOBUFS 1008
    #define EADDRNOTAVAIL 1009

    // Winsock does not use iovecs
    struct iovec {
        u_long  iov_len; // this is not the POSIX definition, it is rather defined to be
        char FAR*   iov_base; // equivalent to a WSABUF for easy integration into Win32
    };
    
    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    typedef unsigned int        PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned long       UInt32;
    typedef signed long         SInt32;
    typedef LONGLONG            SInt64;
    typedef ULONGLONG           UInt64;
    typedef float               Float32;
    typedef double              Float64;
    typedef UInt16              Bool16;
    typedef UInt8               Bool8;
    
    typedef unsigned long       FourCharCode;
    typedef FourCharCode        OSType;

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif
        
    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )

    #define kSInt16_Max USHRT_MAX
    #define kUInt16_Max USHRT_MAX
    
    #define kSInt32_Max LONG_MAX
    #define kUInt32_Max ULONG_MAX
    
    #undef kSInt64_Max
    #define kSInt64_Max  9223372036854775807i64
    
    #undef kUInt64_Max
    #define kUInt64_Max  (kSInt64_Max * 2ULL + 1)

#elif __sgi__
    /* Defines */
    #define _64BITARG_ "ll"

    /* paths */
    #define kPathDelimiterString "/"
    #define kPathDelimiterChar '/'
    #define kPartialPathBeginsWithDelimiter 0
	#define	kEOLString "\n"

    /* Includes */
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <pthread.h>
    
    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    typedef unsigned char       boolean;
    #define true                1
    #define false               0

    typedef unsigned int        PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned long       UInt32;
    typedef signed long         SInt32;
    typedef signed long long    SInt64;
    typedef unsigned long long  UInt64;
    typedef float               Float32;
    typedef double              Float64;
	
	typedef UInt16				Bool16;

	typedef unsigned long		FourCharCode;
	typedef FourCharCode		OSType;

	/* Nulled-out new() for use without memory debugging */
	/* #define NEW(t,c,v) new c v
	#define NEW_ARRAY(t,c,n) new c[n] */

    #define thread_t    pthread_t
    #define cthread_errno() errno

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif
        
    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )

#elif defined(sun) // && defined(sparc)

    /* Defines */
    #define _64BITARG_ "ll"

    /* paths */
    #define kPathDelimiterString "/"
    #define kPathDelimiterChar '/'
    #define kPartialPathBeginsWithDelimiter 0
    #define kEOLString "\n"

    /* Includes */
    #include <sys/types.h>
    #include <sys/byteorder.h>
    
    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    //typedef unsigned char     Bool16;
    //#define true              1
    //#define false             0

    typedef unsigned int        PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned long       UInt32;
    typedef signed long         SInt32;
    typedef signed long long    SInt64;
    typedef unsigned long long  UInt64;
    typedef float               Float32;
    typedef double              Float64;
    typedef UInt16              Bool16;
    typedef UInt8               Bool8;
    
    typedef unsigned long       FourCharCode;
    typedef FourCharCode        OSType;

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif
        
    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )

#elif defined(__hpux__)

    /* Defines */
    #define _64BITARG_ "ll"

    /* paths */
    #define kPathDelimiterString "/"
    #define kPathDelimiterChar '/'
    #define kPartialPathBeginsWithDelimiter 0
    #define kEOLString "\n"

    /* Includes */
    #include <sys/types.h>
    #include <sys/byteorder.h>

    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    //typedef unsigned char     Bool16;
    //#define true              1
    //#define false             0

    typedef unsigned int        PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned long       UInt32;
    typedef signed long         SInt32;
    typedef signed long long    SInt64;
    typedef unsigned long long  UInt64;
    typedef float               Float32;
    typedef double              Float64;
    typedef UInt16              Bool16;
    typedef UInt8               Bool8;

    typedef unsigned long       FourCharCode;
    typedef FourCharCode        OSType;

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif

    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )

#elif defined(__osf__)
    
   /* Defines */
    #define _64BITARG_ "l"

    /* paths */
    #define kEOLString "\n"
    #define kPathDelimiterString "/"
    #define kPathDelimiterChar '/'
    #define kPartialPathBeginsWithDelimiter 0

    /* Includes */
    #include <sys/types.h>
    #include <machine/endian.h>
    
    /* Constants */
    #define QT_TIME_TO_LOCAL_TIME   (-2082844800)
    #define QT_PATH_SEPARATOR       '/'

    /* Typedefs */
    typedef unsigned long       PointerSizedInt;
    typedef unsigned char       UInt8;
    typedef signed char         SInt8;
    typedef unsigned short      UInt16;
    typedef signed short        SInt16;
    typedef unsigned int        UInt32;
    typedef signed int          SInt32;
    typedef signed long         SInt64;
    typedef unsigned long       UInt64;
    typedef float               Float32;
    typedef double              Float64;
    typedef UInt16              Bool16;
    typedef UInt8               Bool8;
    
    typedef unsigned int        FourCharCode;
    typedef FourCharCode        OSType;

    #ifdef  FOUR_CHARS_TO_INT
    #error Conflicting Macro "FOUR_CHARS_TO_INT"
    #endif

    #define FOUR_CHARS_TO_INT( c1, c2, c3, c4 )  ( c1 << 24 | c2 << 16 | c3 << 8 | c4 )

    #ifdef  TW0_CHARS_TO_INT
    #error Conflicting Macro "TW0_CHARS_TO_INT"
    #endif
        
    #define TW0_CHARS_TO_INT( c1, c2 )  ( c1 << 8 | c2 )


#endif

typedef SInt32 OS_Error;

enum
{
    OS_NoErr = (OS_Error) 0,
    OS_BadURLFormat = (OS_Error) -100,
    OS_NotEnoughSpace = (OS_Error) -101
};


#endif /* OSHeaders_H */
