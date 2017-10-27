/**
 * @file XTmemory.h
 * @author Francesco Sacco
 * @date 27 Oct 2017
 * @version 0
 * @brief Header file of memory control library.
 *
 * This library controls the Emulator memory. It'll provide 1MB of RAM memory
 * and 64K of IO.
 *
 * Based on:
 * 8086tiny:
 * Copyright (c) 2013-2014 Adrian Cable - http://www.megalith.co.uk/8086tiny
 * 8086tiny modifications and WIN32 interface class implementation:
 * Copyright (c) 2014 Julian Olds - https://jaybertsoftware.weebly.com/8086-tiny-plus.html
 *
 * This work is licensed under the MIT License. See included LICENSE.TXT.
 *
 * @see https://github.com/francescosacco/tinyXT
 */

 #ifndef _XTMEMORY_
 #define _XTMEMORY_

 #define RAM_SIZE                                0x10FFF0 // 1M + 65,520 B
 #define IO_PORT_COUNT                           0x10000  // 64KB

extern unsigned char mem[] ;
extern unsigned char io_ports[] ;

#endif // _XTMEMORY_
